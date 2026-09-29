"""Office VR text sidecar.

Default brain is the Lingyunzhi DSH public session-gateway
(login cookie + /LingYZBE/dsh/api/...). Cursor Cloud Agents is a test switch
only (OFFICEVR_BRAIN=cursor). Unreal talks only to this sidecar.
"""

from __future__ import annotations

import json
import os
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import httpx

import asr
import mic
import tts

ROOT = Path(__file__).resolve().parent
CURSOR_API = "https://api.cursor.com"
KNOWN_AGENTS = ("office.agent.0", "office.agent.1")


def load_env() -> None:
    path = ROOT / ".env"
    if not path.exists():
        return
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip())


load_env()

HOST = os.environ.get("SIDECAR_HOST", "127.0.0.1")
PORT = int(os.environ.get("SIDECAR_PORT", "8091"))
BRAIN = os.environ.get("OFFICEVR_BRAIN", "dsh").strip().lower() or "dsh"

DSH_ORIGIN = os.environ.get("OFFICEVR_DSH_ORIGIN", "http://127.0.0.1:5173").rstrip("/")
DSH_USER = os.environ.get("OFFICEVR_DSH_USER", "admin").strip()
DSH_PASSWORD = os.environ.get("OFFICEVR_DSH_PASSWORD", "")
DSH_STABLE = {
    "office.agent.0": os.environ.get("OFFICEVR_DSH_STABLE_0", "").strip(),
    "office.agent.1": os.environ.get("OFFICEVR_DSH_STABLE_1", "").strip(),
}

MODEL = os.environ.get("CURSOR_MODEL", "composer-2.5")
API_KEY = os.environ.get("CURSOR_API_KEY", "")

CURSOR_PROMPTS = {
    "office.agent.0": (
        "你是 VR 办公室展厅里的 Agent 0。"
        "用简体中文简短回答访客。"
        "不要改仓库、不要跑命令、不要编造职位或姓名。"
        "不要给自己编职位或姓名，自称 Agent 0 即可。"
    ),
    "office.agent.1": (
        "你是 VR 办公室展厅里的 Agent 1。"
        "用简体中文简短回答访客。"
        "不要改仓库、不要跑命令、不要编造职位或姓名。"
        "不要给自己编职位或姓名，自称 Agent 1 即可。"
    ),
}

_lock = threading.Lock()
_active_id: str | None = None
_busy = False
_dsh_session_id: str | None = None
_cloud_id: str | None = None
_dsh_client: httpx.Client | None = None


def json_bytes(payload: dict, status: int = 200) -> tuple[int, bytes]:
    return status, json.dumps(payload, ensure_ascii=False).encode("utf-8")


def dsh_origin_header() -> str:
    parsed = urlparse(DSH_ORIGIN)
    if not parsed.scheme or not parsed.netloc:
        return DSH_ORIGIN
    return f"{parsed.scheme}://{parsed.netloc}"


def dsh_http() -> httpx.Client:
    global _dsh_client
    if _dsh_client is None:
        _dsh_client = httpx.Client(
            base_url=DSH_ORIGIN,
            timeout=httpx.Timeout(180.0),
            trust_env=False,
            follow_redirects=False,
            headers={
                "Content-Type": "application/json",
                "Origin": dsh_origin_header(),
                "Accept": "application/json",
            },
        )
    return _dsh_client


def close_dsh_http() -> None:
    global _dsh_client
    if _dsh_client is not None:
        _dsh_client.close()
        _dsh_client = None


def dsh_login(force: bool = False) -> None:
    client = dsh_http()
    if not force and client.cookies:
        return
    if not DSH_PASSWORD:
        raise RuntimeError("missing_dsh_password")
    response = client.post(
        "/LingYZBE/admin/login",
        json={"username": DSH_USER, "password": DSH_PASSWORD},
    )
    if response.status_code >= 400:
        raise RuntimeError(f"dsh_login_failed {response.status_code}")


def dsh_post(path: str, payload: dict, retry_login: bool = True) -> httpx.Response:
    dsh_login()
    client = dsh_http()
    response = client.post(path, json=payload)
    if response.status_code in (401, 403) and retry_login:
        dsh_login(force=True)
        response = client.post(path, json=payload)
    return response


def dsh_rpc(method: str, args: dict) -> dict:
    rpc_id = str(uuid.uuid4())
    response = dsh_post(
        f"/LingYZBE/dsh/api/{method}",
        {
            "type": "client-request",
            "rpcId": rpc_id,
            "method": method,
            "payload": {"args": args},
        },
    )
    if response.status_code >= 400:
        raise RuntimeError(f"dsh_rpc_http {method} {response.status_code} {response.text[:500]}")
    body = response.json()
    result = body.get("result") if isinstance(body, dict) else None
    if not isinstance(result, dict) or result.get("ok") is not True:
        error = result.get("error") if isinstance(result, dict) else None
        raise RuntimeError(f"dsh_rpc_failed {method} {error}")
    value = result.get("value")
    return value if isinstance(value, dict) else {}


def collect_text_deltas(node: Any, out: list[str]) -> None:
    if isinstance(node, dict):
        kind = node.get("type")
        text = node.get("text")
        if kind == "text-delta" and isinstance(text, str) and text:
            out.append(text)
        for value in node.values():
            collect_text_deltas(value, out)
    elif isinstance(node, list):
        for item in node:
            collect_text_deltas(item, out)


def wait_dsh_idle(session_id: str, deadline: float) -> dict:
    last: dict = {}
    while time.time() < deadline:
        listing = dsh_rpc("session/list", {"_request": {}})
        items = listing.get("items") if isinstance(listing, dict) else None
        if isinstance(items, list):
            for raw in items:
                if not isinstance(raw, dict):
                    continue
                if str(raw.get("sessionId") or "") != session_id:
                    continue
                last = raw
                if raw.get("running") is False:
                    return raw
        time.sleep(1.0)
    return last


def read_dsh_reply(session_id: str) -> str:
    page = dsh_rpc(
        "session/page",
        {"request": {"address": {"kind": "session", "sessionId": session_id}}},
    )
    chunks: list[str] = []
    collect_text_deltas(page, chunks)
    reply = "".join(chunks).strip()
    if reply:
        return reply
    return ""


def cursor_client() -> httpx.Client:
    return httpx.Client(
        base_url=CURSOR_API,
        auth=(API_KEY, ""),
        timeout=httpx.Timeout(180.0),
        trust_env=False,
        headers={"Content-Type": "application/json"},
    )


def poll_cursor_run(client: httpx.Client, agent_id: str, run_id: str, deadline: float) -> dict:
    path = f"/v1/agents/{agent_id}/runs/{run_id}"
    last: dict = {}
    while time.time() < deadline:
        response = client.get(path)
        if response.status_code >= 400:
            raise RuntimeError(f"run {response.status_code}: {response.text[:500]}")
        last = response.json()
        status = str(last.get("status") or "")
        if status in {"FINISHED", "ERROR", "CANCELLED", "EXPIRED"}:
            return last
        time.sleep(2.0)
    raise RuntimeError(f"run timeout status={last.get('status')}")


def begin_dsh(agent_id: str) -> tuple[int, bytes]:
    global _active_id, _dsh_session_id
    if agent_id not in KNOWN_AGENTS:
        return json_bytes({"ok": False, "error": "unknown_agent"}, 400)
    stable_id = DSH_STABLE.get(agent_id, "")
    if not stable_id:
        return json_bytes(
            {
                "ok": False,
                "error": "missing_stable_id",
                "agent_id": agent_id,
                "hint": "填写 OFFICEVR_DSH_STABLE_0 / OFFICEVR_DSH_STABLE_1，不要用占位 id",
            },
            409,
        )
    with _lock:
        if _busy:
            return json_bytes({"ok": False, "error": "busy", "active_agent_id": _active_id}, 409)
        if _active_id and _active_id != agent_id:
            return json_bytes({"ok": False, "error": "busy", "active_agent_id": _active_id}, 409)
        if _active_id == agent_id and _dsh_session_id:
            return json_bytes(
                {
                    "ok": True,
                    "agent_id": agent_id,
                    "resumed": True,
                    "brain": "dsh",
                    "session_id": _dsh_session_id,
                }
            )
        _active_id = agent_id
    try:
        response = dsh_post(
            "/LingYZBE/dsh/api/lingyunzhi/session/business-agent",
            {"stable_id": stable_id},
        )
        if response.status_code >= 400:
            with _lock:
                if _dsh_session_id is None:
                    _active_id = None
            return json_bytes(
                {
                    "ok": False,
                    "error": "dsh_create_failed",
                    "detail": f"{response.status_code} {response.text[:800]}",
                },
                502,
            )
        body = response.json()
        session_id = str(body.get("session_id") or body.get("sessionId") or "")
        if not session_id:
            with _lock:
                if _dsh_session_id is None:
                    _active_id = None
            return json_bytes(
                {"ok": False, "error": "dsh_create_failed", "detail": str(body)[:800]},
                502,
            )
        with _lock:
            _dsh_session_id = session_id
        return json_bytes(
            {
                "ok": True,
                "agent_id": agent_id,
                "brain": "dsh",
                "session_id": session_id,
                "runtime": "dsh-session-gateway",
                "hint": "会话已开始。编辑器控制台输入 officevr.Say 你好",
            }
        )
    except Exception as exc:
        with _lock:
            if _dsh_session_id is None:
                _active_id = None
        return json_bytes({"ok": False, "error": "dsh_begin_failed", "detail": str(exc)}, 502)


def prompt_dsh(text: str) -> tuple[int, bytes]:
    global _busy
    text = (text or "").strip()
    if not text:
        return json_bytes({"ok": False, "error": "empty_text"}, 400)
    with _lock:
        if _busy:
            return json_bytes({"ok": False, "error": "busy", "active_agent_id": _active_id}, 409)
        if not _active_id or not _dsh_session_id:
            return json_bytes({"ok": False, "error": "no_session"}, 409)
        agent_id = _active_id
        session_id = _dsh_session_id
        _busy = True
    try:
        dsh_rpc(
            "session/prompt",
            {
                "request": {
                    "sessionId": session_id,
                    "content": [{"type": "text", "text": text}],
                }
            },
        )
        last = wait_dsh_idle(session_id, time.time() + 170)
        reply = read_dsh_reply(session_id)
        if not reply:
            reply = "DSH 本轮已结束，但 session/page 里没有 text-delta。"
        return json_bytes(
            {
                "ok": True,
                "agent_id": agent_id,
                "reply": reply,
                "brain": "dsh",
                "session_id": session_id,
                "running": last.get("running"),
                "runtime": "dsh-session-gateway",
            }
        )
    except Exception as exc:
        return json_bytes({"ok": False, "error": "dsh_prompt_failed", "detail": str(exc)}, 502)
    finally:
        with _lock:
            _busy = False


def end_dsh() -> tuple[int, bytes]:
    global _active_id, _dsh_session_id
    session_id = None
    with _lock:
        session_id = _dsh_session_id
        _active_id = None
        _dsh_session_id = None
    if session_id:
        try:
            dsh_rpc("session/cancel", {"request": {"sessionId": session_id}})
        except Exception:
            pass
    close_dsh_http()
    return json_bytes({"ok": True, "brain": "dsh"})


def begin_cursor(agent_id: str) -> tuple[int, bytes]:
    global _active_id, _cloud_id, _busy
    if agent_id not in CURSOR_PROMPTS:
        return json_bytes({"ok": False, "error": "unknown_agent"}, 400)
    if not API_KEY:
        return json_bytes({"ok": False, "error": "missing_cursor_api_key"}, 500)
    with _lock:
        if _busy:
            return json_bytes({"ok": False, "error": "busy", "active_agent_id": _active_id}, 409)
        if _active_id and _active_id != agent_id:
            return json_bytes({"ok": False, "error": "busy", "active_agent_id": _active_id}, 409)
        _active_id = agent_id
        if _active_id == agent_id and _cloud_id:
            return json_bytes(
                {
                    "ok": True,
                    "agent_id": agent_id,
                    "resumed": True,
                    "model": MODEL,
                    "brain": "cursor",
                }
            )
    return json_bytes(
        {
            "ok": True,
            "agent_id": agent_id,
            "model": MODEL,
            "brain": "cursor",
            "runtime": "cursor-cloud",
            "hint": "会话已开始。编辑器控制台输入 officevr.Say 你好",
        }
    )


def prompt_cursor(text: str) -> tuple[int, bytes]:
    global _busy, _cloud_id
    text = (text or "").strip()
    if not text:
        return json_bytes({"ok": False, "error": "empty_text"}, 400)
    with _lock:
        if _busy:
            return json_bytes({"ok": False, "error": "busy", "active_agent_id": _active_id}, 409)
        if not _active_id:
            return json_bytes({"ok": False, "error": "no_session"}, 409)
        agent_id = _active_id
        cloud_id = _cloud_id
        _busy = True
    try:
        with cursor_client() as client:
            if not cloud_id:
                payload = {
                    "prompt": {"text": f"{CURSOR_PROMPTS[agent_id]}\n\n访客说：{text}"},
                    "model": {"id": MODEL},
                    "name": agent_id,
                }
                response = client.post("/v1/agents", json=payload)
                if response.status_code >= 400:
                    return json_bytes(
                        {
                            "ok": False,
                            "error": "cursor_create_failed",
                            "detail": f"{response.status_code} {response.text[:800]}",
                        },
                        502,
                    )
                created = response.json()
                cloud_id = str(created.get("id") or created.get("agent", {}).get("id") or "")
                run_id = str(created.get("latestRunId") or created.get("run", {}).get("id") or "")
                if not cloud_id:
                    return json_bytes(
                        {"ok": False, "error": "cursor_create_failed", "detail": str(created)[:800]},
                        502,
                    )
                with _lock:
                    _cloud_id = cloud_id
            else:
                response = client.post(
                    f"/v1/agents/{cloud_id}/runs",
                    json={"prompt": {"text": text}},
                )
                if response.status_code >= 400:
                    return json_bytes(
                        {
                            "ok": False,
                            "error": "cursor_followup_failed",
                            "detail": f"{response.status_code} {response.text[:800]}",
                        },
                        502,
                    )
                run = response.json()
                run_id = str(run.get("id") or run.get("run", {}).get("id") or "")
                if not run_id:
                    agent = client.get(f"/v1/agents/{cloud_id}").json()
                    run_id = str(agent.get("latestRunId") or "")
            if not run_id:
                agent = client.get(f"/v1/agents/{cloud_id}").json()
                run_id = str(agent.get("latestRunId") or "")
            finished = poll_cursor_run(client, cloud_id, run_id, time.time() + 170)
            reply = str(finished.get("result") or "").strip()
            status = str(finished.get("status") or "")
            if status != "FINISHED" and not reply:
                reply = f"Agent 状态 {status}"
            return json_bytes(
                {
                    "ok": True,
                    "agent_id": agent_id,
                    "reply": reply,
                    "status": status,
                    "brain": "cursor",
                    "runtime": "cursor-cloud",
                }
            )
    except Exception as exc:
        return json_bytes({"ok": False, "error": "prompt_failed", "detail": str(exc)}, 502)
    finally:
        with _lock:
            _busy = False


def end_cursor() -> tuple[int, bytes]:
    global _active_id, _cloud_id
    cloud_id = None
    with _lock:
        cloud_id = _cloud_id
        _active_id = None
        _cloud_id = None
    if cloud_id:
        try:
            with cursor_client() as client:
                client.delete(f"/v1/agents/{cloud_id}")
        except Exception:
            pass
    return json_bytes({"ok": True, "brain": "cursor"})


def attach_fields(status: int, body: bytes, extra: dict) -> tuple[int, bytes]:
    try:
        payload = json.loads(body.decode("utf-8"))
    except Exception:
        return status, body
    if not isinstance(payload, dict):
        return status, body
    payload.update(extra)
    return json_bytes(payload, status)


def begin_session(agent_id: str) -> tuple[int, bytes]:
    if BRAIN == "cursor":
        status, body = begin_cursor(agent_id)
    else:
        status, body = begin_dsh(agent_id)
    if status < 300:
        mic.open_capture()
        return attach_fields(
            status,
            body,
            {
                **mic.status(),
                **asr.status(),
                "hint": "请一直按住左手 Y，直到面板出现「正在听」。若接通前已松开，请再按住一次。B 结束会话。",
            },
        )
    return status, body


def prompt_session(text: str) -> tuple[int, bytes]:
    if BRAIN == "cursor":
        status, body = prompt_cursor(text)
    else:
        status, body = prompt_dsh(text)
    extra: dict = {}
    if status < 300:
        try:
            payload = json.loads(body.decode("utf-8"))
            reply = str(payload.get("reply") or "") if isinstance(payload, dict) else ""
        except Exception:
            reply = ""
        if reply:
            try:
                tts.synthesize(reply)
                extra["tts_url"] = "/v1/tts/last.wav"
            except Exception as exc:
                extra["tts_url"] = ""
                extra["tts_error"] = str(exc)[-300:]
    extra.update(tts.status())
    return attach_fields(status, body, extra)


def end_session() -> tuple[int, bytes]:
    mic.close_capture()
    if BRAIN == "cursor":
        return end_cursor()
    return end_dsh()


def listen_start() -> tuple[int, bytes]:
    if not _active_id:
        return json_bytes({"ok": False, "error": "no_session"}, 409)
    if _busy:
        return json_bytes({"ok": False, "error": "busy", "active_agent_id": _active_id}, 409)
    info = mic.start_utterance()
    if not info.get("mic_open"):
        return json_bytes({"ok": False, "error": "mic_unavailable", **info}, 503)
    return json_bytes({"ok": True, "listening": True, "agent_id": _active_id, **info})


def listen_stop() -> tuple[int, bytes]:
    pcm, info = mic.stop_utterance()
    seconds = float(info.get("seconds") or 0.0)
    if pcm is None or seconds < 0.35:
        return json_bytes(
            {
                "ok": False,
                "error": "too_short",
                "transcript": "",
                "hint": "按住 Y 再说长一点",
                **info,
            },
            400,
        )
    try:
        transcript = asr.transcribe(pcm)
    except Exception as exc:
        return json_bytes(
            {"ok": False, "error": "asr_failed", "detail": str(exc), "transcript": "", **asr.status(), **info},
            502,
        )
    transcript = (transcript or "").strip()
    if not transcript:
        return json_bytes(
            {
                "ok": False,
                "error": "empty_transcript",
                "transcript": "",
                "hint": "没听清，请再按住 Y 说一次",
                **asr.status(),
                **info,
            },
            400,
        )
    status, body = prompt_session(transcript)
    return attach_fields(status, body, {"transcript": transcript, **asr.status(), **info})


def dsh_origin_reachable() -> bool:
    try:
        with httpx.Client(timeout=2.0, trust_env=False, follow_redirects=False) as client:
            response = client.get(DSH_ORIGIN + "/")
            return response.status_code < 500
    except Exception:
        return False


def health_payload() -> dict:
    payload = {
        "ok": True,
        "service": "office-agent-sidecar",
        "brain": BRAIN,
        "dsh_origin": DSH_ORIGIN,
        "dsh_origin_reachable": dsh_origin_reachable() if BRAIN == "dsh" else None,
        "has_dsh_password": bool(DSH_PASSWORD),
        "has_stable_0": bool(DSH_STABLE["office.agent.0"]),
        "has_stable_1": bool(DSH_STABLE["office.agent.1"]),
        "model": MODEL,
        "has_cursor_key": bool(API_KEY),
        "active_agent_id": _active_id,
        "listen": f"http://{HOST}:{PORT}",
    }
    payload.update(mic.status())
    payload.update(asr.status())
    payload.update(tts.status())
    return payload


def yn(flag: bool | None) -> str:
    if flag is True:
        return "是"
    if flag is False:
        return "否"
    return "—"


def html_status() -> bytes:
    info = health_payload()
    rows = (
        ("服务", "运行中"),
        ("大脑", str(info["brain"])),
        ("DSH 入口", str(info["dsh_origin"])),
        ("DSH 入口可达", yn(info["dsh_origin_reachable"])),
        ("已填 DSH 密码", yn(info["has_dsh_password"])),
        ("已填 stable_id 0", yn(info["has_stable_0"])),
        ("已填 stable_id 1", yn(info["has_stable_1"])),
        ("Cursor 测试钥", yn(info["has_cursor_key"])),
        ("当前会话", str(info["active_agent_id"] or "无")),
        ("录音设备", str(info.get("mic_device") or info.get("mic_error") or "未打开")),
        ("麦类型", str(info.get("mic_kind") or "—")),
        ("ASR", str(info.get("asr_backend") or "none")),
        ("中文 TTS", str(info.get("tts_voice_zh") or "未装中文语音包")),
        ("JSON 探活", "/healthz"),
    )
    body = "".join(
        f"<tr><th>{label}</th><td>{value}</td></tr>" for label, value in rows
    )
    page = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta http-equiv="refresh" content="5">
<title>Office VR Sidecar</title>
<style>
body {{ font-family: Segoe UI, sans-serif; background:#111; color:#eee; margin:2rem; }}
a {{ color:#9cf; }}
th {{ text-align:left; padding-right:1.5rem; color:#aaa; font-weight:500; }}
td {{ padding:0.25rem 0; }}
.note {{ color:#aaa; margin-top:1.5rem; max-width:40rem; line-height:1.5; }}
</style>
</head>
<body>
<h1>Office VR Sidecar</h1>
<p>UE 只打这个地址。正式大脑是 DSH；Cursor 仅测试。</p>
<table>{body}</table>
<p class="note">对准展厅人，点 Y 开会话，按住 Y 说话，松开后对话框出字。一键启动：StartOfficeVR.bat。测试大脑：OFFICEVR_BRAIN=cursor。</p>
</body>
</html>
"""
    return page.encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        print("%s - %s" % (self.address_string(), fmt % args), flush=True)

    def _request_path(self) -> str:
        return (self.path or "/").split("?", 1)[0]

    def _send(self, status: int, body: bytes, content_type: str = "application/json; charset=utf-8") -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length") or "0")
        raw = self.rfile.read(length) if length else b"{}"
        if not raw:
            return {}
        return json.loads(raw.decode("utf-8"))

    def do_GET(self) -> None:
        path = self._request_path()
        if path in ("/", ""):
            self._send(200, html_status(), "text/html; charset=utf-8")
            return
        if path == "/favicon.ico":
            self._send(204, b"", "image/x-icon")
            return
        if path in ("/healthz", "/v1/health"):
            self._send(*json_bytes(health_payload()))
            return
        if path == "/v1/tts/last.wav":
            wav = tts.TTS_WAV
            if not wav.exists():
                self._send(*json_bytes({"ok": False, "error": "no_tts"}, 404))
                return
            self._send(200, wav.read_bytes(), "audio/wav")
            return
        self._send(*json_bytes({"ok": False, "error": "not_found"}, 404))

    def do_POST(self) -> None:
        try:
            payload = self._read_json()
        except json.JSONDecodeError:
            self._send(*json_bytes({"ok": False, "error": "bad_json"}, 400))
            return
        path = self._request_path()
        if path in ("/v1/session/begin", "/v1/begin"):
            self._send(*begin_session(str(payload.get("agent_id") or "")))
            return
        if path in ("/v1/session/prompt", "/v1/prompt"):
            self._send(*prompt_session(str(payload.get("text") or "")))
            return
        if path in ("/v1/session/listen/start", "/v1/listen/start"):
            self._send(*listen_start())
            return
        if path in ("/v1/session/listen/stop", "/v1/listen/stop"):
            self._send(*listen_stop())
            return
        if path in ("/v1/session/end", "/v1/end"):
            self._send(*end_session())
            return
        self._send(*json_bytes({"ok": False, "error": "not_found"}, 404))


def main() -> None:
    asr.warmup()
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(
        f"office agent sidecar on http://{HOST}:{PORT} brain={BRAIN} origin={DSH_ORIGIN}",
        flush=True,
    )
    server.serve_forever()


if __name__ == "__main__":
    main()
