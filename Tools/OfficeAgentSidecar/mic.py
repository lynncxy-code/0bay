"""WASAPI capture for PICO Connect virtual mic. Keep the device open for the whole session."""

from __future__ import annotations

import threading
from typing import Any

import numpy as np

try:
    import sounddevice as sd
except Exception as exc:  # pragma: no cover
    sd = None
    _import_error = str(exc)
else:
    _import_error = ""

PICO_HINTS = ("pico", "picostreaming", "pico streaming")
TARGET_RATE = 16000

_lock = threading.Lock()
_stream = None
_device_index: int | None = None
_device_name = ""
_device_kind = ""
_rate = TARGET_RATE
_recording = False
_chunks: list[np.ndarray] = []
_open_error = ""


def _input_devices() -> list[dict[str, Any]]:
    if sd is None:
        return []
    out = []
    for index, device in enumerate(sd.query_devices()):
        if int(device.get("max_input_channels") or 0) < 1:
            continue
        out.append({"index": index, "name": str(device.get("name") or "")})
    return out


def _pico_score(name: str) -> int:
    compact = name.lower().replace(" ", "")
    if "picostreamingmicrophone" in compact:
        return 3
    if "pico" in compact and ("mic" in compact or "麦克风" in name):
        return 2
    if "pico" in compact:
        return 1
    return 0


def pick_input_device() -> tuple[int, str, str]:
    devices = _input_devices()
    ranked = sorted(devices, key=lambda item: _pico_score(item["name"]), reverse=True)
    if ranked and _pico_score(ranked[0]["name"]) > 0:
        item = ranked[0]
        return item["index"], item["name"], "pico"
    if sd is None:
        raise RuntimeError(_import_error or "sounddevice_missing")
    default = sd.default.device[0] if sd.default.device is not None else None
    if isinstance(default, (list, tuple)):
        default = default[0]
    if default is not None and int(default) >= 0:
        info = sd.query_devices(int(default))
        return int(default), str(info.get("name") or f"device {default}"), "fallback"
    if devices:
        item = devices[0]
        return item["index"], item["name"], "fallback"
    raise RuntimeError("no_input_device")


def _callback(indata, frames, time_info, status) -> None:  # noqa: ARG001
    with _lock:
        if not _recording:
            return
        _chunks.append(np.array(indata[:, 0], dtype=np.float32, copy=True))


def status() -> dict[str, Any]:
    with _lock:
        return {
            "mic_open": _stream is not None,
            "mic_device": _device_name,
            "mic_kind": _device_kind,
            "mic_rate": _rate,
            "listening": _recording,
            "mic_error": _open_error or _import_error,
            "input_devices": [item["name"] for item in _input_devices()[:12]],
        }


def open_capture() -> dict[str, Any]:
    global _stream, _device_index, _device_name, _device_kind, _rate, _open_error
    if sd is None:
        _open_error = _import_error or "sounddevice_missing"
        return status()
    with _lock:
        if _stream is not None:
            return status()
    try:
        index, name, kind = pick_input_device()
        info = sd.query_devices(index)
        rate = int(float(info.get("default_samplerate") or TARGET_RATE))
        stream = sd.InputStream(
            device=index,
            channels=1,
            samplerate=rate,
            dtype="float32",
            callback=_callback,
        )
        stream.start()
        with _lock:
            _stream = stream
            _device_index = index
            _device_name = name
            _device_kind = kind
            _rate = rate
            _open_error = ""
        return status()
    except Exception as exc:
        _open_error = str(exc)
        return status()


def close_capture() -> None:
    global _stream, _recording, _chunks
    stream = None
    with _lock:
        stream = _stream
        _stream = None
        _recording = False
        _chunks = []
    if stream is not None:
        try:
            stream.stop()
            stream.close()
        except Exception:
            pass


def start_utterance() -> dict[str, Any]:
    global _chunks, _recording
    info = open_capture()
    with _lock:
        if _stream is None:
            return info
        _chunks = []
        _recording = True
    return status()


def _resample(pcm: np.ndarray, src_rate: int) -> np.ndarray:
    if src_rate == TARGET_RATE or pcm.size == 0:
        return pcm.astype(np.float32)
    n = max(1, int(round(pcm.size * TARGET_RATE / src_rate)))
    x = np.linspace(0.0, 1.0, pcm.size, endpoint=False)
    xi = np.linspace(0.0, 1.0, n, endpoint=False)
    return np.interp(xi, x, pcm.astype(np.float32)).astype(np.float32)


def stop_utterance() -> tuple[np.ndarray | None, dict[str, Any]]:
    global _recording, _chunks
    with _lock:
        _recording = False
        chunks = list(_chunks)
        _chunks = []
        rate = _rate
    if not chunks:
        return None, status()
    pcm = np.concatenate(chunks)
    peak = float(np.max(np.abs(pcm))) if pcm.size else 0.0
    info = status()
    info["peak"] = peak
    info["seconds"] = float(pcm.size / max(rate, 1))
    return _resample(pcm, rate), info
