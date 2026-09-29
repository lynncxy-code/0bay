"""D2 probe: TTS -> ASR -> (optional) agent reply. No Unreal."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

import asr  # noqa: E402
import mic  # noqa: E402
import tts  # noqa: E402


def main() -> int:
    print("=== D2 mic ===")
    print("pick", mic.pick_input_device())
    opened = mic.open_capture()
    print("open", {k: opened[k] for k in ("mic_open", "mic_device", "mic_kind", "mic_error")})
    mic.close_capture()

    print("=== D2 tts voices ===")
    print(json.dumps(tts.status(), ensure_ascii=False))
    phrase = "你好，这里是办公室展厅。"
    wav = tts.synthesize(phrase)
    print("tts_wav", wav, "bytes", wav.stat().st_size)

    print("=== D2 asr on tts wav ===")
    text = asr.transcribe_wav(wav)
    print("transcript", text)
    print("asr", asr.status())
    ok = bool(text)
    print("D2_RESULT", "PASS" if ok else "FAIL_ASR_EMPTY")
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
