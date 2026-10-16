// Pulls one camera stream outside UE the way VideoTwinRtspReader does, and reports how fast frames come out
// (2026-10-16, used to find why the 2560x1440 main stream lagged inside UE; see Docs/VideoTwin_Packaging.md 8.13).
//
//   usage: rtsp_decode_test <env var holding the rtsp url> <mode> <seconds>
//   mode is a set of tags, e.g. "slice", "frame", "frameT", "hwd3dT", "hwd3dT1080", "framereadonly", "framenoconv":
//     "oldslice" exactly    CPU, 2 slice threads + LOW_DELAY (what the runtime ships with); anything else: 3 frame threads
//     hwd3d / hwcuda        GPU decode (D3D11VA / CUDA) and download to memory
//     T                     colour conversion on 4 threads (sws_scale_frame)    1080  convert to 1920x1080
//     readonly              only read packets (does the camera / network deliver 25/s?)
//     noconv                decode but skip the conversion
//     dump                  write frame 100 as raw BGRA (dump_<mode>.bgra) to compare colours between modes
//   Prints per 5 s: frames out per second, packets, send errors, packet-arrival -> converted-frame time.
//
// Build (VS 2022 x64 prompt, FFmpeg from Source/ThirdParty/FFmpeg):
//   cl /O2 /I<FFmpeg>\include rtsp_decode_test.c /link /LIBPATH:<FFmpeg>\lib\Win64 avformat.lib avcodec.lib avutil.lib swscale.lib
//   then copy <FFmpeg>in\Win64\*.dll next to the exe.
// Run with the url in an environment variable so the camera password never lands on a command line, e.g. in PowerShell:
//   $env:VT_URL = "rtsp://admin:" + [Uri]::EscapeDataString($env:OFFICE_CAM_PASSWORD) + "@192.168.229.2:554/LiveMedia/ch1/Media1?src=onvif"
//   .tsp_decode_test.exe VT_URL hwd3dT 20
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
static enum AVPixelFormat HwFmt = AV_PIX_FMT_NONE;
static enum AVPixelFormat PickHw(AVCodecContext* c, const enum AVPixelFormat* f) { for (; *f != AV_PIX_FMT_NONE; ++f) if (*f == HwFmt) return *f; printf("hw format not offered\n"); return AV_PIX_FMT_NONE; }

int main(int argc, char** argv)
{
    if (argc < 4) { printf("usage\n"); return 2; }
    const char* url = getenv(argv[1]);
    if (!url) { printf("env %s not set\n", argv[1]); return 2; }
    const char* mode = argv[2];
    double seconds = atof(argv[3]);
    int frameThreads = strcmp(mode, "oldslice") != 0;
    int resend = 1;
    int swsThreads = strstr(mode, "T") ? 4 : 1;
    int outW = strstr(mode, "1080") ? 1920 : 0, outH = strstr(mode, "1080") ? 1080 : 0;

    AVDictionary* o = NULL;
    av_dict_set(&o, "rtsp_transport", "tcp", 0);
    av_dict_set(&o, "fflags", "nobuffer", 0);
    av_dict_set(&o, "flags", "low_delay", 0);
    av_dict_set(&o, "max_delay", "0", 0);
    av_dict_set(&o, "reorder_queue_size", "0", 0);
    av_dict_set(&o, "probesize", "200000", 0);
    av_dict_set(&o, "analyzeduration", "500000", 0);
    av_dict_set(&o, "timeout", "5000000", 0);
    AVFormatContext* fmt = NULL;
    if (avformat_open_input(&fmt, url, NULL, &o) < 0) { printf("open failed\n"); return 1; }
    if (avformat_find_stream_info(fmt, NULL) < 0) { printf("no info\n"); return 1; }
    const AVCodec* dec = NULL;
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    AVCodecContext* c = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(c, fmt->streams[vs]->codecpar);
    if (frameThreads) { c->thread_type = FF_THREAD_FRAME; c->thread_count = 3; }
    else { c->flags |= AV_CODEC_FLAG_LOW_DELAY; c->thread_type = FF_THREAD_SLICE; c->thread_count = 2; }
    AVFrame* swf = av_frame_alloc();
    int hw = strstr(mode, "hw") != NULL;
    if (hw)
    {
        enum AVHWDeviceType t = strstr(mode, "cuda") ? AV_HWDEVICE_TYPE_CUDA : AV_HWDEVICE_TYPE_D3D11VA;
        for (int i = 0;; i++) { const AVCodecHWConfig* cf = avcodec_get_hw_config(dec, i); if (!cf) break; if ((cf->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cf->device_type == t) { HwFmt = cf->pix_fmt; break; } }
        AVBufferRef* dev = NULL;
        int r = av_hwdevice_ctx_create(&dev, t, NULL, NULL, 0);
        if (r < 0 || HwFmt == AV_PIX_FMT_NONE) { printf("hw device %s failed (%d)\n", av_hwdevice_get_type_name(t), r); return 1; }
        c->hw_device_ctx = av_buffer_ref(dev);
        c->get_format = PickHw;
        c->thread_count = 1; c->thread_type = 0;
        c->flags |= AV_CODEC_FLAG_LOW_DELAY;
        printf("hw device %s ok\n", av_hwdevice_get_type_name(t));
    }
    if (avcodec_open2(c, dec, NULL) < 0) { printf("codec open failed\n"); return 1; }
    printf("%s: %dx%d %s, threads %d active %s\n", mode, c->width, c->height, dec->name, c->thread_count,
        c->active_thread_type == FF_THREAD_FRAME ? "frame" : "slice");

    AVPacket* pkt = av_packet_alloc();
    AVFrame* pic = av_frame_alloc();
    struct SwsContext* sws = NULL;
    uint8_t* bgra = NULL;
    long long frames = 0, packets = 0, eagain = 0, senderr = 0, winFrames = 0;
    double t0 = av_gettime_relative() / 1e6, win = t0;
    double xferSum = 0; long long xferN = 0;
    double swsSum = 0; long long swsN = 0; long long lastPk = 0;
    double pktTime[64] = {0}; long long pktSeq = 0, frameSeq = 0; double lagSum = 0, lagMax = 0; long long lagN = 0;
    while (av_gettime_relative() / 1e6 - t0 < seconds)
    {
        if (av_read_frame(fmt, pkt) < 0) { printf("read ended\n"); break; }
        if (pkt->stream_index != vs) { av_packet_unref(pkt); continue; }
        packets++;
        pktTime[(pkt->pts / 3600) % 64] = av_gettime_relative() / 1e6; pktSeq++;
        if (strstr(mode, "readonly")) { av_packet_unref(pkt); double nw = av_gettime_relative() / 1e6; if (nw - win >= 5) { printf("  t=%4.0fs  %5.1f packets/s\n", nw - t0, (packets - lastPk) / (nw - win)); win = nw; lastPk = packets; } continue; }
        int sent = 0;
        for (int attempt = 0; attempt < (resend ? 8 : 1) && !sent; ++attempt)
        {
            int r = avcodec_send_packet(c, pkt);
            if (r == AVERROR(EAGAIN)) eagain++; else if (r < 0) senderr++;
            sent = r != AVERROR(EAGAIN);
            if (!resend && r < 0) break;   // the shipped code: no receive after a failed send
            while (avcodec_receive_frame(c, pic) == 0)
            {
                if (hw && pic->format == HwFmt) { double th0 = av_gettime_relative() / 1e6; if (av_hwframe_transfer_data(swf, pic, 0) < 0) { printf("transfer failed\n"); return 1; } av_frame_copy_props(swf, pic); av_frame_unref(pic); av_frame_move_ref(pic, swf); xferSum += av_gettime_relative() / 1e6 - th0; xferN++; }
                int w = pic->width, h = pic->height;
                int ow = outW ? outW : w, oh = outH ? outH : h;
                if (!sws)
                {
                    sws = sws_alloc_context();
                    av_opt_set_int(sws, "srcw", w, 0); av_opt_set_int(sws, "srch", h, 0); av_opt_set_int(sws, "src_format", pic->format, 0);
                    av_opt_set_int(sws, "dstw", ow, 0); av_opt_set_int(sws, "dsth", oh, 0); av_opt_set_int(sws, "dst_format", AV_PIX_FMT_BGRA, 0);
                    av_opt_set_int(sws, "sws_flags", SWS_BILINEAR, 0); av_opt_set_int(sws, "threads", swsThreads, 0); { int full = pic->color_range == AVCOL_RANGE_JPEG || pic->format == AV_PIX_FMT_YUVJ420P; av_opt_set_int(sws, "src_range", full, 0); av_opt_set_int(sws, "dst_range", 1, 0); printf("  src fmt %s range %d full %d\n", av_get_pix_fmt_name(pic->format), pic->color_range, full); }
                    if (sws_init_context(sws, NULL, NULL) < 0) { printf("sws init failed\n"); return 1; }
                    printf("  sws %dx%d -> %dx%d threads %d\n", w, h, ow, oh, swsThreads);
                }
                AVFrame* out = av_frame_alloc(); out->color_range = AVCOL_RANGE_JPEG; out->format = AV_PIX_FMT_BGRA; out->width = ow; out->height = oh; av_frame_get_buffer(out, 0);
                if (strstr(mode, "noconv")) { av_frame_unref(pic); frames++; winFrames++; continue; }
                double ts0 = av_gettime_relative() / 1e6;
                if (sws_scale_frame(sws, out, pic) < 0) { printf("scale failed\n"); return 1; }
                swsSum += av_gettime_relative() / 1e6 - ts0; swsN++;
                if (frames == 100) { char fn[64]; sprintf(fn, "dump_%s.bgra", mode); FILE* df = fopen(fn, "wb"); for (int y = 0; y < oh; y++) fwrite(out->data[0] + y * out->linesize[0], 1, ow * 4, df); fclose(df); printf("  dumped %s %dx%d\n", fn, ow, oh); }
                int64_t fpts = pic->pts;
                av_frame_free(&out);
                av_frame_unref(pic);
                double now = av_gettime_relative() / 1e6;
                // frames come out in packet order (no B-frames from these cameras): frame n belongs to packet n
                { double lag = now - pktTime[(fpts / 3600) % 64]; if (lag >= 0 && lag < 10) { lagSum += lag; lagN++; if (lag > lagMax) lagMax = lag; } }
                frameSeq++;
                frames++; winFrames++;
            }
        }
        av_packet_unref(pkt);
        double now = av_gettime_relative() / 1e6;
        if (now - win >= 5)
        {
            printf("  t=%4.0fs  %5.1f fps out  packets %lld frames %lld  eagain %lld senderr %lld  packet->frame avg %.0f ms max %.0f ms\n",
                now - t0, winFrames / (now - win), packets, frames, eagain, senderr, lagN ? lagSum / lagN * 1000 : 0, lagMax * 1000, swsN ? swsSum / swsN * 1000 : 0); swsSum = 0; swsN = 0;
            win = now; winFrames = 0; lagSum = 0; lagMax = 0; lagN = 0;
        }
    }
    avformat_close_input(&fmt);
    return 0;
}
