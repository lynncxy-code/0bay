"""Windows Chinese TTS. Writes last_tts.wav for UE to fetch."""

from __future__ import annotations

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TTS_WAV = ROOT / "last_tts.wav"
TTS_TEXT = ROOT / "tts_input.txt"
TTS_PS1 = ROOT / "_speak.ps1"

_PS = r"""
Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$zh = $synth.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo } |
    Where-Object { $_.Culture.Name -like 'zh*' } | Select-Object -First 1
if ($zh) { $synth.SelectVoice($zh.Name) }
$out = $args[0]
$textPath = $args[1]
$text = Get-Content -Raw -Encoding UTF8 $textPath
if (-not $text) { throw 'empty tts text' }
$synth.Rate = 0
$synth.SetOutputToWaveFile($out)
$synth.Speak($text)
$synth.Dispose()
"""


def status() -> dict:
    voices = list_voices()
    zh = [name for name, culture in voices if culture.lower().startswith("zh")]
    return {
        "tts_wav": str(TTS_WAV) if TTS_WAV.exists() else "",
        "tts_voice_zh": zh[0] if zh else "",
        "tts_warning": "" if zh else "未安装中文语音包，将用系统默认声音朗读。",
        "tts_voices": [f"{name} ({culture})" for name, culture in voices[:8]],
    }


def list_voices() -> list[tuple[str, str]]:
    script = (
        "Add-Type -AssemblyName System.Speech; "
        "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$s.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name + '|' + $_.VoiceInfo.Culture.Name }"
    )
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", script],
        capture_output=True,
        text=True,
        timeout=20,
        check=False,
    )
    voices: list[tuple[str, str]] = []
    for line in result.stdout.splitlines():
        if "|" not in line:
            continue
        name, culture = line.strip().split("|", 1)
        voices.append((name, culture))
    return voices


def synthesize(text: str) -> Path:
    speak = (text or "").strip()
    if not speak:
        raise RuntimeError("empty_tts_text")
    if len(speak) > 800:
        speak = speak[:800]
    TTS_TEXT.write_text(speak, encoding="utf-8")
    TTS_PS1.write_text(_PS, encoding="utf-8")
    if TTS_WAV.exists():
        TTS_WAV.unlink()
    result = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(TTS_PS1),
            str(TTS_WAV),
            str(TTS_TEXT),
        ],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    if result.returncode != 0 or not TTS_WAV.exists() or TTS_WAV.stat().st_size < 44:
        err = (result.stderr or result.stdout or "tts_failed").strip()
        raise RuntimeError(err[-500:])
    return TTS_WAV
