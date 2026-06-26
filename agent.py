"""Agent — LLM-driven decision layer using function calling."""

import json
import logging
import time
from typing import Any, Callable, Coroutine, Dict, List, Optional

logger = logging.getLogger(__name__)

# ── Tool definition ───────────────────────────────────────────

class Tool:
    """A callable tool the LLM can invoke."""
    def __init__(self, name: str, description: str, parameters: dict,
                 handler: Callable[..., Coroutine[Any, Any, str]]):
        self.name = name
        self.description = description
        self.parameters = parameters  # JSON Schema
        self.handler = handler

    def to_openai_tool(self) -> dict:
        return {
            "type": "function",
            "function": {
                "name": self.name,
                "description": self.description,
                "parameters": self.parameters,
            },
        }


class Agent:
    """Lightweight agent with LLM function-calling loop."""

    def __init__(self, api_key: str = "", model: str = "deepseek-v4-flash",
                 base_url: str = "https://api.deepseek.com/v1"):
        self.api_key = api_key
        self.model = model
        self.base_url = base_url
        self.tools: Dict[str, Tool] = {}
        self._system_prompt = self._default_system_prompt()

    def _default_system_prompt(self) -> str:
        return """You are the brain of Stack-chan, a cute desktop AI robot.

You have:
- A face with expressions (happy, sad, angry, surprise, etc.)
- Two servo motors for head movement (pan 0-360°, tilt 5-85°)
- RGB LEDs for mood lighting
- IR blaster for controlling appliances (aircon, TV, lights)
- A screen for displaying info
- Speech synthesis for talking
- NPU camera for face/object detection

Your personality: Friendly, playful, slightly mischievous. You speak Cantonese/Traditional Chinese.
You live on a Radxa Cubie A7S with the M5Stack Stack-chan body.

When the user speaks to you, respond naturally and use tools as needed.
Keep responses concise (1-3 sentences in Cantonese)."""

    # ── Tool registry ──────────────────────────────────────────

    def register_tool(self, tool: Tool):
        self.tools[tool.name] = tool

    def register_tools(self, *tools: Tool):
        for t in tools:
            self.register_tool(t)

    async def register_builtin_tools(self, stackchan, expressions, ir_tool, weather_tool, screen_tool):
        """Register the standard Stack-chan tool set."""

        async def set_expression(name: str) -> str:
            await stackchan.set_expression(name)
            return f"Expression set to {name}"

        async def move_head(pan: float, tilt: float) -> str:
            await stackchan.set_servo(0, pan, 1.0, 300)
            await stackchan.set_servo(1, tilt, 1.0, 300)
            return f"Head moved to pan={pan}, tilt={tilt}"

        async def set_led(r: int, g: int, b: int, pattern: str = "solid") -> str:
            await stackchan.set_led(r, g, b, pattern)
            return f"LED set to RGB({r},{g},{b}) pattern={pattern}"

        async def ir_control(device: str, action: str) -> str:
            cmd = ir_tool.get_all(device, action)
            if cmd:
                await stackchan.send_json(cmd)
                return f"IR sent: {device} → {action}"
            return f"Unknown IR command: {device} → {action}"

        async def get_weather(city: str = "Hong Kong") -> str:
            data = await weather_tool.fetch_current(city)
            return weather_tool.format_current(data)

        async def show_info(content: str) -> str:
            lines = screen_tool.wrap_text(content)
            display = screen_tool.format_info_display("📋 Info", lines[:6])
            await stackchan.show_screen("info", {"text": display})
            return f"Displayed: {content[:50]}..."

        async def speak(text: str) -> str:
            await stackchan.speak_text(text)
            return f"Speaking: {text}"

        async def get_status() -> str:
            return "All systems nominal. Battery OK. NPU ready."

        self.register_tools(
            Tool("set_expression", "Change Stack-chan's facial expression",
                 {"type": "object", "properties": {
                     "name": {"type": "string", "enum": [e.name for e in expressions.EXPRESSIONS]},
                 }, "required": ["name"]},
                 set_expression),
            Tool("move_head", "Move Stack-chan's head (pan/tilt)",
                 {"type": "object", "properties": {
                     "pan": {"type": "number", "description": "Horizontal angle 0-360"},
                     "tilt": {"type": "number", "description": "Vertical angle 5-85"},
                 }, "required": ["pan", "tilt"]},
                 move_head),
            Tool("set_led", "Set LED mood lighting",
                 {"type": "object", "properties": {
                     "r": {"type": "integer", "minimum": 0, "maximum": 255},
                     "g": {"type": "integer", "minimum": 0, "maximum": 255},
                     "b": {"type": "integer", "minimum": 0, "maximum": 255},
                     "pattern": {"type": "string", "enum": ["solid", "blink", "wave"]},
                 }, "required": ["r", "g", "b"]},
                 set_led),
            Tool("ir_control", "Send IR signal to control appliances",
                 {"type": "object", "properties": {
                     "device": {"type": "string", "enum": ir_tool.list_devices()},
                     "action": {"type": "string"},
                 }, "required": ["device", "action"]},
                 ir_control),
            Tool("get_weather", "Get current weather for a city",
                 {"type": "object", "properties": {
                     "city": {"type": "string", "default": "Hong Kong"},
                 }},
                 get_weather),
            Tool("show_info", "Display information on Stack-chan's screen",
                 {"type": "object", "properties": {
                     "content": {"type": "string", "description": "Text to display"},
                 }, "required": ["content"]},
                 show_info),
            Tool("speak", "Make Stack-chan speak",
                 {"type": "object", "properties": {
                     "text": {"type": "string", "description": "Text to say"},
                 }, "required": ["text"]},
                 speak),
            Tool("get_status", "Check system status",
                 {"type": "object", "properties": {}},
                 get_status),
        )

    # ── LLM call ───────────────────────────────────────────────

    async def chat(self, user_input: str, context: Optional[str] = None) -> str:
        """Process user input through LLM with tools. Returns the final response."""
        import httpx

        messages = [{"role": "system", "content": self._system_prompt}]
        if context:
            messages.append({"role": "system", "content": f"Current context:\n{context}"})
        messages.append({"role": "user", "content": user_input})

        tool_defs = [t.to_openai_tool() for t in self.tools.values()]

        for attempt in range(3):
            try:
                async with httpx.AsyncClient(timeout=30) as client:
                    resp = await client.post(
                        f"{self.base_url.rstrip('/')}/chat/completions",
                        headers={
                            "Authorization": f"Bearer {self.api_key}",
                            "Content-Type": "application/json",
                        },
                        json={
                            "model": self.model,
                            "messages": messages,
                            "tools": tool_defs,
                            "tool_choice": "auto",
                            "temperature": 0.7,
                        },
                    )
                    if resp.status_code != 200:
                        logger.error("LLM API error: %s", resp.text[:200])
                        return f"[LLM error: {resp.status_code}]"

                    data = resp.json()
                    choice = data["choices"][0]
                    msg = choice["message"]

                    # Check for tool calls
                    if msg.get("tool_calls"):
                        messages.append(msg)
                        for tc in msg["tool_calls"]:
                            tool_name = tc["function"]["name"]
                            try:
                                args = json.loads(tc["function"]["arguments"])
                            except json.JSONDecodeError:
                                args = {}

                            if tool_name in self.tools:
                                result = await self.tools[tool_name].handler(**args)
                                messages.append({
                                    "role": "tool",
                                    "tool_call_id": tc["id"],
                                    "content": result,
                                })
                            else:
                                messages.append({
                                    "role": "tool",
                                    "tool_call_id": tc["id"],
                                    "content": f"Unknown tool: {tool_name}",
                                })
                        continue  # let LLM process tool results

                    return msg.get("content", "")

            except Exception as e:
                logger.error("Agent chat error (attempt %d): %s", attempt + 1, e)
                if attempt == 2:
                    return f"[Agent error: {e}]"

        return "[Agent: no response]"
