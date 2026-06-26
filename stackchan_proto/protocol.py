"""Stack-chan UDP Protocol — A7S ↔ M5Stack CoreS3 communication."""

import json
import struct
import asyncio
import logging
from enum import IntEnum
from typing import Optional, Callable, Awaitable

logger = logging.getLogger(__name__)

# UDP Ports
CMD_PORT = 9002       # A7S → Stack-chan (commands)
STATUS_PORT = 9000    # Stack-chan → A7S (status/events)
MIC_PORT = 9001       # Stack-chan → A7S (audio stream, raw PCM)

# Binary command magic + servo encoding
MAGIC = 0xA7A7
SERVO_CMD = 0x01
EXPR_CMD = 0x02
LED_CMD = 0x03
IR_CMD = 0x04
AUDIO_CMD = 0x05

class ServoID(IntEnum):
    PAN = 0
    TILT = 1


class StackChanProtocol:
    """Async UDP client for communicating with Stack-chan firmware."""

    def __init__(
        self,
        remote_addr: str = "192.168.1.100",
        cmd_port: int = CMD_PORT,
        status_port: int = STATUS_PORT,
        local_addr: str = "0.0.0.0",
    ):
        self.remote_addr = remote_addr
        self.cmd_port = cmd_port
        self.remote = (remote_addr, cmd_port)

        self._cmd_transport: Optional[asyncio.DatagramTransport] = None
        self._status_transport: Optional[asyncio.DatagramTransport] = None
        self._status_callback: Optional[Callable[[dict], Awaitable[None]]] = None

        self._loop = asyncio.get_event_loop()
        self._local_addr = local_addr

    # ── Lifecycle ──────────────────────────────────────────────

    async def start(self, status_callback: Optional[Callable[[dict], Awaitable[None]]] = None):
        """Open UDP sockets."""
        self._status_callback = status_callback

        class CmdProto(asyncio.DatagramProtocol):
            def connection_made(s, t): self._cmd_transport = t
            def datagram_received(s, d, a): pass
            def error_received(s, e): logger.error("CMD UDP error: %s", e)

        class StatusProto(asyncio.DatagramProtocol):
            def connection_made(s, t): self._status_transport = t
            def datagram_received(s, d, a):
                self._on_status(d, a)
            def error_received(s, e): logger.error("Status UDP error: %s", e)

        await self._loop.create_datagram_endpoint(CmdProto, local_addr=(self._local_addr, 0))
        await self._loop.create_datagram_endpoint(StatusProto, local_addr=(self._local_addr, STATUS_PORT))
        logger.info("StackChan UDP started — cmd→%s:%d, status←:%d", self.remote_addr, self.cmd_port, STATUS_PORT)

    def stop(self):
        if self._cmd_transport:
            self._cmd_transport.close()
        if self._status_transport:
            self._status_transport.close()

    # ── Send helpers ───────────────────────────────────────────

    async def send_json(self, cmd: dict):
        """Send a JSON command as UDP datagram."""
        payload = json.dumps(cmd, ensure_ascii=False).encode("utf-8")
        self._cmd_transport.sendto(payload, self.remote)
        logger.debug("TX: %s", cmd)

    async def send_binary(self, cmd_type: int, data: bytes):
        """Send a binary-encoded command (compact, low latency)."""
        header = struct.pack("<HB", MAGIC, cmd_type)
        self._cmd_transport.sendto(header + data, self.remote)

    async def set_servo(self, servo: ServoID, angle: float, speed: float = 1.0, duration_ms: int = 300):
        """Set servo angle with speed and duration."""
        data = struct.pack("<BffH", int(servo), angle, speed, duration_ms)
        await self.send_binary(SERVO_CMD, data)

    async def set_expression(self, name: str):
        """Switch facial expression by name."""
        await self.send_json({"cmd": "face", "expression": name})

    async def set_led(self, r: int, g: int, b: int, pattern: str = "solid"):
        """Set RGB LEDs."""
        await self.send_json({"cmd": "led", "r": r, "g": g, "b": b, "pattern": pattern})

    async def ir_blast(self, protocol: str = "nec", address: int = 0, command: int = 0):
        """Send IR code."""
        await self.send_json({"cmd": "ir", "protocol": protocol, "address": address, "command": command})

    async def speak_text(self, text: str):
        """Display text on screen (TTS audio handled separately)."""
        await self.send_json({"cmd": "speak", "text": text})

    async def show_screen(self, mode: str, data: dict):
        """Display formatted content on screen."""
        await self.send_json({"cmd": "screen", "mode": mode, "data": data})

    async def play_emote(self, animation: str):
        """Play an animation sequence."""
        await self.send_json({"cmd": "emote", "animation": animation})

    # ── Status handler ─────────────────────────────────────────

    def _on_status(self, data: bytes, addr: tuple):
        try:
            msg = json.loads(data.decode("utf-8"))
            logger.debug("RX status: %s", msg)
            if self._status_callback:
                asyncio.ensure_future(self._status_callback(msg))
        except Exception:
            logger.warning("Bad status packet from %s: %d bytes", addr, len(data))
