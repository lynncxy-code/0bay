"""Chinese ASR on CPU. Prefer SenseVoice-Small; fall back to faster-whisper."""

from __future__ import annotations

import os
import re
import threading
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
MODEL_DIR = ROOT / "models"
WAV_PATH = ROOT / "last_utterance.wav"
TARGET_RATE = 16000

_lock = threading.Lock()
_model = None
_backend = "none"
_error = ""
_loading = False


def status() -> dict:
    return {
        "asr_backend": _backend,
        "asr_ready": _model is not None,
        "asr_error": _error,
    }


def warmup() -> None:
    threading.Thread(target=_load, daemon=True, name="asr-warmup").start()


def _strip_sensevoice(text: str) -> str:
    return re.sub(r"<\|[^>]*\|>", "", text or "").strip()


def _write_wav(path: Path, pcm: np.ndarray) -> None:
    samples = np.clip(pcm, -1.0, 1.0)
    data = (samples * 32767.0).astype(np.int16)
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(TARGET_RATE)
        handle.writeframes(data.tobytes())


def _load() -> None:
    global _model, _backend, _error, _loading
    with _lock:
        if _model is not None or _loading:
            return
        _loading = True
        _backend = "loading"
    preferred = os.environ.get("OFFICEVR_ASR", "auto").strip().lower() or "auto"
    errors: list[str] = []
    if preferred in ("auto", "sensevoice"):
        try:
            from funasr import AutoModel

            model = AutoModel(model="iic/SenseVoiceSmall", device="cpu", disable_update=True)
            with _lock:
                _model = ("sensevoice", model)
                _backend = "sensevoice-small-cpu"
                _loading = False
                _error = ""
            return
        except Exception as exc:
            errors.append(f"sensevoice:{exc}")
            if preferred == "sensevoice":
                with _lock:
                    _backend = "failed"
                    _error = str(exc)
                    _loading = False
                return
    try:
        from faster_whisper import WhisperModel

        MODEL_DIR.mkdir(parents=True, exist_ok=True)
        name = os.environ.get("OFFICEVR_WHISPER_MODEL", "small").strip() or "small"
        model = WhisperModel(
            name,
            device="cpu",
            compute_type="int8",
            download_root=str(MODEL_DIR),
        )
        with _lock:
            _model = ("whisper", model)
            _backend = f"faster-whisper-{name}-cpu"
            _loading = False
            _error = ""
        return
    except Exception as exc:
        errors.append(f"whisper:{exc}")
        with _lock:
            _backend = "failed"
            _error = " ; ".join(errors)
            _loading = False


def transcribe_wav(path: Path) -> str:
    with wave.open(str(path), "rb") as handle:
        rate = handle.getframerate()
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        frames = handle.readframes(handle.getnframes())
    if width != 2:
        raise RuntimeError(f"unsupported_wav_width_{width}")
    pcm = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
    if channels > 1:
        pcm = pcm.reshape(-1, channels).mean(axis=1)
    if rate != TARGET_RATE:
        n = max(1, int(round(pcm.size * TARGET_RATE / rate)))
        x = np.linspace(0.0, 1.0, pcm.size, endpoint=False)
        xi = np.linspace(0.0, 1.0, n, endpoint=False)
        pcm = np.interp(xi, x, pcm).astype(np.float32)
    return transcribe(pcm)


def transcribe(pcm_16k: np.ndarray) -> str:
    if pcm_16k is None or pcm_16k.size < int(TARGET_RATE * 0.3):
        return ""
    _load()
    with _lock:
        packed = _model
        if packed is None:
            raise RuntimeError(_error or "asr_not_ready")
        kind, model = packed
    if kind == "sensevoice":
        _write_wav(WAV_PATH, pcm_16k)
        result = model.generate(input=str(WAV_PATH), language="zh", use_itn=True)
        text = ""
        if result:
            first = result[0]
            if isinstance(first, dict):
                text = str(first.get("text") or "")
            else:
                text = str(first)
        return _strip_sensevoice(text)
    segments, _info = model.transcribe(pcm_16k, language="zh", beam_size=1, vad_filter=True)
    return "".join(segment.text for segment in segments).strip()
