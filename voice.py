"""Voice pipeline — STT + TTS for Stack-chan conversation."""

import asyncio
import base64
import io
import json
import logging
import os
import struct
import subprocess
import tempfile
import wave
from typing import Optional, Callable, Awaitable

import httpx

logger = logging.getLogger(__name__)

# ── Configuration defaults ────────────────────────────────────
XAI_STT_URL = "https://api.x.ai/v1/stt"
XAI_TTS_URL = "https://api.x.ai/v1/audio/speech"
OPENAI_TTS_URL = "https://api.openai.com/v1/audio/speech"

# Audio format
SAMPLE_RATE = 16000
BITS = 16
CHANNELS = 1


class VoicePipeline:
    """Speech-to-text and text-to-speech pipeline."""

    def __init__(
        self,
        stt_provider: str = "xai",
        tts_provider: str = "xai",
        xai_api_key: str = "",
        openai_api_key: str = "",
        language: str = "zh",
        on_transcript: Optional[Callable[[str], Awaitable[None]]] = None,
    ):
        self.stt_provider = stt_provider
        self.tts_provider = tts_provider
        self.xai_api_key = xai_api_key or os.environ.get("XAI_API_KEY", "")
        self.openai_api_key = openai_api_key or os.environ.get("OPENAI_API_KEY", "")
        self.language = language
        self.on_transcript = on_transcript
        self._audio_buffer = bytearray()

    # ── STT ────────────────────────────────────────────────────

    async def transcribe(self, audio_data: bytes) -> str:
        """Transcribe audio bytes to text."""
        if self.stt_provider == "xai":
            return await self._transcribe_xai(audio_data)
        elif self.stt_provider == "openai":
            return await self._transcribe_openai(audio_data)
        else:
            return "[STT not configured]"

    async def _transcribe_xai(self, audio_data: bytes) -> str:
        """Grok xAI STT."""
        if not self.xai_api_key:
            return "[STT: No xAI API key]"

        # Ensure WAV format
        wav_data = self._ensure_wav(audio_data)

        async with httpx.AsyncClient(timeout=30) as client:
            try:
                resp = await client.post(
                    XAI_STT_URL,
                    headers={"Authorization": f"Bearer {self.xai_api_key}"},
                    files={"file": ("audio.wav", wav_data, "audio/wav")},
                    data={"language": self.language, "format": "true"},
                )
                if resp.status_code == 200:
                    return resp.json().get("text", "")
                else:
                    logger.error("xAI STT error: %s", resp.text[:200])
                    return f"[STT error: {resp.status_code}]"
            except Exception as e:
                logger.error("xAI STT exception: %s", e)
                return "[STT error]"

    async def _transcribe_openai(self, audio_data: bytes) -> str:
        """OpenAI Whisper STT."""
        if not self.openai_api_key:
            return "[STT: No OpenAI key]"

        wav_data = self._ensure_wav(audio_data)
        async with httpx.AsyncClient(timeout=30) as client:
            try:
                resp = await client.post(
                    "https://api.openai.com/v1/audio/transcriptions",
                    headers={"Authorization": f"Bearer {self.openai_api_key}"},
                    files={"file": ("audio.wav", wav_data, "audio/wav")},
                    data={"model": "whisper-1", "language": self.language},
                )
                if resp.status_code == 200:
                    return resp.json().get("text", "")
                else:
                    logger.error("OpenAI STT error: %s", resp.text[:200])
                    return f"[STT error: {resp.status_code}]"
            except Exception as e:
                logger.error("OpenAI STT exception: %s", e)
                return "[STT error]"

    # ── TTS ────────────────────────────────────────────────────

    async def synthesize(self, text: str) -> Optional[bytes]:
        """Convert text to speech audio bytes (PCM 16kHz)."""
        if self.tts_provider == "xai":
            return await self._synthesize_xai(text)
        elif self.tts_provider == "openai":
            return await self._synthesize_openai(text)
        else:
            logger.warning("No TTS provider configured")
            return None

    async def _synthesize_xai(self, text: str) -> Optional[bytes]:
        if not self.xai_api_key:
            return None
        async with httpx.AsyncClient(timeout=30) as client:
            try:
                resp = await client.post(
                    XAI_TTS_URL,
                    headers={"Authorization": f"Bearer {self.xai_api_key}"},
                    json={"model": "grok-tts", "input": text, "voice": "coral", "response_format": "wav"},
                )
                if resp.status_code == 200:
                    return self._ensure_pcm(resp.content)
            except Exception as e:
                logger.error("xAI TTS error: %s", e)
        return None

    async def _synthesize_openai(self, text: str) -> Optional[bytes]:
        if not self.openai_api_key:
            return None
        async with httpx.AsyncClient(timeout=30) as client:
            try:
                resp = await client.post(
                    OPENAI_TTS_URL,
                    headers={"Authorization": f"Bearer {self.openai_api_key}"},
                    json={"model": "tts-1", "input": text, "voice": "nova", "response_format": "pcm"},
                )
                if resp.status_code == 200:
                    return resp.content
            except Exception as e:
                logger.error("OpenAI TTS error: %s", e)
        return None

    # ── Audio playback ─────────────────────────────────────────

    async def play_audio(self, pcm_data: bytes, sample_rate: int = SAMPLE_RATE):
        """Play PCM audio data through default system output."""
        if not pcm_data:
            return

        # Try various audio players
        for player in ["aplay", "paplay", "ffplay", "sox"]:
            if subprocess.run(["which", player], capture_output=True).returncode == 0:
                try:
                    if player == "aplay":
                        proc = await asyncio.create_subprocess_exec(
                            "aplay", "-r", str(sample_rate), "-f", "S16_LE", "-c", "1",
                            stdin=subprocess.PIPE,
                        )
                    elif player == "paplay":
                        proc = await asyncio.create_subprocess_exec(
                            "paplay", "--raw", f"--rate={sample_rate}",
                            f"--channels=1", "--format=s16le",
                            stdin=subprocess.PIPE,
                        )
                    else:
                        continue  # skip others for now
                    proc.stdin.write(pcm_data)
                    await proc.stdin.drain()
                    proc.stdin.close()
                    await proc.wait()
                    return
                except Exception as e:
                    logger.debug("Audio player %s failed: %s", player, e)
                    continue

        logger.warning("No audio player found. Install aplay (alsa-utils) or paplay (pulseaudio-utils).")

    # ── Helpers ────────────────────────────────────────────────

    def feed_mic_data(self, pcm_chunk: bytes):
        """Accumulate microphone PCM data for later transcription."""
        self._audio_buffer.extend(pcm_chunk)
        # Keep max 30 seconds
        max_bytes = SAMPLE_RATE * BITS // 8 * CHANNELS * 30
        if len(self._audio_buffer) > max_bytes:
            self._audio_buffer = self._audio_buffer[-max_bytes:]

    async def flush_and_transcribe(self) -> str:
        """Transcribe accumulated audio and clear buffer."""
        if len(self._audio_buffer) < SAMPLE_RATE * 2:  # less than 2 seconds
            return ""
        data = bytes(self._audio_buffer)
        self._audio_buffer.clear()
        return await self.transcribe(data)

    def _ensure_wav(self, audio_data: bytes) -> bytes:
        """Wrap raw PCM in WAV container if needed."""
        if audio_data[:4] == b"RIFF":
            return audio_data
        buf = io.BytesIO()
        with wave.open(buf, "wb") as w:
            w.setnchannels(CHANNELS)
            w.setsampwidth(BITS // 8)
            w.setframerate(SAMPLE_RATE)
            w.writeframes(audio_data)
        return buf.getvalue()

    def _ensure_pcm(self, wav_data: bytes) -> bytes:
        """Extract PCM from WAV if needed."""
        if wav_data[:4] != b"RIFF":
            return wav_data
        try:
            with wave.open(io.BytesIO(wav_data), "rb") as w:
                return w.readframes(w.getnframes())
        except Exception:
            return wav_data
