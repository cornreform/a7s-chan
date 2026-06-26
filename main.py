"""A7S-chan — Main entry point. Async event loop for Stack-chan robot brain."""

import asyncio
import logging
import os
import signal
import sys
import time

import yaml

from stackchan_proto import StackChanProtocol
from expressions.pack import EXPRESSIONS, EXPRESSION_MAP, get as get_expr
from tools.ir_remote import get_all as ir_get, list_devices as ir_devices
from tools import weather as weather_tool
from tools import screen_display as screen_tool
from vision import NPUVision
from voice import VoicePipeline
from agent import Agent
from behavior import BehaviorMachine, State

logger = logging.getLogger("a7s_chan")

# ── Config ────────────────────────────────────────────────────

DEFAULT_CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config.yaml")


def load_config(path: str = DEFAULT_CONFIG_PATH) -> dict:
    if os.path.exists(path):
        with open(path) as f:
            return yaml.safe_load(f) or {}
    return {}


# ── Main Application ──────────────────────────────────────────

class StackChanApp:
    """Main application — coordinates vision, agent, voice, behavior, and hardware."""

    def __init__(self, config: dict):
        self.config = config
        self.running = True

        # Shared state (updated by vision, read by behavior/servo)
        self.face_x = 0.5
        self.face_y = 0.5
        self.has_face = False
        self.vision_fps = 0.0
        self.last_voice_text = ""
        self.pending_weather: dict = {}
        self._shutdown_event = asyncio.Event()

        # Components
        self.stackchan = StackChanProtocol(
            remote_addr=config.get("stackchan", {}).get("ip", "192.168.1.100"),
        )
        self.vision = NPUVision(
            enabled=config.get("vision", {}).get("enabled", True),
        )
        self.voice = VoicePipeline(
            xai_api_key=config.get("voice", {}).get("xai_api_key", ""),
            openai_api_key=config.get("voice", {}).get("openai_api_key", ""),
            language=config.get("voice", {}).get("language", "zh"),
            on_transcript=self._on_transcript,
        )
        self.agent = Agent(
            api_key=config.get("agent", {}).get("api_key", ""),
            model=config.get("agent", {}).get("model", "deepseek-v4-flash"),
            base_url=config.get("agent", {}).get("base_url", "https://api.deepseek.com/v1"),
        )

        self.behavior = BehaviorMachine(
            on_enter_state=self._on_enter_state,
            on_idle_tick=self._on_idle_tick,
            on_track_tick=self._on_track_tick,
        )

        # Expression tracking
        self._current_expr = "idle"
        self._anim_tick = 0.0

        # Audio buffer for continuous mic input
        self._mic_buffer = bytearray()

    # ── Lifecycle ──────────────────────────────────────────────

    async def start(self):
        """Initialize all components and start main loop."""
        logger.info("Starting A7S-chan...")

        # Init UDP
        await self.stackchan.start(status_callback=self._on_stackchan_status)

        # Register agent tools
        await self.agent.register_builtin_tools(
            self.stackchan, EXPRESSIONS, ir_get, weather_tool, screen_tool,
        )

        # Set initial expression
        await self.stackchan.set_expression("idle")
        await self.stackchan.set_led(255, 255, 255)

        logger.info("A7S-chan ready! 🎉")
        await self._main_loop()

    async def stop(self):
        """Graceful shutdown."""
        logger.info("Shutting down...")
        self.running = False
        self.stackchan.stop()
        self._shutdown_event.set()

    # ── Main loop (30fps target) ───────────────────────────────

    async def _main_loop(self):
        """Core loop: vision → behavior → servo commands."""
        frame_interval = 1.0 / 30.0
        voice_check_interval = 0.1  # 10Hz voice check
        last_voice_check = 0.0

        while self.running:
            loop_start = time.monotonic()

            # 1. Vision inference
            vision_result = await self.vision.detect()
            self.has_face = vision_result.has_face
            if vision_result.has_face:
                self.face_x = vision_result.face_center_x
                self.face_y = vision_result.face_center_y
            self.vision_fps = vision_result.fps

            # 2. Behavior tick
            await self.behavior.tick(self.has_face)

            # 3. Face tracking servo (if tracking)
            if self.behavior.state == State.TRACKING and self.has_face:
                target_pan = int(self.face_x * 360)
                target_tilt = int(85 - self.face_y * 80)  # invert Y
                target_tilt = max(5, min(85, target_tilt))
                await self.stackchan.set_servo(0, target_pan, 0.3, 200)
                await self.stackchan.set_servo(1, target_tilt, 0.3, 200)

            # 4. Check voice (every 100ms)
            now = loop_start
            if now - last_voice_check > voice_check_interval:
                last_voice_check = now
                if self._mic_buffer and self.behavior.should_process_voice():
                    self.voice.feed_mic_data(bytes(self._mic_buffer))
                    self._mic_buffer.clear()
                    text = await self.voice.flush_and_transcribe()
                    if text.strip():
                        self.last_voice_text = text
                        self.behavior.on_user_spoke()
                        await self._handle_voice_command(text)

            # 5. Maintain frame rate
            elapsed = time.monotonic() - loop_start
            sleep_time = max(0, frame_interval - elapsed)
            if sleep_time > 0:
                await asyncio.sleep(sleep_time)

    # ── Voice handler ──────────────────────────────────────────

    async def _handle_voice_command(self, text: str):
        """Process transcribed voice command through agent."""
        logger.info("Voice: %s", text)

        # Build context
        context_parts = [
            f"Current expression: {self._current_expr}",
            f"Face detected: {self.has_face}",
            f"Face position: ({self.face_x:.2f}, {self.face_y:.2f})",
        ]

        await self.stackchan.set_expression("think")

        response = await self.agent.chat(text, context="\n".join(context_parts))

        # TTS response
        if response and not response.startswith("["):
            audio = await self.voice.synthesize(response)
            if audio:
                await self.stackchan.set_expression("talk")
                await self.voice.play_audio(audio)
            await self.stackchan.set_expression("happy")
        else:
            await self.stackchan.set_expression("confused")

    # ── Callbacks ──────────────────────────────────────────────

    async def _on_transcript(self, text: str):
        """Called when voice STT produces text (from voice.py callback)."""
        logger.debug("Transcript: %s", text)

    async def _on_stackchan_status(self, msg: dict):
        """Handle status messages from Stack-chan."""
        if "event" in msg:
            if msg["event"] == "touch":
                self.behavior.on_touch(msg.get("zone", 1))
            elif msg["event"] == "mic_data":
                import base64
                pcm = base64.b64decode(msg.get("pcm", ""))
                self._mic_buffer.extend(pcm)
        elif "battery" in msg:
            logger.debug("Battery: %d%%", msg["battery"])

    async def _on_enter_state(self, state: State):
        """Handle state transitions."""
        expr_map = {
            State.IDLE: "idle",
            State.TRACKING: "happy",
            State.CONVERSE: "listen",
            State.AUTONOMOUS: "happy",
            State.EXECUTE: "think",
            State.SLEEP: "sleep",
        }
        expr = expr_map.get(state, "idle")
        await self.stackchan.set_expression(expr)
        self._current_expr = expr

    async def _on_idle_tick(self):
        """Idle behavior — look around, blink."""
        import math
        t = time.monotonic()
        pan = 180 + int(30 * math.sin(t * 0.5))
        await self.stackchan.set_servo(0, pan, 0.1, 500)
        await self.stackchan.set_expression("blink")

    async def _on_track_tick(self):
        """Tracking behavior — head follows face."""
        # Face-following happens in main loop
        pass


# ── Entry ─────────────────────────────────────────────────────

def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
    )

    config_path = os.environ.get("A7S_CONFIG", DEFAULT_CONFIG_PATH)
    config = load_config(config_path)

    app = StackChanApp(config)

    async def run():
        await app.start()

    def shutdown():
        asyncio.ensure_future(app.stop())

    loop = asyncio.get_event_loop()
    try:
        loop.add_signal_handler(signal.SIGINT, shutdown)
        loop.add_signal_handler(signal.SIGTERM, shutdown)
    except NotImplementedError:
        # Windows
        pass

    try:
        loop.run_until_complete(run())
    except KeyboardInterrupt:
        pass
    finally:
        loop.close()

    logger.info("A7S-chan stopped.")


if __name__ == "__main__":
    main()
