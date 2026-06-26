"""IR Remote Control — NEC protocol code database for Hong Kong appliances."""

from typing import Dict, List, Optional

class IRCommand:
    def __init__(self, protocol: str, address: int, command: int, bits: int = 32):
        self.protocol = protocol
        self.address = address
        self.command = command
        self.bits = bits

    def to_dict(self) -> dict:
        return {
            "cmd": "ir",
            "protocol": self.protocol,
            "address": self.address,
            "command": self.command,
            "bits": self.bits,
        }


# ── IR Code Database ─────────────────────────────────────────
# NEC protocol. Address + Command pairs.
# NOTE: These are PLACEHOLDER codes — replace with actual codes
# learned from your remote controls.

IR_DATABASE: Dict[str, Dict[str, IRCommand]] = {
    "aircon": {
        "power_on":  IRCommand("nec", 0x00, 0x10),
        "power_off": IRCommand("nec", 0x00, 0x11),
        "temp_up":   IRCommand("nec", 0x00, 0x12),
        "temp_down": IRCommand("nec", 0x00, 0x13),
        "mode_cool": IRCommand("nec", 0x00, 0x14),
        "mode_heat": IRCommand("nec", 0x00, 0x15),
        "mode_fan":  IRCommand("nec", 0x00, 0x16),
        "fan_low":   IRCommand("nec", 0x00, 0x17),
        "fan_high":  IRCommand("nec", 0x00, 0x18),
        "swing":     IRCommand("nec", 0x00, 0x19),
    },
    "tv": {
        "power":     IRCommand("nec", 0x01, 0x10),
        "vol_up":    IRCommand("nec", 0x01, 0x11),
        "vol_down":  IRCommand("nec", 0x01, 0x12),
        "ch_up":     IRCommand("nec", 0x01, 0x13),
        "ch_down":   IRCommand("nec", 0x01, 0x14),
        "mute":      IRCommand("nec", 0x01, 0x15),
        "input_hdmi1": IRCommand("nec", 0x01, 0x16),
        "input_hdmi2": IRCommand("nec", 0x01, 0x17),
    },
    "light": {
        "power":     IRCommand("nec", 0x02, 0x10),
        "bright_up": IRCommand("nec", 0x02, 0x11),
        "bright_down": IRCommand("nec", 0x02, 0x12),
        "warm":      IRCommand("nec", 0x02, 0x13),
        "cool":      IRCommand("nec", 0x02, 0x14),
        "night":     IRCommand("nec", 0x02, 0x15),
    },
}


def list_devices() -> List[str]:
    """Return available device names."""
    return list(IR_DATABASE.keys())


def list_commands(device: str) -> List[str]:
    """Return available commands for a device."""
    if device in IR_DATABASE:
        return list(IR_DATABASE[device].keys())
    return []


def lookup(device: str, action: str) -> Optional[IRCommand]:
    """Look up IR code by device and action."""
    return IR_DATABASE.get(device, {}).get(action)


def get_all(device: str, action: str) -> Optional[dict]:
    """Get formatted command dict for Stack-chan."""
    cmd = lookup(device, action)
    return cmd.to_dict() if cmd else None
