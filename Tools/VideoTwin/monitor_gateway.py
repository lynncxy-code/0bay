"""On-demand 14-channel monitor gateway; independent of the existing fusion service.

Passwords are passed to MediaMTX through child-process environment overrides only.
The generated YAML, worker arguments and filtered logs contain no credentials.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from urllib.parse import quote

HERE = Path(__file__).resolve().parent


def config_text(python, worker, listen="127.0.0.1:8898"):
    lines = ["logLevel: info", "api: no", "metrics: no", "pprof: no", "playback: no",
             "rtmp: no", "webrtc: no", "srt: no", "moq: no", "rtsp: yes",
             "rtspAddress: 127.0.0.1:19554", "rtspTransports: [tcp]",
             "hls: yes", f"hlsAddress: {listen}", "hlsVariant: fmp4",
             "hlsAlwaysRemux: no", "hlsSegmentDuration: 1s", "hlsSegmentCount: 7",
             "hlsMuxerCloseAfter: 10s", "paths:"]
    for channel in range(17, 31):
        for suffix in ("", "_sub"):
            # Placeholder source is replaced by MTX_PATHS_RAW..._SOURCE in memory.
            # MediaMTX env parsing uses underscores as separators; source path names must not contain them.
            raw, output = f"raw{channel}{suffix.replace('_', '')}", f"cam{channel}{suffix}"
            lines.extend([f"  {raw}:", "    source: rtsp://127.0.0.1:1/unconfigured",
                          "    rtspTransport: tcp", "    sourceOnDemand: yes",
                          "    sourceOnDemandStartTimeout: 20s", "    sourceOnDemandCloseAfter: 10s"])
            for codec_suffix in ("", "_vp9"):
                target = output + codec_suffix
                command = f'"{python}" "{worker}" {target}'
                lines.extend([f"  {target}:", "    source: publisher",
                              f"    runOnDemand: {json.dumps(command)}", "    runOnDemandRestart: yes",
                              "    runOnDemandStartTimeout: 30s", "    runOnDemandCloseAfter: 10s"])
    return "\n".join(lines) + "\n"


def find_tool(name, supplied):
    if supplied:
        path = Path(supplied)
        if path.is_file():
            return str(path.resolve())
        raise ValueError(f"{name} executable not found")
    candidates = [HERE / f"{name}.exe"]
    if name == "mediamtx":
        candidates.append(HERE.parents[1] / "Saved/VideoTwinMVP/live/mediamtx.exe")
    for path in candidates:
        if path.is_file():
            return str(path)
    found = shutil.which(name)
    if found:
        return found
    raise ValueError(f"Set --{name} to an installed executable")


def source_overrides(password, host, port, username):
    result = {}
    for channel in range(17, 31):
        for subtype, suffix in ((0, ""), (1, "_sub")):
            result[f"MTX_PATHS_RAW{channel}{suffix.replace('_', '').upper()}_SOURCE"] = (
                f"rtsp://{quote(username, safe='')}:{quote(password, safe='')}@{host}:{port}"
                f"/rtsp/streaming?channel={channel}&subtype={subtype}")
    return result


def redact(line, password):
    line = re.sub(r"rtsp://[^\s@]+@", "rtsp://<redacted>@", line)
    if password:
        for value in (quote(password, safe=""), password):
            line = line.replace(value, "<redacted>")
    return line


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mediamtx")
    parser.add_argument("--ffmpeg")
    parser.add_argument("--listen", default="127.0.0.1:8898")
    parser.add_argument("--host", default="192.168.229.16")
    parser.add_argument("--port", type=int, default=443)
    parser.add_argument("--username", default="admin")
    parser.add_argument("--camera-interface", default="", help="Windows interface for camera traffic; auto-loads the existing VideoTwin network setting")
    parser.add_argument("--video-mode", choices=("h264", "copy"), default="h264")
    parser.add_argument("--output", type=Path, default=HERE.parents[1] / "Saved/MonitorGateway")
    parser.add_argument("--validate", action="store_true", help="Check tools and write credential-free config only")
    args = parser.parse_args()
    mediamtx = find_tool("mediamtx", args.mediamtx)
    ffmpeg = find_tool("ffmpeg", args.ffmpeg)
    args.output.mkdir(parents=True, exist_ok=True)
    config = args.output.resolve() / "mediamtx.monitor.yml"
    config.write_text(config_text(sys.executable, HERE / "monitor_worker.py", args.listen), encoding="utf-8")
    if args.validate:
        print("Validated: 14 channels, 28 sources, 56 on-demand H264/VP9 profiles; credential-free configuration written.")
        return
    password = os.environ.get("OFFICE_CAM_PASSWORD")
    if not password:
        raise ValueError("OFFICE_CAM_PASSWORD is not set; no camera connection was attempted")
    # Do not inherit old MediaMTX overrides from the separate fusion service.
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("MTX_")}
    relay = None
    interface = args.camera_interface
    network_config = HERE.parents[1] / 'Saved/VideoTwinMVP/live/network.json'
    if not interface and network_config.is_file():
        interface = json.loads(network_config.read_text(encoding='utf-8-sig')).get('camera_interface', '')
    source_host, source_port = args.host, args.port
    if interface and os.name == 'nt':
        lookup_env = dict(env, MONITOR_CAMERA_INTERFACE=interface)
        script = "$a=Get-NetIPConfiguration -InterfaceAlias $env:MONITOR_CAMERA_INTERFACE -ErrorAction Stop; @{index=$a.InterfaceIndex;ip=@($a.IPv4Address)[0].IPAddress}|ConvertTo-Json -Compress"
        adapter = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command', script], env=lookup_env, text=True))
        routes = args.output.resolve() / 'camera-relay.json'
        routes.write_text(json.dumps([{'host': args.host, 'port': args.port, 'local_port': 19555}]), encoding='utf-8')
        relay = subprocess.Popen([sys.executable, str(HERE / 'camera_tcp_relay.py'), '--routes', str(routes),
                                  '--interface-index', str(adapter['index']), '--source-ip', adapter['ip'], '--workers', '64'],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        time.sleep(0.5)
        if relay.poll() is not None:
            raise ValueError('Camera interface relay failed to start')
        source_host, source_port = '127.0.0.1', 19555
    env.update(source_overrides(password, source_host, source_port, args.username))
    env["MONITOR_FFMPEG"] = ffmpeg
    env["MONITOR_VIDEO_MODE"] = args.video_mode
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    print(f"Monitor gateway starting: 14 channels; HLS {args.listen}; mode {args.video_mode}", flush=True)
    # MediaMTX owns source and worker restarts; exit if startup fails (e.g. port conflict).
    with (args.output / "gateway.log").open("a", encoding="utf-8") as log:
        process = subprocess.Popen([mediamtx, str(config)], env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                   errors="replace", creationflags=flags)
        try:
            for line in process.stdout:
                safe = redact(line, password)
                log.write(safe)
                log.flush()
            code = process.wait()
            if code:
                raise ValueError("Media gateway stopped; inspect the filtered gateway.log")
        finally:
            if process.poll() is None:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                else:
                    process.terminate()
                process.wait(timeout=10)
            if relay is not None:
                relay.terminate()
                relay.wait(timeout=10)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as exc:
        print(f"Monitor gateway: {exc}", file=sys.stderr)
        sys.exit(1)
