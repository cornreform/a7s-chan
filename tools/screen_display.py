"""Screen display formatting tools for Stack-chan."""

def wrap_text(text: str, max_width: int = 20) -> list:
    """Wrap text to fit Stack-chan screen width (~20 Chinese chars)."""
    lines = []
    for paragraph in text.split("\n"):
        while len(paragraph) > max_width:
            # Try to break at space or punctuation
            break_at = max_width
            for i in range(max_width, max_width // 2, -1):
                if i < len(paragraph) and paragraph[i] in " .,!?，。！？、":
                    break_at = i + 1
                    break
            lines.append(paragraph[:break_at].strip())
            paragraph = paragraph[break_at:].strip()
        if paragraph:
            lines.append(paragraph)
    return lines


def paginate(lines: list, page_size: int = 6) -> list:
    """Split lines into pages for screen display."""
    pages = []
    for i in range(0, len(lines), page_size):
        pages.append(lines[i:i + page_size])
    return pages


def format_weather_display(weather_data: dict) -> str:
    """Format weather for Stack-chan screen."""
    if "error" in weather_data:
        return f"⚠️ {weather_data['error']}"
    lines = [
        f"🌤 {weather_data.get('city', '')}",
        f"{weather_data.get('temp', '--')}°C  {weather_data.get('condition', '')}",
        f"💧 {weather_data.get('humidity', '--')}%",
    ]
    return "\n".join(lines)


def format_info_display(title: str, body_lines: list) -> str:
    """Format info screen with title + body."""
    separator = "─" * 20
    return f"{title}\n{separator}\n" + "\n".join(body_lines)


def format_menu(items: list, selected: int = 0) -> str:
    """Format a button menu layout."""
    lines = []
    for i, item in enumerate(items):
        prefix = "▶ " if i == selected else "  "
        lines.append(f"{prefix}{item}")
    return "\n".join(lines)
