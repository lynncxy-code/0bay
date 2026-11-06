"""Isolated synthetic integration test. Never reads camera passwords or contacts cameras."""
import json
import argparse
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

from flask import Flask, jsonify
from werkzeug.serving import make_server
from monitor_gateway import config_text

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / 'Saved/MonitorGateway/smoke'
PLATFORM = Path('D:/tmp/digital_twin_aircraft-prod/01-source')
FFMPEG = 'D:/AI/Python310/Lib/site-packages/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe'
sys.path.insert(0, str(PLATFORM / 'backend'))
from monitor_streams import monitor_streams


def kill(process):
    if process and process.poll() is None:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cef', action='store_true', help='Also test an existing isolated UE CEF session on port 9228')
    options = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('MTX_') and k != 'OFFICE_CAM_PASSWORD'}
    env['MONITOR_FFMPEG'] = FFMPEG
    env['MONITOR_VIDEO_MODE'] = 'h264'
    if options.cef:
        env['MONITOR_TEST_CEF'] = '9228'
    text = config_text(sys.executable, HERE / 'monitor_worker.py')
    text = text.replace('rtsp://127.0.0.1:1/unconfigured', 'rtsp://127.0.0.1:19554/synthetic')
    text += '  synthetic:\n    source: publisher\n'
    config = OUT / 'synthetic.yml'
    config.write_text(text, encoding='utf-8')
    app = Flask(__name__, static_folder=str(PLATFORM / 'frontend'), static_url_path='')
    app.register_blueprint(monitor_streams)
    publisher = [None]
    log = (OUT / 'services.log').open('w', encoding='utf-8')

    def start_signal():
        publisher[0] = subprocess.Popen([FFMPEG, '-hide_banner', '-loglevel', 'error', '-re',
            '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=15', '-an', '-c:v', 'libx264',
            '-preset', 'ultrafast', '-tune', 'zerolatency', '-pix_fmt', 'yuv420p', '-g', '15',
            '-f', 'rtsp', '-rtsp_transport', 'tcp', 'rtsp://127.0.0.1:19554/synthetic'],
            env=env, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)

    @app.post('/__test__/signal/<action>')
    def signal(action):
        if action == 'stop':
            kill(publisher[0])
        elif action == 'start':
            if publisher[0] is None or publisher[0].poll() is not None:
                start_signal()
        return jsonify(ok=True)

    mtx = subprocess.Popen([str(ROOT / 'Saved/VideoTwinMVP/live/mediamtx.exe'), str(config)],
                           env=env, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    server = None
    try:
        time.sleep(1)
        if mtx.poll() is not None:
            raise RuntimeError('MediaMTX test config did not start; see services.log')
        start_signal()
        docker_check = subprocess.run(['docker', 'exec', 'digital_twin_aircraft-backend-1', 'python', '-c',
            "import requests; s=requests.Session(); s.trust_env=False; "
            "r=s.get('http://host.docker.internal:8898/cam17/index.m3u8?cookieCheck=1',timeout=25); "
            "assert r.status_code == 200, r.status_code; print('PASS Docker to host HLS')"],
            capture_output=True, text=True)
        if docker_check.returncode:
            raise RuntimeError('Docker cannot reach the loopback HLS listener: ' + docker_check.stderr[-400:])
        print(docker_check.stdout, flush=True)
        server = make_server('127.0.0.1', 5018, app, threaded=True)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        result = subprocess.run(['node', str(HERE / 'smoke_monitor.cjs')], env=env)
        if result.returncode:
            raise RuntimeError('Browser smoke test failed')
    finally:
        if server:
            server.shutdown()
        kill(publisher[0])
        kill(mtx)
        log.close()


if __name__ == '__main__':
    main()
