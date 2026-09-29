"""One-click Office VR process stack: sidecar, Lingyunzhi containers, Vite, editor."""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIDECAR = ROOT / "Tools" / "OfficeAgentSidecar"
FRONTEND = ROOT / "ontology-online-tool" / "frontend"
EDITOR_BAT = ROOT / "StartEditor.bat"
MAGPIE_DIR = ROOT / "Tools" / "Magpie"
MAGPIE_EXE = MAGPIE_DIR / "Magpie.exe"
SIDECAR_HOST = "127.0.0.1"
SIDECAR_PORT = 8095
VITE_PORT = 5173
DOCKER_CONTAINERS = ("docker-postgres-1", "lingyz-api", "lingyz-dsh")
CREATE_NEW_CONSOLE = 0x00000010


def load_sidecar_port() -> int:
    env_path = SIDECAR / ".env"
    if not env_path.exists():
        return SIDECAR_PORT
    for raw in env_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("SIDECAR_PORT="):
            try:
                return int(line.split("=", 1)[1].strip() or SIDECAR_PORT)
            except ValueError:
                return SIDECAR_PORT
    return SIDECAR_PORT


def port_open(port: int, host: str = "127.0.0.1") -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.4):
            return True
    except OSError:
        return False


def pids_on_port(port: int) -> list[int]:
    command = (
        f"(Get-NetTCPConnection -LocalPort {port} -State Listen "
        f"-ErrorAction SilentlyContinue).OwningProcess"
    )
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", command],
        capture_output=True,
        text=True,
        check=False,
    )
    pids: list[int] = []
    for token in result.stdout.replace(",", " ").split():
        try:
            pid = int(token)
        except ValueError:
            continue
        if pid not in pids:
            pids.append(pid)
    return pids


def kill_port(port: int) -> None:
    for pid in pids_on_port(port):
        subprocess.run(
            ["taskkill", "/F", "/PID", str(pid)],
            capture_output=True,
            check=False,
        )


def wait_port(port: int, seconds: float, host: str = "127.0.0.1") -> bool:
    deadline = time.time() + seconds
    while time.time() < deadline:
        if port_open(port, host):
            return True
        time.sleep(0.25)
    return False


def sidecar_python() -> Path:
    venv_py = SIDECAR / ".venv" / "Scripts" / "python.exe"
    if venv_py.exists():
        return venv_py
    return Path(sys.executable)


def ensure_sidecar_venv() -> Path:
    venv_py = SIDECAR / ".venv" / "Scripts" / "python.exe"
    if not venv_py.exists():
        subprocess.run([sys.executable, "-m", "venv", str(SIDECAR / ".venv")], check=True)
    subprocess.run(
        [str(venv_py), "-m", "pip", "install", "-q", "-r", str(SIDECAR / "requirements.txt")],
        check=True,
        cwd=str(SIDECAR),
    )
    return venv_py


def start_sidecar(port: int) -> None:
    if port_open(port):
        kill_port(port)
        time.sleep(0.4)
    python = ensure_sidecar_venv()
    env = os.environ.copy()
    env["PYTHONUNBUFFERED"] = "1"
    subprocess.Popen(
        [str(python), str(SIDECAR / "server.py")],
        cwd=str(SIDECAR),
        env=env,
        creationflags=CREATE_NEW_CONSOLE,
    )
    if not wait_port(port, 8):
        raise RuntimeError(f"sidecar 没有在 {port} 起来")


def start_docker() -> list[str]:
    started: list[str] = []
    docker = subprocess.run(["docker", "version"], capture_output=True, check=False)
    if docker.returncode != 0:
        print("Docker 不可用，跳过凌云智容器")
        return started
    listed = subprocess.run(
        ["docker", "ps", "-a", "--format", "{{.Names}}"],
        capture_output=True,
        text=True,
        check=False,
    )
    names = {line.strip() for line in listed.stdout.splitlines() if line.strip()}
    for name in DOCKER_CONTAINERS:
        if name not in names:
            print(f"没有容器 {name}，跳过")
            continue
        result = subprocess.run(["docker", "start", name], capture_output=True, text=True, check=False)
        if result.returncode == 0:
            started.append(name)
            print(f"已启动 {name}")
        else:
            print(f"{name} 启动失败: {(result.stderr or result.stdout).strip()[:300]}")
    return started


def start_vite() -> None:
    if port_open(VITE_PORT):
        print(f"Vite 已在 {VITE_PORT}")
        return
    if not (FRONTEND / "package.json").exists():
        print("没有 ontology-online-tool/frontend，跳过 Vite")
        return
    npm = "npm.cmd" if os.name == "nt" else "npm"
    subprocess.Popen(
        [npm, "run", "dev", "--", "--host", "127.0.0.1", "--port", str(VITE_PORT)],
        cwd=str(FRONTEND),
        creationflags=CREATE_NEW_CONSOLE,
    )
    if wait_port(VITE_PORT, 20):
        print(f"Vite 已在 {VITE_PORT}")
    else:
        print("Vite 还在启动，浏览器稍后打开 http://127.0.0.1:5173")


def start_magpie() -> None:
    if not MAGPIE_EXE.exists():
        print("没有 Magpie，跳过旁观放大")
        return
    probe = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq Magpie.exe"],
        capture_output=True,
        text=True,
        check=False,
    )
    if "Magpie.exe" in probe.stdout:
        print("Magpie 已在运行")
        return
    subprocess.Popen(
        [str(MAGPIE_EXE), "-t"],
        cwd=str(MAGPIE_DIR),
        creationflags=CREATE_NEW_CONSOLE,
    )
    print("已启动 Magpie。旁观窗口在前台时按 Ctrl+Alt+Shift+M。")


def start_editor() -> None:
    probe = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe"],
        capture_output=True,
        text=True,
        check=False,
    )
    if "UnrealEditor.exe" in probe.stdout:
        print("Unreal 已在运行")
        return
    if not EDITOR_BAT.exists():
        print("没有 StartEditor.bat")
        return
    subprocess.Popen(["cmd.exe", "/c", str(EDITOR_BAT)], cwd=str(ROOT), creationflags=CREATE_NEW_CONSOLE)
    print("已启动 Unreal 编辑器")


def open_status(port: int) -> None:
    url = f"http://{SIDECAR_HOST}:{port}/"
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            body = response.read(80).decode("utf-8", "replace")
            if "Office VR Sidecar" not in body and '"ok"' not in body:
                print("状态页异常:", body[:80])
    except Exception as exc:
        print("状态页还没就绪:", exc)
    os.startfile(url)  # type: ignore[attr-defined]


def main() -> int:
    port = load_sidecar_port()
    print(f"Office VR 一键启动  sidecar=http://{SIDECAR_HOST}:{port}")
    start_docker()
    start_vite()
    start_sidecar(port)
    start_editor()
    start_magpie()
    open_status(port)
    print("完成。以后只跑仓库根目录 StartOfficeVR.bat。")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print("启动失败:", exc)
        raise SystemExit(1)
