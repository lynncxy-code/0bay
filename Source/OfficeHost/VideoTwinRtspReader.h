#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include <atomic>

class FRunnableThread;

/**
 * Reads one rtsp:// camera stream with FFmpeg on its own thread and keeps the newest decoded picture (BGRA).
 *
 * Why this exists: the stock route (camera RTSP -> MediaMTX -> HLS -> Electra) put the projected video 3.6 s behind
 * the camera on site (2026-10-15, measured against a direct RTSP read on the same machine and clock). HLS has to fill
 * a whole segment before anything leaves the converter, and Electra plays three segments behind the newest one, with
 * no setting for HLS that changes it. Here nothing is buffered on purpose: the newest picture replaces the previous
 * one, and the game thread uploads whatever is newest when it ticks.
 *
 * The thread reconnects by itself (every 2 s after a failure), so the owner only has to look at how old the newest
 * picture is. Credentials in the address are never written to the log (see MaskUrl).
 */
class FVideoTwinRtspReader : public FRunnable
{
public:
    /** @param InUrl full address, credentials included; @param InName short label for the log (camera id). */
    FVideoTwinRtspReader(const FString& InUrl, const FString& InName);
    virtual ~FVideoTwinRtspReader() override;

    /** Start the reading thread. False when the FFmpeg libraries could not be loaded (see GetLastError). */
    bool Start();
    /** Ask the thread to finish and wait for it. Blocking reads are interrupted, so this returns quickly. */
    void Shutdown();

    /** Copies the newest picture into Out if it is newer than InOutSeq (and updates InOutSeq). Game thread. */
    bool TakeFrame(TArray<uint8>& Out, int32& OutWidth, int32& OutHeight, uint64& InOutSeq);
    /** Seconds since the newest picture was decoded; a large number when there has never been one. */
    double SecondsSinceLastFrame() const;
    /** Pictures decoded per second over the last couple of seconds (for the status line). */
    float GetRecentFps() const;
    FString GetLastError() const;

    /** user:password@ in an address replaced by user:***@, for logging. */
    static FString MaskUrl(const FString& Url);
    /** Load the FFmpeg DLLs from the project's Binaries folder once per process. */
    static bool EnsureLibrariesLoaded(FString& OutError);

    // FRunnable
    virtual uint32 Run() override;
    virtual void Stop() override { bStopRequested = true; }

private:
    /** One connection: open, decode until the stream ends or fails. Returns when it is over. */
    void ReadOnce();
    void SetError(const FString& Error);
    static int InterruptCallback(void* Opaque);

    FString Url;
    FString Name;
    FRunnableThread* Thread = nullptr;
    std::atomic<bool> bStopRequested { false };
    /** FPlatformTime::Seconds() after which a blocking network call is abandoned (stalled camera, cable pulled). */
    std::atomic<double> IoDeadline { 0.0 };

    mutable FCriticalSection Lock;
    TArray<uint8> Frame;
    int32 FrameWidth = 0;
    int32 FrameHeight = 0;
    uint64 FrameSeq = 0;
    double LastFrameTime = -1.0;
    double FpsWindowStart = 0.0;
    int32 FpsWindowCount = 0;
    float RecentFps = 0.f;
    FString LastError;
};
