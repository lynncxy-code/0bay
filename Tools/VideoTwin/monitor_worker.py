"""MediaMTX runOnDemand worker. Reads a local RTSP proxy, never a credentialed URL."""
import os
import re
import subprocess
import sys


def command(path, ffmpeg, mode):
    if not re.fullmatch(r"cam(?:1[7-9]|2[0-9]|30)(?:_sub)?(?:_vp9)?", path):
        raise ValueError("Invalid monitor stream")
    raw = path.removesuffix("_vp9").replace("cam", "raw", 1).replace('_sub', 'sub')
    args = [ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error", "-rtsp_transport", "tcp",
            "-timeout", "15000000", "-i", f"rtsp://127.0.0.1:19554/{raw}",
            "-map", "0:v:0", "-an"]
    if path.endswith("_vp9"):
        sub = "_sub" in path
        fps, width, bitrate = ("10", "640", "500k") if sub else ("15", "1920", "2500k")
        args += ["-c:v", "libvpx-vp9", "-deadline", "realtime", "-cpu-used", "8",
                 "-row-mt", "1", "-threads", "2", "-pix_fmt", "yuv420p", "-r", fps,
                 "-vf", f"scale=w='min({width},iw)':h=-2", "-g", fps, "-keyint_min", fps,
                 "-b:v", bitrate, "-crf", "32", "-strict", "experimental"]
    elif mode == "copy":
        args += ["-c:v", "copy"]
    else:
        fps = "15" if path.endswith("_sub") else "25"
        args += ["-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency",
                 "-profile:v", "baseline", "-pix_fmt", "yuv420p", "-r", fps,
                 "-g", fps, "-keyint_min", fps, "-sc_threshold", "0", "-bf", "0",
                 "-crf", "23", "-threads", "2"]
    return args + ["-f", "rtsp", "-rtsp_transport", "tcp", f"rtsp://127.0.0.1:19554/{path}"]


if __name__ == "__main__":
    args = command(sys.argv[1], os.environ["MONITOR_FFMPEG"], os.environ.get("MONITOR_VIDEO_MODE", "h264"))
    # MediaMTX kills the command process group when the last reader leaves.
    sys.exit(subprocess.call(args))
