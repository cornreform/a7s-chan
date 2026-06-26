"""Weather tool — fetch current weather and forecast."""

import aiohttp
import json
import logging
from typing import Optional

logger = logging.getLogger(__name__)

DEFAULT_API_KEY = ""  # Set in config.yaml
DEFAULT_CITY = "Hong Kong"
DEFAULT_LANG = "zh_tc"


async def fetch_current(city: str = DEFAULT_CITY, api_key: str = DEFAULT_API_KEY, lang: str = DEFAULT_LANG) -> dict:
    """Fetch current weather from OpenWeatherMap."""
    if not api_key:
        return {"error": "No API key configured. Set weather.api_key in config.yaml"}

    url = "https://api.openweathermap.org/data/2.5/weather"
    params = {"q": city, "appid": api_key, "units": "metric", "lang": lang}

    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(url, params=params, timeout=10) as r:
                if r.status != 200:
                    return {"error": f"Weather API HTTP {r.status}: {await r.text()}"}
                data = json.loads(await r.text())
                return {
                    "city": data.get("name", city),
                    "temp": round(data["main"]["temp"]),
                    "feels_like": round(data["main"]["feels_like"]),
                    "humidity": data["main"]["humidity"],
                    "condition": data["weather"][0]["description"],
                    "icon": data["weather"][0]["icon"],
                    "wind_speed": data["wind"]["speed"],
                    "city_sunrise": data["sys"].get("sunrise", 0),
                    "city_sunset": data["sys"].get("sunset", 0),
                }
    except Exception as e:
        return {"error": f"Weather fetch failed: {e}"}


async def fetch_forecast(city: str = DEFAULT_CITY, api_key: str = DEFAULT_API_KEY, days: int = 3, lang: str = DEFAULT_LANG) -> dict:
    """Fetch weather forecast."""
    if not api_key:
        return {"error": "No API key configured"}

    url = "https://api.openweathermap.org/data/2.5/forecast"
    params = {"q": city, "appid": api_key, "units": "metric", "lang": lang, "cnt": days * 8}

    try:
        async with aiohttp.ClientSession() as session:
            async with session.get(url, params=params, timeout=10) as r:
                if r.status != 200:
                    return {"error": f"Forecast API HTTP {r.status}"}
                data = json.loads(await r.text())
                forecasts = []
                for item in data.get("list", [])[:days * 8:8]:
                    forecasts.append({
                        "date": item["dt_txt"][:10],
                        "temp_high": round(item["main"]["temp_max"]),
                        "temp_low": round(item["main"]["temp_min"]),
                        "condition": item["weather"][0]["description"],
                        "icon": item["weather"][0]["icon"],
                    })
                return {
                    "city": data.get("city", {}).get("name", city),
                    "forecasts": forecasts,
                }
    except Exception as e:
        return {"error": f"Forecast fetch failed: {e}"}


def format_current(data: dict) -> str:
    """Format current weather for display."""
    if "error" in data:
        return data["error"]
    lines = [
        f"🌤  {data['city']}",
        f"🌡  {data['temp']}°C (體感 {data['feels_like']}°C)",
        f"☁️  {data['condition']}",
        f"💧 濕度 {data['humidity']}%",
        f"💨 風速 {data['wind_speed']} m/s",
    ]
    return "\n".join(lines)


def format_forecast(data: dict) -> str:
    """Format forecast for display."""
    if "error" in data:
        return data["error"]
    lines = [f"📅  {data['city']} 天氣預報"]
    for f in data.get("forecasts", []):
        lines.append(f"  {f['date']}: {f['temp_high']}°C/{f['temp_low']}°C, {f['condition']}")
    return "\n".join(lines)
