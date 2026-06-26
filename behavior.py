"""Behavior State Machine — controls Stack-chan's autonomous behavior."""

import asyncio
import logging
import time
from enum import Enum
from typing import Optional, Callable, Awaitable

logger = logging.getLogger(__name__)


class State(Enum):
    IDLE = "idle"
    TRACKING = "tracking"
    CONVERSE = "converse"
    AUTONOMOUS = "autonomous"
    EXECUTE = "execute"
    SLEEP = "sleep"


TRANSITIONS = {
    State.IDLE:       [State.TRACKING, State.CONVERSE, State.SLEEP, State.AUTONOMOUS],
    State.TRACKING:   [State.IDLE, State.CONVERSE, State.SLEEP],
    State.CONVERSE:   [State.IDLE, State.EXECUTE, State.TRACKING],
    State.AUTONOMOUS: [State.IDLE, State.TRACKING, State.SLEEP],
    State.EXECUTE:    [State.IDLE, State.CONVERSE],
    State.SLEEP:      [State.IDLE, State.TRACKING],
}


class BehaviorMachine:
    """Finite state machine for Stack-chan behavior."""

    def __init__(
        self,
        on_enter_state: Optional[Callable[[State], Awaitable[None]]] = None,
        on_idle_tick: Optional[Callable[[], Awaitable[None]]] = None,
        on_track_tick: Optional[Callable[[], Awaitable[None]]] = None,
        on_sleep_timeout: Optional[Callable[[], Awaitable[None]]] = None,
    ):
        self.state = State.IDLE
        self.prev_state = State.IDLE
        self._on_enter_state = on_enter_state
        self._on_idle_tick = on_idle_tick
        self._on_track_tick = on_track_tick
        self._on_sleep_timeout = on_sleep_timeout

        # Timing
        self._state_enter_time = time.monotonic()
        self._last_interaction = time.monotonic()

        # Configuration
        self.idle_timeout_s = 30.0        # idle → sleep
        self.sleep_idle_timeout_s = 300.0 # deep sleep after 5min
        self.auto_scan_interval = 3.0     # look around every N seconds when idle
        self._wake_word_detected = False
        self._user_spoke = False

    # ── Events ─────────────────────────────────────────────────

    def on_face_detected(self):
        """Called when vision detects a face."""
        self._last_interaction = time.monotonic()
        if self.state in (State.IDLE, State.AUTONOMOUS):
            asyncio.ensure_future(self.transition(State.TRACKING))

    def on_face_lost(self):
        """Called when vision loses the face."""
        if self.state == State.TRACKING:
            asyncio.ensure_future(self.transition(State.IDLE))

    def on_user_spoke(self):
        """Called when voice transcription produces text."""
        self._last_interaction = time.monotonic()
        self._user_spoke = True
        if self.state != State.CONVERSE:
            asyncio.ensure_future(self.transition(State.CONVERSE))

    def on_wake_word(self):
        """Called on wake word detection."""
        self._wake_word_detected = True
        self._last_interaction = time.monotonic()
        if self.state == State.SLEEP:
            asyncio.ensure_future(self.transition(State.IDLE))
        asyncio.ensure_future(self.transition(State.CONVERSE))

    def on_touch(self, zone: int):
        """Called on touch sensor event."""
        self._last_interaction = time.monotonic()
        if self.state == State.SLEEP:
            asyncio.ensure_future(self.transition(State.IDLE))

    def on_conversation_end(self):
        """Called when conversation ends."""
        self._user_spoke = False
        asyncio.ensure_future(self.transition(State.TRACKING))

    # ── Transitions ────────────────────────────────────────────

    async def transition(self, target: State):
        """Attempt to transition to target state."""
        if target == self.state:
            return
        if target in TRANSITIONS.get(self.state, []):
            self.prev_state = self.state
            self.state = target
            self._state_enter_time = time.monotonic()
            logger.info("Behavior: %s → %s", self.prev_state.value, target.value)
            if self._on_enter_state:
                await self._on_enter_state(target)
        else:
            logger.debug("Transition %s → %s not allowed", self.state.value, target.value)

    # ── Main tick (call at ~10Hz) ──────────────────────────────

    async def tick(self, has_face: bool):
        """Update state machine logic."""
        now = time.monotonic()
        elapsed_idle = now - self._last_interaction

        # Auto-sleep
        if self.state in (State.IDLE,) and elapsed_idle > self.idle_timeout_s:
            await self.transition(State.SLEEP)

        # State-specific behavior
        if self.state == State.IDLE:
            if self._on_idle_tick and int(now / self.auto_scan_interval) % 2 == 0:
                await self._on_idle_tick()

        elif self.state == State.TRACKING:
            if not has_face:
                elapsed_no_face = now - self._state_enter_time
                if elapsed_no_face > 5.0:
                    await self.transition(State.IDLE)
            if self._on_track_tick:
                await self._on_track_tick()

        elif self.state == State.SLEEP:
            if elapsed_idle > self.sleep_idle_timeout_s:
                if self._on_sleep_timeout:
                    await self._on_sleep_timeout()

        # Conversation timeout
        if self.state == State.CONVERSE and elapsed_idle > 15.0:
            if not self._user_spoke:
                await self.transition(State.TRACKING)

    # ── Queries ────────────────────────────────────────────────

    @property
    def state_elapsed(self) -> float:
        return time.monotonic() - self._state_enter_time

    def is_interactive(self) -> bool:
        return self.state in (State.CONVERSE, State.TRACKING)

    def should_process_voice(self) -> bool:
        return self.state in (State.IDLE, State.TRACKING, State.CONVERSE)
