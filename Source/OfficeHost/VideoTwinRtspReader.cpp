#include "VideoTwinRtspReader.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

THIRD_PARTY_INCLUDES_START
extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
}
THIRD_PARTY_INCLUDES_END

namespace
{
    FString AvError(int Code)
    {
        char Buf[256] = {0};
        av_strerror(Code, Buf, sizeof(Buf));
        return FString(UTF8_TO_TCHAR(Buf));
    }
}

FVideoTwinRtspReader::FVideoTwinRtspReader(const FString& InUrl, const FString& InName)
    : Url(InUrl), Name(InName)
{
}

FVideoTwinRtspReader::~FVideoTwinRtspReader()
{
    Shutdown();
}

FString FVideoTwinRtspReader::MaskUrl(const FString& In)
{
    // scheme://user:password@host -> scheme://user:***@host
    const int32 Scheme = In.Find(TEXT("://"));
    const int32 At = In.Find(TEXT("@"));
    if (Scheme == INDEX_NONE || At == INDEX_NONE || At < Scheme) return In;
    const int32 Colon = In.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Scheme + 3);
    if (Colon == INDEX_NONE || Colon > At) return In;
    return In.Left(Colon + 1) + TEXT("***") + In.Mid(At);
}

bool FVideoTwinRtspReader::EnsureLibrariesLoaded(FString& OutError)
{
    static bool bTried = false;
    static bool bLoaded = false;
    static FString Error;
    if (!bTried)
    {
        bTried = true;
        // Delay-loaded (OfficeHost.Build.cs), so nothing happens until the first call. Loading them here by full path
        // means the delay-load resolver finds them already in the process: the editor's executable lives in the
        // engine folder and would not look in the project's Binaries otherwise. Dependency order.
        const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory()));
        const TCHAR* Dlls[] = { TEXT("avutil-60.dll"), TEXT("swresample-6.dll"), TEXT("swscale-9.dll"), TEXT("avcodec-62.dll"), TEXT("avformat-62.dll") };
        bLoaded = true;
        for (const TCHAR* Dll : Dlls)
        {
            const FString Path = FPaths::Combine(Dir, Dll);
            if (!FPlatformProcess::GetDllHandle(*Path))
            {
                Error = FString::Printf(TEXT("cannot load %s"), *Path);
                bLoaded = false;
                break;
            }
        }
        if (bLoaded)
        {
            // FFmpeg's own log goes to stderr, where nobody sees it, and some messages repeat the address.
            av_log_set_level(AV_LOG_QUIET);
            avformat_network_init();
        }
    }
    OutError = Error;
    return bLoaded;
}

bool FVideoTwinRtspReader::Start()
{
    FString Error;
    if (!EnsureLibrariesLoaded(Error)) { SetError(Error); return false; }
    if (Thread) return true;
    bStopRequested = false;
    Thread = FRunnableThread::Create(this, *FString::Printf(TEXT("VideoTwinRtsp_%s"), *Name), 0, TPri_AboveNormal);
    return Thread != nullptr;
}

void FVideoTwinRtspReader::Shutdown()
{
    if (!Thread) return;
    bStopRequested = true;
    Thread->WaitForCompletion();
    delete Thread;
    Thread = nullptr;
}

int FVideoTwinRtspReader::InterruptCallback(void* Opaque)
{
    const FVideoTwinRtspReader* Self = static_cast<const FVideoTwinRtspReader*>(Opaque);
    return (Self->bStopRequested || FPlatformTime::Seconds() > Self->IoDeadline.load()) ? 1 : 0;
}

void FVideoTwinRtspReader::SetError(const FString& Error)
{
    FScopeLock L(&Lock);
    if (Error != LastError) UE_LOG(LogTemp, Warning, TEXT("VideoTwin camera %s direct stream: %s"), *Name, *Error);
    LastError = Error;
}

FString FVideoTwinRtspReader::GetLastError() const
{
    FScopeLock L(&Lock);
    return LastError;
}

bool FVideoTwinRtspReader::TakeFrame(TArray<uint8>& Out, int32& OutWidth, int32& OutHeight, uint64& InOutSeq)
{
    FScopeLock L(&Lock);
    if (FrameSeq == InOutSeq || Frame.Num() == 0) return false;
    Out = Frame;
    OutWidth = FrameWidth;
    OutHeight = FrameHeight;
    InOutSeq = FrameSeq;
    return true;
}

double FVideoTwinRtspReader::SecondsSinceLastFrame() const
{
    FScopeLock L(&Lock);
    return LastFrameTime < 0 ? 1e9 : FPlatformTime::Seconds() - LastFrameTime;
}

float FVideoTwinRtspReader::GetRecentFps() const
{
    FScopeLock L(&Lock);
    return (LastFrameTime < 0 || FPlatformTime::Seconds() - LastFrameTime > 2.0) ? 0.f : RecentFps;
}

uint32 FVideoTwinRtspReader::Run()
{
    UE_LOG(LogTemp, Display, TEXT("VideoTwin camera %s direct stream: reading %s"), *Name, *MaskUrl(Url));
    while (!bStopRequested)
    {
        ReadOnce();
        // Reconnect after a failure or the end of the stream, without hammering the camera.
        for (int32 i = 0; i < 20 && !bStopRequested; ++i) FPlatformProcess::Sleep(0.1f);
    }
    return 0;
}

void FVideoTwinRtspReader::ReadOnce()
{
    AVFormatContext* Fmt = avformat_alloc_context();
    if (!Fmt) { SetError(TEXT("out of memory")); return; }
    Fmt->interrupt_callback.callback = &FVideoTwinRtspReader::InterruptCallback;
    Fmt->interrupt_callback.opaque = this;
    // Everything below is about not waiting: TCP (no reordering waits, no UDP loss on the office network),
    // no input buffering, no reorder queue, and a short probe because the camera sends its parameters up front.
    AVDictionary* Options = nullptr;
    av_dict_set(&Options, "rtsp_transport", "tcp", 0);
    av_dict_set(&Options, "fflags", "nobuffer", 0);
    av_dict_set(&Options, "flags", "low_delay", 0);
    av_dict_set(&Options, "max_delay", "0", 0);
    av_dict_set(&Options, "reorder_queue_size", "0", 0);
    av_dict_set(&Options, "probesize", "200000", 0);
    av_dict_set(&Options, "analyzeduration", "500000", 0);
    av_dict_set(&Options, "timeout", "5000000", 0);
    IoDeadline = FPlatformTime::Seconds() + 10.0;
    const FTCHARToUTF8 Utf8Url(*Url);
    int Result = avformat_open_input(&Fmt, Utf8Url.Get(), nullptr, &Options);
    av_dict_free(&Options);
    if (Result < 0) { SetError(FString::Printf(TEXT("cannot open (%s)"), *AvError(Result))); return; }   // Fmt is freed on failure

    AVCodecContext* Codec = nullptr;
    AVPacket* Packet = av_packet_alloc();
    AVFrame* Picture = av_frame_alloc();
    SwsContext* Sws = nullptr;
    int32 SwsW = 0, SwsH = 0, SwsFormat = -1, SwsRange = -1;
    TArray<uint8> Bgra;
    int VideoStream = -1;
    do
    {
        IoDeadline = FPlatformTime::Seconds() + 10.0;
        Result = avformat_find_stream_info(Fmt, nullptr);
        if (Result < 0) { SetError(FString::Printf(TEXT("no stream info (%s)"), *AvError(Result))); break; }
        const AVCodec* Decoder = nullptr;
        VideoStream = av_find_best_stream(Fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &Decoder, 0);
        if (VideoStream < 0 || !Decoder) { SetError(TEXT("no video stream / decoder")); break; }
        Codec = avcodec_alloc_context3(Decoder);
        if (!Codec || avcodec_parameters_to_context(Codec, Fmt->streams[VideoStream]->codecpar) < 0) { SetError(TEXT("decoder setup failed")); break; }
        // Slice threads only: frame threading holds back one frame per thread, which is latency we are here to remove.
        Codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        Codec->thread_type = FF_THREAD_SLICE;
        Codec->thread_count = 2;
        Result = avcodec_open2(Codec, Decoder, nullptr);
        if (Result < 0) { SetError(FString::Printf(TEXT("cannot open decoder (%s)"), *AvError(Result))); break; }
        UE_LOG(LogTemp, Display, TEXT("VideoTwin camera %s direct stream: connected, %s %dx%d"), *Name, UTF8_TO_TCHAR(Decoder->name), Codec->width, Codec->height);
        {
            FScopeLock L(&Lock);
            LastError.Reset();
        }

        while (!bStopRequested)
        {
            // A camera that stops sending must not hang the thread: 5 s without a packet ends this connection.
            IoDeadline = FPlatformTime::Seconds() + 5.0;
            Result = av_read_frame(Fmt, Packet);
            if (Result < 0) { if (!bStopRequested) SetError(FString::Printf(TEXT("stream ended (%s)"), *AvError(Result))); break; }
            if (Packet->stream_index == VideoStream && avcodec_send_packet(Codec, Packet) >= 0)
            {
                while (avcodec_receive_frame(Codec, Picture) == 0)
                {
                    const int32 W = Picture->width, H = Picture->height;
                    // The cameras send full-range ("pc") YUV; scaling it as video range would crush blacks and whites.
                    const bool bFull = Picture->color_range == AVCOL_RANGE_JPEG || Picture->format == AV_PIX_FMT_YUVJ420P
                        || Picture->format == AV_PIX_FMT_YUVJ422P || Picture->format == AV_PIX_FMT_YUVJ444P;
                    if (!Sws || W != SwsW || H != SwsH || Picture->format != SwsFormat || int32(bFull) != SwsRange)
                    {
                        if (Sws) sws_freeContext(Sws);
                        Sws = sws_getContext(W, H, AVPixelFormat(Picture->format), W, H, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                        if (!Sws) { SetError(TEXT("colour conversion setup failed")); break; }
                        const int Space = (Picture->colorspace == AVCOL_SPC_BT709) ? SWS_CS_ITU709 : SWS_CS_ITU601;
                        sws_setColorspaceDetails(Sws, sws_getCoefficients(Space), bFull ? 1 : 0, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
                        SwsW = W; SwsH = H; SwsFormat = Picture->format; SwsRange = int32(bFull);
                    }
                    Bgra.SetNumUninitialized(W * H * 4);
                    uint8* Dst[4] = { Bgra.GetData(), nullptr, nullptr, nullptr };
                    int DstStride[4] = { W * 4, 0, 0, 0 };
                    sws_scale(Sws, Picture->data, Picture->linesize, 0, H, Dst, DstStride);
                    av_frame_unref(Picture);
                    const double Now = FPlatformTime::Seconds();
                    FScopeLock L(&Lock);
                    Swap(Frame, Bgra);
                    FrameWidth = W; FrameHeight = H;
                    ++FrameSeq;
                    LastFrameTime = Now;
                    ++FpsWindowCount;
                    if (Now - FpsWindowStart >= 2.0)
                    {
                        RecentFps = FpsWindowStart > 0 ? float(FpsWindowCount / (Now - FpsWindowStart)) : 0.f;
                        FpsWindowStart = Now;
                        FpsWindowCount = 0;
                    }
                }
            }
            av_packet_unref(Packet);
        }
    } while (false);

    if (Sws) sws_freeContext(Sws);
    av_frame_free(&Picture);
    av_packet_free(&Packet);
    avcodec_free_context(&Codec);
    avformat_close_input(&Fmt);
}
