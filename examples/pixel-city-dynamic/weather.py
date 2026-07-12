#!/usr/bin/env python3
"""Pure weather data, cache, interpolation, and GIF generation for Pixel City."""

from __future__ import annotations

from bisect import bisect_right
from dataclasses import dataclass
from datetime import datetime, timezone as datetime_timezone
import fcntl
import json
import math
import os
from pathlib import Path
import random
import struct
import tempfile
from typing import Any, Callable, Dict, Iterable, Mapping, Optional, Sequence, Tuple
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
import zlib


OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast"
WEATHER_CACHE_SCHEMA_VERSION = 1
WEATHER_CACHE_MAX_AGE_SECONDS = 3 * 60 * 60
DEFAULT_WEATHER_REFRESH_SECONDS = 900
MIN_WEATHER_REFRESH_SECONDS = 600
MAX_WEATHER_REFRESH_SECONDS = 21600
MAX_ERROR_LENGTH = 512
PIXEL_WIDTH = 576
PIXEL_HEIGHT = 324
MAX_GIF_FRAMES = 8
GIF_FRAME_COUNT = 6
MAX_GIF_BYTES = 16 * 1024 * 1024
MAX_GIF_PIXELS = PIXEL_WIDTH * PIXEL_HEIGHT

WEATHER_FIELDS = (
    "temperature_2m",
    "apparent_temperature",
    "weather_code",
    "cloud_cover",
    "visibility",
    "precipitation",
    "rain",
    "showers",
    "snowfall",
    "snow_depth",
    "wind_speed_10m",
    "wind_direction_10m",
    "wind_gusts_10m",
)

EXPECTED_UNITS: Mapping[str, str] = {
    "temperature_2m": "°C",
    "apparent_temperature": "°C",
    "weather_code": "wmo code",
    "cloud_cover": "%",
    "visibility": "m",
    "precipitation": "mm",
    "rain": "mm",
    "showers": "mm",
    "snowfall": "cm",
    "snow_depth": "m",
    "wind_speed_10m": "m/s",
    "wind_direction_10m": "°",
    "wind_gusts_10m": "m/s",
}

VALUE_RANGES: Mapping[str, Tuple[float, float]] = {
    "temperature_2m": (-100.0, 70.0),
    "apparent_temperature": (-120.0, 80.0),
    "weather_code": (0.0, 9999.0),
    "cloud_cover": (0.0, 100.0),
    "visibility": (0.0, 100000.0),
    "precipitation": (0.0, 1000.0),
    "rain": (0.0, 1000.0),
    "showers": (0.0, 1000.0),
    "snowfall": (0.0, 500.0),
    "snow_depth": (0.0, 100.0),
    "wind_speed_10m": (0.0, 150.0),
    "wind_direction_10m": (0.0, 360.0),
    "wind_gusts_10m": (0.0, 200.0),
}

KNOWN_WMO_CODES = frozenset(
    (0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67,
     71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99)
)
PRECIPITATION_TYPES = frozenset(("none", "rain", "snow", "hail"))
WEATHER_PRESETS = (
    "clear",
    "cloudy",
    "fog",
    "rain-light",
    "rain-heavy",
    "snow-light",
    "snow-heavy",
    "hail-light",
    "hail-heavy",
    "wind",
    "heat",
)


class WeatherError(RuntimeError):
    """Weather provider data does not satisfy the controller contract."""


class WeatherCacheError(RuntimeError):
    """Weather cache could not be read or written safely."""


def _bounded_error(error: BaseException) -> str:
    clean = "".join(character for character in str(error) if character >= " " and character != "\x7f")
    return clean[:MAX_ERROR_LENGTH] or error.__class__.__name__


def _clean_text(value: Any, name: str, maximum: int = 160) -> str:
    if not isinstance(value, str):
        raise WeatherError(f"{name} must be a string")
    clean = "".join(character for character in value.strip() if character >= " " and character != "\x7f")
    if not clean or len(clean) > maximum:
        raise WeatherError(f"{name} must contain 1..{maximum} safe characters")
    return clean


def _number(value: Any, name: str, minimum: float, maximum: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise WeatherError(f"{name} must be numeric")
    result = float(value)
    if not math.isfinite(result) or result < minimum or result > maximum:
        raise WeatherError(f"{name} must be in {minimum}..{maximum}")
    return result


def _integer(value: Any, name: str, minimum: int, maximum: int) -> int:
    number = _number(value, name, float(minimum), float(maximum))
    if not number.is_integer():
        raise WeatherError(f"{name} must be an integer")
    return int(number)


def _zone(name: Any) -> ZoneInfo:
    zone_name = _clean_text(name, "timezone", 128)
    try:
        return ZoneInfo(zone_name)
    except (ValueError, ZoneInfoNotFoundError) as error:
        raise WeatherError(f"unknown IANA timezone: {zone_name}") from error


def _clamp(value: float, minimum: float = 0.0, maximum: float = 1.0) -> float:
    return max(minimum, min(maximum, value))


def _lerp(start: float, end: float, amount: float) -> float:
    return start + (end - start) * amount


def _smoothstep(amount: float) -> float:
    bounded = _clamp(amount)
    return bounded * bounded * (3.0 - 2.0 * bounded)


def _wrap_degrees(value: float) -> float:
    return (value + 180.0) % 360.0 - 180.0


def _shortest_angle_lerp(start: float, end: float, amount: float) -> float:
    return (start + _wrap_degrees(end - start) * amount) % 360.0


def _parse_timestamp(value: Any, timezone_name: str, name: str) -> datetime:
    text = _clean_text(value, name, 64)
    try:
        parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError as error:
        raise WeatherError(f"{name} must be an ISO-8601 timestamp") from error
    zone = _zone(timezone_name)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=zone)
    else:
        parsed = parsed.astimezone(zone)
    return parsed


@dataclass(frozen=True)
class WeatherSample:
    timestamp: datetime
    weather_code: int
    temperature_c: float
    apparent_temperature_c: float
    cloud_cover_percent: float
    visibility_m: float
    precipitation_mm: float
    rain_mm: float
    showers_mm: float
    snowfall_cm: float
    snow_depth_m: float
    wind_speed_ms: float
    wind_direction_degrees: float
    wind_gusts_ms: float

    def to_mapping(self) -> Dict[str, Any]:
        return {
            "timestamp": self.timestamp.isoformat(),
            "weather_code": self.weather_code,
            "temperature_c": self.temperature_c,
            "apparent_temperature_c": self.apparent_temperature_c,
            "cloud_cover_percent": self.cloud_cover_percent,
            "visibility_m": self.visibility_m,
            "precipitation_mm": self.precipitation_mm,
            "rain_mm": self.rain_mm,
            "showers_mm": self.showers_mm,
            "snowfall_cm": self.snowfall_cm,
            "snow_depth_m": self.snow_depth_m,
            "wind_speed_ms": self.wind_speed_ms,
            "wind_direction_degrees": self.wind_direction_degrees,
            "wind_gusts_ms": self.wind_gusts_ms,
        }

    @classmethod
    def from_mapping(cls, value: Mapping[str, Any], timezone_name: str) -> "WeatherSample":
        if not isinstance(value, Mapping):
            raise WeatherError("weather sample must be an object")
        return cls(
            timestamp=_parse_timestamp(value.get("timestamp"), timezone_name, "timestamp"),
            weather_code=_integer(value.get("weather_code"), "weather_code", 0, 9999),
            temperature_c=_number(value.get("temperature_c"), "temperature_c", -100.0, 70.0),
            apparent_temperature_c=_number(
                value.get("apparent_temperature_c"), "apparent_temperature_c", -120.0, 80.0
            ),
            cloud_cover_percent=_number(
                value.get("cloud_cover_percent"), "cloud_cover_percent", 0.0, 100.0
            ),
            visibility_m=_number(value.get("visibility_m"), "visibility_m", 0.0, 100000.0),
            precipitation_mm=_number(
                value.get("precipitation_mm"), "precipitation_mm", 0.0, 1000.0
            ),
            rain_mm=_number(value.get("rain_mm"), "rain_mm", 0.0, 1000.0),
            showers_mm=_number(value.get("showers_mm"), "showers_mm", 0.0, 1000.0),
            snowfall_cm=_number(value.get("snowfall_cm"), "snowfall_cm", 0.0, 500.0),
            snow_depth_m=_number(value.get("snow_depth_m"), "snow_depth_m", 0.0, 100.0),
            wind_speed_ms=_number(value.get("wind_speed_ms"), "wind_speed_ms", 0.0, 150.0),
            wind_direction_degrees=_number(
                value.get("wind_direction_degrees"), "wind_direction_degrees", 0.0, 360.0
            ),
            wind_gusts_ms=_number(value.get("wind_gusts_ms"), "wind_gusts_ms", 0.0, 200.0),
        )


@dataclass(frozen=True)
class WeatherState:
    condition: str
    cloud_intensity: float
    fog_intensity: float
    precipitation_type: str
    precipitation_intensity: float
    wind_intensity: float
    wind_screen_direction: float
    heat_intensity: float
    snow_cover: float

    def __post_init__(self) -> None:
        if self.precipitation_type not in PRECIPITATION_TYPES:
            raise ValueError(f"unsupported precipitation type: {self.precipitation_type}")
        for name in (
            "cloud_intensity", "fog_intensity", "precipitation_intensity",
            "wind_intensity", "heat_intensity", "snow_cover",
        ):
            value = getattr(self, name)
            if not math.isfinite(value) or not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} must be bounded to 0..1")
        if not math.isfinite(self.wind_screen_direction) or not -1.0 <= self.wind_screen_direction <= 1.0:
            raise ValueError("wind_screen_direction must be bounded to -1..1")

    def to_mapping(self) -> Dict[str, Any]:
        return {
            "condition": self.condition,
            "cloud_intensity": self.cloud_intensity,
            "fog_intensity": self.fog_intensity,
            "precipitation_type": self.precipitation_type,
            "precipitation_intensity": self.precipitation_intensity,
            "wind_intensity": self.wind_intensity,
            "wind_screen_direction": self.wind_screen_direction,
            "heat_intensity": self.heat_intensity,
            "snow_cover": self.snow_cover,
        }


@dataclass(frozen=True)
class WeatherTimeline:
    location_identity: str
    timezone: str
    requested_latitude: float
    requested_longitude: float
    response_latitude: float
    response_longitude: float
    fetched_at: datetime
    current: WeatherSample
    hourly: Tuple[WeatherSample, ...]
    warnings: Tuple[str, ...] = ()

    def to_mapping(self) -> Dict[str, Any]:
        return {
            "location_identity": self.location_identity,
            "timezone": self.timezone,
            "requested_latitude": self.requested_latitude,
            "requested_longitude": self.requested_longitude,
            "response_latitude": self.response_latitude,
            "response_longitude": self.response_longitude,
            "fetched_at": self.fetched_at.astimezone(datetime_timezone.utc).isoformat(),
            "current": self.current.to_mapping(),
            "hourly": [sample.to_mapping() for sample in self.hourly],
            "warnings": list(self.warnings),
        }

    @classmethod
    def from_mapping(
        cls,
        value: Mapping[str, Any],
        expected_identity: str,
        expected_timezone: str,
        expected_latitude: Optional[float] = None,
        expected_longitude: Optional[float] = None,
    ) -> "WeatherTimeline":
        if not isinstance(value, Mapping):
            raise WeatherError("weather timeline must be an object")
        identity = _clean_text(value.get("location_identity"), "location_identity", 192)
        timezone_name = _clean_text(value.get("timezone"), "timezone", 128)
        if identity != expected_identity:
            raise WeatherError("weather cache location does not match resolved location")
        if timezone_name != expected_timezone:
            raise WeatherError("weather cache timezone does not match resolved location")
        raw_hourly = value.get("hourly")
        if not isinstance(raw_hourly, list) or not 2 <= len(raw_hourly) <= 96:
            raise WeatherError("weather hourly timeline must contain 2..96 samples")
        hourly = tuple(WeatherSample.from_mapping(item, timezone_name) for item in raw_hourly)
        _validate_timestamps(hourly)
        fetched_at = _parse_timestamp(value.get("fetched_at"), "UTC", "fetched_at")
        raw_warnings = value.get("warnings", [])
        if not isinstance(raw_warnings, list) or len(raw_warnings) > 32:
            raise WeatherError("weather warnings must be a bounded array")
        warnings = tuple(_clean_text(item, "weather warning", MAX_ERROR_LENGTH) for item in raw_warnings)
        requested_latitude = _number(
            value.get("requested_latitude"), "requested_latitude", -90.0, 90.0
        )
        requested_longitude = _number(
            value.get("requested_longitude"), "requested_longitude", -180.0, 180.0
        )
        canonical_identity = (
            f"{requested_latitude:.4f},{requested_longitude:.4f},{timezone_name}"
        )
        if canonical_identity != identity:
            raise WeatherError("weather timeline identity does not match its coordinates")
        if expected_latitude is not None and f"{requested_latitude:.4f}" != f"{expected_latitude:.4f}":
            raise WeatherError("weather cache latitude does not match resolved location")
        if expected_longitude is not None and f"{requested_longitude:.4f}" != f"{expected_longitude:.4f}":
            raise WeatherError("weather cache longitude does not match resolved location")
        return cls(
            location_identity=identity,
            timezone=timezone_name,
            requested_latitude=requested_latitude,
            requested_longitude=requested_longitude,
            response_latitude=_number(value.get("response_latitude"), "response_latitude", -90.0, 90.0),
            response_longitude=_number(value.get("response_longitude"), "response_longitude", -180.0, 180.0),
            fetched_at=fetched_at,
            current=WeatherSample.from_mapping(value.get("current"), timezone_name),
            hourly=hourly,
            warnings=warnings,
        )


@dataclass(frozen=True)
class WeatherCacheResult:
    timeline: Optional[WeatherTimeline]
    source: str
    stale: bool
    age_seconds: Optional[float]
    attempted: bool
    error: Optional[str] = None


def neutral_weather_sample(timestamp: datetime) -> WeatherSample:
    if timestamp.tzinfo is None:
        raise ValueError("weather sample timestamp must be timezone-aware")
    return WeatherSample(
        timestamp=timestamp,
        weather_code=0,
        temperature_c=20.0,
        apparent_temperature_c=20.0,
        cloud_cover_percent=0.0,
        visibility_m=50000.0,
        precipitation_mm=0.0,
        rain_mm=0.0,
        showers_mm=0.0,
        snowfall_cm=0.0,
        snow_depth_m=0.0,
        wind_speed_ms=0.0,
        wind_direction_degrees=0.0,
        wind_gusts_ms=0.0,
    )


def neutral_weather_state() -> WeatherState:
    return WeatherState("clear", 0.0, 0.0, "none", 0.0, 0.0, 0.0, 0.0, 0.0)


def _sample_from_provider(value: Mapping[str, Any], timezone_name: str, prefix: str) -> WeatherSample:
    if not isinstance(value, Mapping):
        raise WeatherError(f"{prefix} weather sample must be an object")
    timestamp = _parse_timestamp(value.get("time"), timezone_name, f"{prefix}.time")
    normalized: Dict[str, Any] = {"timestamp": timestamp.isoformat()}
    mapping = {
        "weather_code": "weather_code",
        "temperature_2m": "temperature_c",
        "apparent_temperature": "apparent_temperature_c",
        "cloud_cover": "cloud_cover_percent",
        "visibility": "visibility_m",
        "precipitation": "precipitation_mm",
        "rain": "rain_mm",
        "showers": "showers_mm",
        "snowfall": "snowfall_cm",
        "snow_depth": "snow_depth_m",
        "wind_speed_10m": "wind_speed_ms",
        "wind_direction_10m": "wind_direction_degrees",
        "wind_gusts_10m": "wind_gusts_ms",
    }
    for provider_name, sample_name in mapping.items():
        minimum, maximum = VALUE_RANGES[provider_name]
        raw = value.get(provider_name)
        if provider_name == "weather_code":
            normalized[sample_name] = _integer(raw, f"{prefix}.{provider_name}", int(minimum), int(maximum))
        else:
            normalized[sample_name] = _number(raw, f"{prefix}.{provider_name}", minimum, maximum)
    return WeatherSample.from_mapping(normalized, timezone_name)


def _validate_units(units: Any, prefix: str) -> None:
    if not isinstance(units, Mapping):
        raise WeatherError(f"{prefix}_units must be an object")
    if units.get("time") != "iso8601":
        raise WeatherError(f"{prefix}_units.time must be iso8601")
    if prefix == "current" and units.get("interval") != "seconds":
        raise WeatherError("current_units.interval must be seconds")
    for field_name, expected in EXPECTED_UNITS.items():
        actual = units.get(field_name)
        if actual != expected:
            raise WeatherError(
                f"{prefix}_units.{field_name} must be {expected}, got {actual!r}"
            )


def _validate_timestamps(samples: Sequence[WeatherSample]) -> None:
    previous: Optional[datetime] = None
    for sample in samples:
        instant = sample.timestamp.astimezone(datetime_timezone.utc)
        if previous is not None and instant <= previous:
            raise WeatherError("hourly timestamps must be strictly increasing")
        previous = instant


class OpenMeteoProvider:
    def __init__(self, client: Any) -> None:
        self.client = client

    @staticmethod
    def request_url(location: Any) -> str:
        query = urlencode(
            (
                ("latitude", f"{float(location.latitude):.4f}"),
                ("longitude", f"{float(location.longitude):.4f}"),
                ("timezone", str(location.timezone)),
                ("forecast_days", "2"),
                ("temperature_unit", "celsius"),
                ("wind_speed_unit", "ms"),
                ("precipitation_unit", "mm"),
                ("current", ",".join(WEATHER_FIELDS)),
                ("hourly", ",".join(WEATHER_FIELDS)),
            ),
            safe=",/",
        )
        if "apikey" in query.lower() or "api_key" in query.lower():
            raise WeatherError("Open-Meteo request must never contain an API key")
        return f"{OPEN_METEO_URL}?{query}"

    def fetch(self, location: Any, fetched_at: datetime) -> WeatherTimeline:
        if fetched_at.tzinfo is None:
            raise ValueError("fetched_at must be timezone-aware")
        value = self.client.get_json(self.request_url(location))
        if not isinstance(value, Mapping):
            raise WeatherError("Open-Meteo response root must be an object")
        if value.get("error"):
            reason = value.get("reason", "provider error")
            raise WeatherError(f"Open-Meteo failed: {_clean_text(reason, 'reason', 256)}")
        timezone_name = _clean_text(value.get("timezone"), "timezone", 128)
        if timezone_name != location.timezone:
            raise WeatherError("Open-Meteo timezone does not match resolved location")
        _zone(timezone_name)
        response_latitude = _number(value.get("latitude"), "latitude", -90.0, 90.0)
        response_longitude = _number(value.get("longitude"), "longitude", -180.0, 180.0)
        utc_offset_seconds = _integer(
            value.get("utc_offset_seconds"), "utc_offset_seconds", -86400, 86400
        )
        _validate_units(value.get("current_units"), "current")
        _validate_units(value.get("hourly_units"), "hourly")

        raw_current = value.get("current")
        if not isinstance(raw_current, Mapping):
            raise WeatherError("current must be an object")
        _integer(raw_current.get("interval"), "current.interval", 1, 86400)
        current = _sample_from_provider(raw_current, timezone_name, "current")
        expected_offset = int(current.timestamp.utcoffset().total_seconds())
        if utc_offset_seconds != expected_offset:
            raise WeatherError(
                "Open-Meteo UTC offset does not match resolved timezone at current.time"
            )
        raw_hourly = value.get("hourly")
        if not isinstance(raw_hourly, Mapping):
            raise WeatherError("hourly must be an object")
        raw_times = raw_hourly.get("time")
        if not isinstance(raw_times, list) or not 2 <= len(raw_times) <= 96:
            raise WeatherError("hourly.time must contain 2..96 timestamps")
        length = len(raw_times)
        arrays: Dict[str, list] = {}
        for field_name in WEATHER_FIELDS:
            raw_array = raw_hourly.get(field_name)
            if not isinstance(raw_array, list) or len(raw_array) != length:
                raise WeatherError(f"hourly.{field_name} length must match hourly.time")
            arrays[field_name] = raw_array
        hourly = []
        for index, timestamp in enumerate(raw_times):
            item = {"time": timestamp}
            item.update({name: arrays[name][index] for name in WEATHER_FIELDS})
            hourly.append(_sample_from_provider(item, timezone_name, f"hourly[{index}]"))
        samples = tuple(hourly)
        _validate_timestamps(samples)
        span = (
            samples[-1].timestamp.astimezone(datetime_timezone.utc)
            - samples[0].timestamp.astimezone(datetime_timezone.utc)
        ).total_seconds()
        if span <= 0.0 or span > 72 * 60 * 60:
            raise WeatherError("hourly timeline span must be within 72 hours")

        unknown = sorted(
            {sample.weather_code for sample in (current, *samples)} - KNOWN_WMO_CODES
        )
        warnings = ()
        if unknown:
            codes = ", ".join(str(code) for code in unknown[:16])
            warnings = (f"unknown WMO weather code(s): {codes}; numeric fields used",)
        return WeatherTimeline(
            location_identity=location.identity,
            timezone=timezone_name,
            requested_latitude=float(location.latitude),
            requested_longitude=float(location.longitude),
            response_latitude=response_latitude,
            response_longitude=response_longitude,
            fetched_at=fetched_at.astimezone(datetime_timezone.utc),
            current=current,
            hourly=samples,
            warnings=warnings,
        )


def interpolate_weather_sample(timeline: WeatherTimeline, when: datetime) -> WeatherSample:
    if when.tzinfo is None:
        raise ValueError("weather evaluation time must be timezone-aware")
    samples = timeline.hourly
    instants = [sample.timestamp.astimezone(datetime_timezone.utc) for sample in samples]
    target = when.astimezone(datetime_timezone.utc)
    if target <= instants[0]:
        return samples[0]
    if target >= instants[-1]:
        return samples[-1]
    right_index = bisect_right(instants, target)
    left = samples[right_index - 1]
    right = samples[right_index]
    span = max(1.0, (instants[right_index] - instants[right_index - 1]).total_seconds())
    amount = _clamp((target - instants[right_index - 1]).total_seconds() / span)
    return WeatherSample(
        timestamp=when.astimezone(_zone(timeline.timezone)),
        weather_code=left.weather_code if amount < 0.5 else right.weather_code,
        temperature_c=_lerp(left.temperature_c, right.temperature_c, amount),
        apparent_temperature_c=_lerp(
            left.apparent_temperature_c, right.apparent_temperature_c, amount
        ),
        cloud_cover_percent=_lerp(left.cloud_cover_percent, right.cloud_cover_percent, amount),
        visibility_m=_lerp(left.visibility_m, right.visibility_m, amount),
        precipitation_mm=_lerp(left.precipitation_mm, right.precipitation_mm, amount),
        rain_mm=_lerp(left.rain_mm, right.rain_mm, amount),
        showers_mm=_lerp(left.showers_mm, right.showers_mm, amount),
        snowfall_cm=_lerp(left.snowfall_cm, right.snowfall_cm, amount),
        snow_depth_m=_lerp(left.snow_depth_m, right.snow_depth_m, amount),
        wind_speed_ms=_lerp(left.wind_speed_ms, right.wind_speed_ms, amount),
        wind_direction_degrees=_shortest_angle_lerp(
            left.wind_direction_degrees, right.wind_direction_degrees, amount
        ),
        wind_gusts_ms=_lerp(left.wind_gusts_ms, right.wind_gusts_ms, amount),
    )


_RAIN_SEVERITY = {
    51: 0.12, 53: 0.28, 55: 0.52, 56: 0.30, 57: 0.62,
    61: 0.25, 63: 0.50, 65: 0.88, 66: 0.45, 67: 0.92,
    80: 0.35, 81: 0.65, 82: 1.00, 95: 0.58,
}
_SNOW_SEVERITY = {71: 0.25, 73: 0.52, 75: 0.90, 77: 0.42, 85: 0.45, 86: 0.90}
_HAIL_SEVERITY = {96: 0.55, 99: 1.00}


def derive_weather_state(sample: WeatherSample, view_azimuth: float = 0.0) -> WeatherState:
    if not math.isfinite(view_azimuth):
        raise ValueError("view azimuth must be finite")
    view = view_azimuth % 360.0
    cloud = _smoothstep(sample.cloud_cover_percent / 100.0)
    visibility_fog = _smoothstep((10000.0 - sample.visibility_m) / 10000.0)
    fog_floor = 1.0 if sample.weather_code == 48 else 0.65 if sample.weather_code == 45 else 0.0
    fog = _clamp(max(fog_floor, visibility_fog))

    rain_amount = sample.rain_mm + sample.showers_mm
    rain_numeric = _clamp(1.0 - math.exp(-rain_amount / 4.0))
    rain_code = _RAIN_SEVERITY.get(sample.weather_code, 0.0)
    if 51 <= sample.weather_code <= 67:
        rain_code = max(rain_code, 0.35)
    if 80 <= sample.weather_code <= 82 or sample.weather_code == 95:
        rain_code = max(rain_code, 0.35)
    rain = max(rain_code, rain_numeric)
    if sample.precipitation_mm > 0.0 and sample.snowfall_cm <= 0.0:
        rain = max(rain, _clamp(1.0 - math.exp(-sample.precipitation_mm / 4.0)))
    snow_numeric = _clamp(1.0 - math.exp(-sample.snowfall_cm / 2.5))
    snow_code = _SNOW_SEVERITY.get(sample.weather_code, 0.0)
    if 71 <= sample.weather_code <= 77 or 85 <= sample.weather_code <= 86:
        snow_code = max(snow_code, 0.35)
    snow = max(snow_code, snow_numeric)
    hail = _HAIL_SEVERITY.get(sample.weather_code, 0.0)
    if hail > 0.0:
        precipitation_type, precipitation = "hail", hail
    elif snow > 0.0005:
        precipitation_type, precipitation = "snow", snow
    elif rain > 0.0005:
        precipitation_type, precipitation = "rain", rain
    else:
        precipitation_type, precipitation = "none", 0.0

    sustained = _smoothstep((sample.wind_speed_ms - 2.0) / 16.0)
    gust = _smoothstep((sample.wind_gusts_ms - 5.0) / 25.0)
    wind = _clamp(1.0 - (1.0 - sustained) * (1.0 - 0.65 * gust))
    motion_bearing = (sample.wind_direction_degrees + 180.0) % 360.0
    screen_direction = _clamp(
        math.sin(math.radians(_wrap_degrees(motion_bearing - view))), -1.0, 1.0
    )
    if abs(screen_direction) < 1e-12:
        screen_direction = 0.0

    temperature = max(sample.temperature_c, sample.apparent_temperature_c)
    heat = _smoothstep((temperature - 30.0) / 12.0)
    heat *= 1.0 - _smoothstep((cloud - 0.45) / 0.45)
    heat *= 1.0 - _smoothstep((fog - 0.25) / 0.55)
    heat *= 1.0 - _smoothstep((precipitation - 0.20) / 0.65)
    heat = _clamp(heat)
    snow_cover = _clamp(sample.snow_depth_m / 0.08)

    if precipitation_type != "none":
        condition = precipitation_type
    elif fog > 0.12:
        condition = "fog"
    elif cloud > 0.12:
        condition = "cloudy"
    else:
        condition = "clear"
    return WeatherState(
        condition=condition,
        cloud_intensity=_clamp(cloud),
        fog_intensity=_clamp(fog),
        precipitation_type=precipitation_type,
        precipitation_intensity=_clamp(precipitation),
        wind_intensity=_clamp(wind),
        wind_screen_direction=screen_direction,
        heat_intensity=heat,
        snow_cover=snow_cover,
    )


def weather_state_at(
    timeline: WeatherTimeline, when: datetime, view_azimuth: float = 0.0
) -> Tuple[WeatherSample, WeatherState]:
    sample = interpolate_weather_sample(timeline, when)
    samples = timeline.hourly
    instants = [item.timestamp.astimezone(datetime_timezone.utc) for item in samples]
    target = when.astimezone(datetime_timezone.utc)
    if target <= instants[0] or target >= instants[-1]:
        return sample, derive_weather_state(sample, view_azimuth)
    right_index = bisect_right(instants, target)
    left, right = samples[right_index - 1], samples[right_index]
    span = max(1.0, (instants[right_index] - instants[right_index - 1]).total_seconds())
    amount = _clamp((target - instants[right_index - 1]).total_seconds() / span)
    left_state = derive_weather_state(left, view_azimuth)
    right_state = derive_weather_state(right, view_azimuth)
    if left_state.precipitation_type == right_state.precipitation_type:
        precipitation_type = left_state.precipitation_type
        precipitation = _lerp(
            left_state.precipitation_intensity,
            right_state.precipitation_intensity,
            amount,
        )
    elif amount < 0.5:
        precipitation_type = left_state.precipitation_type
        precipitation = left_state.precipitation_intensity * (
            1.0 - _smoothstep(amount * 2.0)
        )
    else:
        precipitation_type = right_state.precipitation_type
        precipitation = right_state.precipitation_intensity * _smoothstep(
            (amount - 0.5) * 2.0
        )
    if precipitation <= 0.0005:
        precipitation_type = "none"
        precipitation = 0.0
    fog = _clamp(_lerp(left_state.fog_intensity, right_state.fog_intensity, amount))
    cloud = _clamp(
        _lerp(left_state.cloud_intensity, right_state.cloud_intensity, amount)
    )
    if precipitation_type != "none":
        condition = precipitation_type
    elif fog > 0.12:
        condition = "fog"
    elif cloud > 0.12:
        condition = "cloudy"
    else:
        condition = "clear"
    state = WeatherState(
        condition=condition,
        cloud_intensity=cloud,
        fog_intensity=fog,
        precipitation_type=precipitation_type,
        precipitation_intensity=_clamp(precipitation),
        wind_intensity=_clamp(_lerp(left_state.wind_intensity, right_state.wind_intensity, amount)),
        wind_screen_direction=_clamp(
            _lerp(left_state.wind_screen_direction, right_state.wind_screen_direction, amount), -1.0, 1.0
        ),
        heat_intensity=_clamp(_lerp(left_state.heat_intensity, right_state.heat_intensity, amount)),
        snow_cover=_clamp(_lerp(left_state.snow_cover, right_state.snow_cover, amount)),
    )
    return sample, state


def preset_weather_sample(name: str, timestamp: datetime) -> WeatherSample:
    if name not in WEATHER_PRESETS:
        raise ValueError(f"unsupported weather preset: {name}")
    values = neutral_weather_sample(timestamp).to_mapping()
    presets: Mapping[str, Mapping[str, Any]] = {
        "clear": {},
        "cloudy": {"weather_code": 3, "cloud_cover_percent": 88.0},
        "fog": {"weather_code": 48, "cloud_cover_percent": 72.0, "visibility_m": 450.0},
        "rain-light": {
            "weather_code": 61, "cloud_cover_percent": 74.0,
            "precipitation_mm": 0.8, "rain_mm": 0.8,
            "wind_speed_ms": 5.0, "wind_gusts_ms": 8.0,
        },
        "rain-heavy": {
            "weather_code": 82, "cloud_cover_percent": 100.0,
            "precipitation_mm": 12.0, "rain_mm": 7.0, "showers_mm": 5.0,
            "wind_speed_ms": 14.0, "wind_gusts_ms": 24.0,
        },
        "snow-light": {
            "weather_code": 71, "cloud_cover_percent": 78.0,
            "precipitation_mm": 0.5, "snowfall_cm": 0.5,
            "temperature_c": -2.0, "apparent_temperature_c": -5.0,
            "wind_speed_ms": 4.0,
        },
        "snow-heavy": {
            "weather_code": 86, "cloud_cover_percent": 100.0,
            "precipitation_mm": 7.0, "snowfall_cm": 8.0,
            "snow_depth_m": 0.08, "temperature_c": -6.0,
            "apparent_temperature_c": -12.0, "wind_speed_ms": 12.0,
            "wind_gusts_ms": 20.0,
        },
        "hail-light": {
            "weather_code": 96, "cloud_cover_percent": 92.0,
            "precipitation_mm": 3.0, "rain_mm": 3.0,
            "wind_speed_ms": 10.0, "wind_gusts_ms": 18.0,
        },
        "hail-heavy": {
            "weather_code": 99, "cloud_cover_percent": 100.0,
            "precipitation_mm": 16.0, "rain_mm": 16.0,
            "wind_speed_ms": 18.0, "wind_gusts_ms": 30.0,
        },
        "wind": {"weather_code": 0, "wind_speed_ms": 18.0, "wind_direction_degrees": 270.0, "wind_gusts_ms": 30.0},
        "heat": {"weather_code": 0, "temperature_c": 42.0, "apparent_temperature_c": 42.0, "wind_speed_ms": 2.0},
    }
    values.update(presets[name])
    return WeatherSample.from_mapping(values, getattr(timestamp.tzinfo, "key", None) or "UTC")


class WeatherCache:
    """Bucketed process-safe weather cache with a strict three-hour last-good cutoff."""

    def __init__(
        self,
        path: Optional[Path] = None,
        clock: Callable[[], datetime] = lambda: datetime.now(datetime_timezone.utc),
    ) -> None:
        cache_root = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
        self.path = Path(path) if path is not None else (
            cache_root / "hyprlax" / "pixel-city-dynamic" / "weather-v1.json"
        )
        self.lock_path = self.path.with_name("weather-v1.lock")
        self.clock = clock

    @staticmethod
    def _empty() -> Dict[str, Any]:
        return {"schema_version": WEATHER_CACHE_SCHEMA_VERSION, "locations": {}}

    def _load_unlocked(self) -> Dict[str, Any]:
        if not self.path.exists():
            return self._empty()
        try:
            with self.path.open("r", encoding="utf-8") as handle:
                value = json.load(handle)
            if not isinstance(value, dict) or value.get("schema_version") != WEATHER_CACHE_SCHEMA_VERSION:
                raise ValueError("weather cache schema mismatch")
            if not isinstance(value.get("locations"), dict):
                raise ValueError("weather cache locations must be an object")
            return value
        except (OSError, ValueError, json.JSONDecodeError) as error:
            raise WeatherCacheError(
                f"weather cache is invalid or unreadable: {_bounded_error(error)}"
            ) from error

    def _write_unlocked(self, value: Mapping[str, Any]) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{self.path.name}.", dir=self.path.parent
        )
        temporary = Path(temporary_name)
        try:
            os.fchmod(descriptor, 0o600)
            with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
                json.dump(value, handle, sort_keys=True, separators=(",", ":"))
                handle.write("\n")
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary, self.path)
            try:
                directory_fd = os.open(self.path.parent, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory_fd)
                finally:
                    os.close(directory_fd)
            except OSError:
                pass
        except OSError as error:
            try:
                temporary.unlink()
            except OSError:
                pass
            raise WeatherCacheError(f"failed to write weather cache: {error}") from error

    def _locked(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lock_handle = self.lock_path.open("a+", encoding="utf-8")
        os.chmod(self.lock_path, 0o600)
        fcntl.flock(lock_handle.fileno(), fcntl.LOCK_EX)
        return lock_handle

    @staticmethod
    def _bucket(now: datetime, refresh_seconds: int) -> int:
        return int(now.astimezone(datetime_timezone.utc).timestamp()) // refresh_seconds

    @staticmethod
    def _timeline_from_entry(
        entry: Mapping[str, Any],
        identity: str,
        timezone_name: str,
        latitude: float,
        longitude: float,
    ) -> Optional[WeatherTimeline]:
        success = entry.get("last_success")
        if not isinstance(success, Mapping):
            return None
        try:
            return WeatherTimeline.from_mapping(
                success.get("timeline"),
                identity,
                timezone_name,
                latitude,
                longitude,
            )
        except WeatherError:
            return None

    @staticmethod
    def _usable(
        timeline: Optional[WeatherTimeline], now: datetime
    ) -> Tuple[Optional[WeatherTimeline], Optional[float]]:
        if timeline is None:
            return None, None
        age = max(
            0.0,
            (now.astimezone(datetime_timezone.utc) - timeline.fetched_at).total_seconds(),
        )
        if age > WEATHER_CACHE_MAX_AGE_SECONDS:
            return None, age
        return timeline, age

    def resolve(
        self,
        location: Any,
        refresh_seconds: int,
        fetch: Callable[[datetime], WeatherTimeline],
    ) -> WeatherCacheResult:
        if not MIN_WEATHER_REFRESH_SECONDS <= refresh_seconds <= MAX_WEATHER_REFRESH_SECONDS:
            raise ValueError(
                f"weather refresh must be {MIN_WEATHER_REFRESH_SECONDS}..{MAX_WEATHER_REFRESH_SECONDS} seconds"
            )
        now = self.clock()
        if now.tzinfo is None:
            raise ValueError("weather cache clock must be timezone-aware")
        now = now.astimezone(datetime_timezone.utc)
        identity = _clean_text(location.identity, "location identity", 192)
        timezone_name = _clean_text(location.timezone, "timezone", 128)
        latitude = _number(location.latitude, "latitude", -90.0, 90.0)
        longitude = _number(location.longitude, "longitude", -180.0, 180.0)
        bucket = self._bucket(now, refresh_seconds)
        attempt_token = f"{bucket}:{os.getpid()}:{os.urandom(8).hex()}"

        lock_handle = self._locked()
        try:
            cache = self._load_unlocked()
            entry = cache["locations"].get(identity)
            if not isinstance(entry, dict):
                entry = {"timezone": timezone_name}
                cache["locations"][identity] = entry
            if entry.get("timezone") != timezone_name:
                entry.clear()
                entry["timezone"] = timezone_name
            timeline, age = self._usable(
                self._timeline_from_entry(
                    entry, identity, timezone_name, latitude, longitude
                ),
                now,
            )
            if entry.get("last_attempt_bucket") == bucket:
                error = entry.get("last_error") if isinstance(entry.get("last_error"), str) else None
                if timeline is None:
                    source = "neutral"
                elif error is not None:
                    source = "stale"
                else:
                    source = "cache"
                return WeatherCacheResult(
                    timeline=timeline,
                    source=source,
                    stale=(
                        timeline is None
                        or error is not None
                        or (age is not None and age > refresh_seconds)
                    ),
                    age_seconds=age,
                    attempted=False,
                    error=error,
                )
            entry["last_attempt_bucket"] = bucket
            entry["last_attempt_token"] = attempt_token
            entry["last_attempt_at"] = now.isoformat()
            entry["last_error"] = None
            self._write_unlocked(cache)
        finally:
            fcntl.flock(lock_handle.fileno(), fcntl.LOCK_UN)
            lock_handle.close()

        try:
            fetched = fetch(now)
            if fetched.location_identity != identity or fetched.timezone != timezone_name:
                raise WeatherError("fetched weather does not match resolved location")
            normalized = WeatherTimeline.from_mapping(
                fetched.to_mapping(),
                identity,
                timezone_name,
                latitude,
                longitude,
            )
            lock_handle = self._locked()
            try:
                cache = self._load_unlocked()
                entry = cache["locations"].setdefault(identity, {"timezone": timezone_name})
                current, age = self._usable(
                    self._timeline_from_entry(
                        entry, identity, timezone_name, latitude, longitude
                    ),
                    now,
                )
                reservation_matches = (
                    entry.get("last_attempt_bucket") == bucket
                    and entry.get("last_attempt_token") == attempt_token
                )
                newer_success = (
                    current is not None
                    and current.fetched_at > normalized.fetched_at
                )
                if reservation_matches and not newer_success:
                    entry["last_error"] = None
                    entry["last_success"] = {"timeline": normalized.to_mapping()}
                    self._write_unlocked(cache)
                    committed = True
                else:
                    committed = False
            finally:
                fcntl.flock(lock_handle.fileno(), fcntl.LOCK_UN)
                lock_handle.close()
            if committed:
                return WeatherCacheResult(normalized, "fresh", False, 0.0, True)
            return WeatherCacheResult(
                current,
                "cache" if current is not None else "neutral",
                current is None or (age is not None and age > refresh_seconds),
                age,
                True,
            )
        except (WeatherCacheError, WeatherError, HTTPError, URLError, OSError, ValueError) as error:
            bounded = _bounded_error(error)
            lock_handle = self._locked()
            try:
                cache = self._load_unlocked()
                entry = cache["locations"].setdefault(identity, {"timezone": timezone_name})
                reservation_matches = (
                    entry.get("last_attempt_bucket") == bucket
                    and entry.get("last_attempt_token") == attempt_token
                )
                if reservation_matches:
                    entry["last_error"] = bounded
                timeline, age = self._usable(
                    self._timeline_from_entry(
                        entry, identity, timezone_name, latitude, longitude
                    ),
                    now,
                )
                if reservation_matches:
                    self._write_unlocked(cache)
            finally:
                fcntl.flock(lock_handle.fileno(), fcntl.LOCK_UN)
                lock_handle.close()
            return WeatherCacheResult(
                timeline=timeline,
                source="stale" if timeline is not None else "neutral",
                stale=True,
                age_seconds=age,
                attempted=True,
                error=(bounded if reservation_matches else None),
            )


@dataclass(frozen=True)
class DecodedGIF:
    width: int
    height: int
    frames: Tuple[bytes, ...]
    delays_cs: Tuple[int, ...]
    loop_count: Optional[int]
    transparent_index: Optional[int]
    palette: Tuple[Tuple[int, int, int], ...]


def _palette_size(palette: Sequence[Tuple[int, int, int]]) -> Tuple[int, int]:
    if not 2 <= len(palette) <= 256:
        raise ValueError("GIF palette must contain 2..256 colors")
    size = 2
    bits = 1
    while size < len(palette):
        size *= 2
        bits += 1
    return size, bits


def _gif_lzw_encode(indexes: bytes, minimum_code_size: int) -> bytes:
    if not indexes:
        raise ValueError("GIF frame may not be empty")
    clear_code = 1 << minimum_code_size
    end_code = clear_code + 1
    dictionary: Dict[Tuple[int, int], int] = {}
    next_code = end_code + 1
    code_size = minimum_code_size + 1
    bit_buffer = 0
    bit_count = 0
    output = bytearray()

    def emit(code: int) -> None:
        nonlocal bit_buffer, bit_count
        bit_buffer |= code << bit_count
        bit_count += code_size
        while bit_count >= 8:
            output.append(bit_buffer & 0xFF)
            bit_buffer >>= 8
            bit_count -= 8

    emit(clear_code)
    prefix = indexes[0]
    for suffix in indexes[1:]:
        key = (prefix, suffix)
        existing = dictionary.get(key)
        if existing is not None:
            prefix = existing
            continue
        emit(prefix)
        if next_code < 4096:
            dictionary[key] = next_code
            next_code += 1
            # Decoder adds this entry after reading the next emitted code, so
            # encoder width advances one dictionary slot later.
            if next_code > (1 << code_size) and code_size < 12:
                code_size += 1
        else:
            emit(clear_code)
            dictionary.clear()
            next_code = end_code + 1
            code_size = minimum_code_size + 1
        prefix = suffix
    emit(prefix)
    emit(end_code)
    if bit_count:
        output.append(bit_buffer & 0xFF)
    return bytes(output)


def _gif_sub_blocks(payload: bytes) -> bytes:
    result = bytearray()
    for offset in range(0, len(payload), 255):
        block = payload[offset : offset + 255]
        result.append(len(block))
        result.extend(block)
    result.append(0)
    return bytes(result)


def encode_palette_gif(
    width: int,
    height: int,
    frames: Sequence[bytes],
    palette: Sequence[Tuple[int, int, int]],
    delay_cs: int = 12,
    loop_count: int = 0,
    transparent_index: int = 0,
) -> bytes:
    if not 1 <= width <= 65535 or not 1 <= height <= 65535:
        raise ValueError("GIF dimensions must be 1..65535")
    if width * height > MAX_GIF_PIXELS:
        raise ValueError(f"GIF may contain at most {MAX_GIF_PIXELS} pixels")
    if not 1 <= len(frames) <= MAX_GIF_FRAMES:
        raise ValueError(f"GIF must contain 1..{MAX_GIF_FRAMES} frames")
    if not 1 <= delay_cs <= 65535:
        raise ValueError("GIF delay must be positive")
    if not 0 <= loop_count <= 65535:
        raise ValueError("GIF loop count must be 0..65535")
    table_size, bits = _palette_size(palette)
    if not 0 <= transparent_index < len(palette):
        raise ValueError("transparent GIF index is outside palette")
    normalized_palette = []
    for color in palette:
        if len(color) != 3 or any(not isinstance(channel, int) or not 0 <= channel <= 255 for channel in color):
            raise ValueError("GIF colors must be integer RGB triples")
        normalized_palette.append(tuple(color))
    normalized_palette.extend([(0, 0, 0)] * (table_size - len(normalized_palette)))
    expected = width * height
    for frame in frames:
        if len(frame) != expected:
            raise ValueError(f"GIF frame must contain exactly {expected} palette indexes")
        if frame and max(frame) >= len(palette):
            raise ValueError("GIF frame references color outside palette")

    packed = 0x80 | ((bits - 1) << 4) | (bits - 1)
    output = bytearray(b"GIF89a")
    output.extend(struct.pack("<HHBBB", width, height, packed, transparent_index, 0))
    for red, green, blue in normalized_palette:
        output.extend((red, green, blue))
    output.extend(b"\x21\xff\x0bNETSCAPE2.0\x03\x01")
    output.extend(struct.pack("<H", loop_count))
    output.append(0)
    minimum_code_size = max(2, bits)
    for frame in frames:
        output.extend(b"\x21\xf9\x04\x09")
        output.extend(struct.pack("<H", delay_cs))
        output.extend(bytes((transparent_index, 0)))
        output.append(0x2C)
        output.extend(struct.pack("<HHHHB", 0, 0, width, height, 0))
        output.append(minimum_code_size)
        output.extend(_gif_sub_blocks(_gif_lzw_encode(bytes(frame), minimum_code_size)))
    output.append(0x3B)
    return bytes(output)


def _read_sub_blocks(data: bytes, offset: int) -> Tuple[bytes, int]:
    output = bytearray()
    while True:
        if offset >= len(data):
            raise ValueError("truncated GIF sub-block")
        length = data[offset]
        offset += 1
        if length == 0:
            return bytes(output), offset
        if offset + length > len(data):
            raise ValueError("truncated GIF sub-block payload")
        output.extend(data[offset : offset + length])
        offset += length


def _gif_lzw_decode(payload: bytes, minimum_code_size: int, expected: int) -> bytes:
    if not 2 <= minimum_code_size <= 8:
        raise ValueError("unsupported GIF LZW minimum code size")
    clear_code = 1 << minimum_code_size
    end_code = clear_code + 1
    bit_offset = 0
    code_size = minimum_code_size + 1
    dictionary: Dict[int, bytes] = {}
    next_code = end_code + 1
    previous: Optional[bytes] = None
    output = bytearray()

    def reset() -> None:
        nonlocal dictionary, next_code, code_size, previous
        dictionary = {index: bytes((index,)) for index in range(clear_code)}
        next_code = end_code + 1
        code_size = minimum_code_size + 1
        previous = None

    def read_code() -> int:
        nonlocal bit_offset
        if bit_offset + code_size > len(payload) * 8:
            raise ValueError("truncated GIF LZW stream")
        value = 0
        for bit in range(code_size):
            absolute = bit_offset + bit
            value |= ((payload[absolute // 8] >> (absolute % 8)) & 1) << bit
        bit_offset += code_size
        return value

    reset()
    while True:
        code = read_code()
        if code == clear_code:
            reset()
            continue
        if code == end_code:
            break
        if code in dictionary:
            entry = dictionary[code]
        elif previous is not None and code == next_code:
            entry = previous + previous[:1]
        else:
            raise ValueError("invalid GIF LZW code")
        output.extend(entry)
        if previous is not None and next_code < 4096:
            dictionary[next_code] = previous + entry[:1]
            next_code += 1
            if next_code == (1 << code_size) and code_size < 12:
                code_size += 1
        previous = entry
        if len(output) > expected:
            raise ValueError("GIF frame decoded beyond dimensions")
    if len(output) != expected:
        raise ValueError("GIF frame does not fill dimensions")
    return bytes(output)


def decode_palette_gif(source: Any) -> DecodedGIF:
    data = Path(source).read_bytes() if isinstance(source, (str, os.PathLike, Path)) else bytes(source)
    if len(data) > MAX_GIF_BYTES:
        raise ValueError(f"GIF exceeds {MAX_GIF_BYTES} bytes")
    if len(data) < 13 or data[:6] not in (b"GIF87a", b"GIF89a"):
        raise ValueError("invalid GIF signature")
    width, height, packed, _, _ = struct.unpack("<HHBBB", data[6:13])
    if width <= 0 or height <= 0:
        raise ValueError("invalid GIF dimensions")
    if width * height > MAX_GIF_PIXELS:
        raise ValueError(f"GIF may contain at most {MAX_GIF_PIXELS} pixels")
    offset = 13
    palette: Tuple[Tuple[int, int, int], ...] = ()
    if packed & 0x80:
        count = 1 << ((packed & 0x07) + 1)
        end = offset + count * 3
        if end > len(data):
            raise ValueError("truncated GIF global color table")
        palette = tuple(tuple(data[index : index + 3]) for index in range(offset, end, 3))
        offset = end
    if not palette:
        raise ValueError("GIF requires a global color table")
    frames = []
    delays = []
    loop_count: Optional[int] = None
    pending_delay = 0
    pending_transparent: Optional[int] = None
    transparent: Optional[int] = None
    saw_trailer = False
    while offset < len(data):
        marker = data[offset]
        offset += 1
        if marker == 0x3B:
            saw_trailer = True
            break
        if marker == 0x21:
            if offset >= len(data):
                raise ValueError("truncated GIF extension")
            label = data[offset]
            offset += 1
            if label == 0xF9:
                if offset + 6 > len(data) or data[offset] != 4:
                    raise ValueError("invalid GIF graphics control extension")
                gce_packed = data[offset + 1]
                pending_delay = struct.unpack("<H", data[offset + 2 : offset + 4])[0]
                pending_transparent = data[offset + 4] if gce_packed & 1 else None
                offset += 6
            elif label == 0xFF:
                if offset >= len(data):
                    raise ValueError("truncated GIF application extension")
                name_length = data[offset]
                offset += 1
                name_end = offset + name_length
                if name_end > len(data):
                    raise ValueError("truncated GIF application identifier")
                application = data[offset:name_end]
                offset = name_end
                application_data, offset = _read_sub_blocks(data, offset)
                if application == b"NETSCAPE2.0" and len(application_data) >= 3 and application_data[0] == 1:
                    loop_count = struct.unpack("<H", application_data[1:3])[0]
            else:
                _, offset = _read_sub_blocks(data, offset)
            continue
        if marker != 0x2C:
            raise ValueError(f"unsupported GIF block marker 0x{marker:02x}")
        if offset + 9 > len(data):
            raise ValueError("truncated GIF image descriptor")
        left, top, frame_width, frame_height, image_packed = struct.unpack(
            "<HHHHB", data[offset : offset + 9]
        )
        offset += 9
        if (left, top, frame_width, frame_height) != (0, 0, width, height):
            raise ValueError("GIF frames must cover the full logical screen")
        if image_packed & 0xC0:
            raise ValueError("local palettes and interlaced GIF frames are unsupported")
        if offset >= len(data):
            raise ValueError("truncated GIF image data")
        minimum_code_size = data[offset]
        offset += 1
        compressed, offset = _read_sub_blocks(data, offset)
        frames.append(_gif_lzw_decode(compressed, minimum_code_size, width * height))
        delays.append(pending_delay)
        if pending_transparent is not None:
            if transparent is not None and transparent != pending_transparent:
                raise ValueError("GIF frames use inconsistent transparency indexes")
            transparent = pending_transparent
        pending_delay = 0
        pending_transparent = None
    if not frames:
        raise ValueError("GIF contains no frames")
    if not saw_trailer or offset != len(data):
        raise ValueError("GIF must end with one trailer and no trailing data")
    return DecodedGIF(
        width=width,
        height=height,
        frames=tuple(frames),
        delays_cs=tuple(delays),
        loop_count=loop_count,
        transparent_index=transparent,
        palette=palette,
    )


def validate_gif(source: Any, width: int = PIXEL_WIDTH, height: int = PIXEL_HEIGHT) -> DecodedGIF:
    decoded = decode_palette_gif(source)
    if (decoded.width, decoded.height) != (width, height):
        raise ValueError(f"GIF must be {width}x{height}")
    if not 1 <= len(decoded.frames) <= MAX_GIF_FRAMES:
        raise ValueError(f"GIF must contain 1..{MAX_GIF_FRAMES} frames")
    if any(delay <= 0 for delay in decoded.delays_cs):
        raise ValueError("GIF frame delays must be positive")
    if decoded.transparent_index is None:
        raise ValueError("GIF must declare transparency")
    if decoded.loop_count != 0:
        raise ValueError("GIF must loop forever")
    return decoded


def quantized_weather_signature(state: WeatherState) -> Tuple[Any, ...]:
    return (
        state.condition,
        state.precipitation_type,
        round(state.cloud_intensity * 10.0),
        round(state.fog_intensity * 10.0),
        round(state.precipitation_intensity * 8.0),
        round(state.wind_intensity * 8.0),
        round(state.wind_screen_direction * 8.0),
        round(state.heat_intensity * 10.0),
    )


def _seed(layer_name: str, state: WeatherState) -> int:
    signature = quantized_weather_signature(state)
    if layer_name == "weather-cloud":
        stable = ("cloud",)
    elif layer_name.startswith("weather-fog"):
        stable = ("fog",)
    elif layer_name.startswith("weather-precip"):
        stable = (signature[1],)
    else:
        stable = signature
    payload = repr((layer_name, stable)).encode("ascii")
    return zlib.crc32(payload) & 0xFFFFFFFF


def _put(indexes: bytearray, width: int, height: int, x: int, y: int, color: int) -> None:
    if 0 <= x < width and 0 <= y < height:
        indexes[y * width + x] = color


def _blob(
    indexes: bytearray,
    width: int,
    height: int,
    center_x: int,
    center_y: int,
    radius_x: int,
    radius_y: int,
    color: int,
) -> None:
    for y in range(center_y - radius_y, center_y + radius_y + 1):
        dy = (y - center_y) / max(1, radius_y)
        for x in range(center_x - radius_x, center_x + radius_x + 1):
            dx = (x - center_x) / max(1, radius_x)
            if dx * dx + dy * dy <= 1.0:
                _put(indexes, width, height, x, y, color)


def _transparent_frames(width: int, height: int, count: int = 1) -> Tuple[bytes, ...]:
    return tuple(bytes(width * height) for _ in range(count))


def _render_cloud_frames(
    state: WeatherState, width: int, height: int
) -> Tuple[Tuple[bytes, ...], Tuple[Tuple[int, int, int], ...]]:
    palette = ((0, 0, 0), (176, 190, 204), (118, 135, 154), (210, 216, 221))
    if state.cloud_intensity <= 0.0005:
        return _transparent_frames(width, height), palette
    rng = random.Random(_seed("weather-cloud", state))
    count = 4 + round(20 * state.cloud_intensity)
    clouds = [
        (
            rng.randrange(-30, width + 30),
            rng.randrange(28, max(29, min(height - 40, 150))),
            rng.randrange(12, 34),
            rng.randrange(4, 11),
            rng.randrange(1, 4),
        )
        for _ in range(count)
    ]
    frames = []
    drift = (2.0 + 9.0 * state.wind_intensity) * (
        state.wind_screen_direction if abs(state.wind_screen_direction) > 0.08 else 0.25
    )
    for frame_index in range(GIF_FRAME_COUNT):
        indexes = bytearray(width * height)
        for base_x, base_y, radius_x, radius_y, color in clouds:
            travel = round(drift * frame_index)
            x = ((base_x + travel + radius_x) % (width + radius_x * 2)) - radius_x
            _blob(indexes, width, height, x, base_y, radius_x, radius_y, color)
            _blob(indexes, width, height, x - radius_x // 2, base_y + 2, radius_x // 2, radius_y, color)
        frames.append(bytes(indexes))
    return tuple(frames), palette


def _render_fog_frames(
    layer_name: str, state: WeatherState, width: int, height: int
) -> Tuple[Tuple[bytes, ...], Tuple[Tuple[int, int, int], ...]]:
    palette = ((0, 0, 0), (150, 164, 176), (183, 193, 200), (112, 128, 142))
    if state.fog_intensity <= 0.0005:
        return _transparent_frames(width, height), palette
    front = layer_name.endswith("front")
    rng = random.Random(_seed(layer_name, state))
    band_count = 3 + round(6 * state.fog_intensity)
    bands = [
        (
            rng.randrange(0, width),
            rng.randrange(height // 4, height - 12),
            rng.randrange(5, 13) if front else rng.randrange(3, 9),
            rng.randrange(1, 4),
        )
        for _ in range(band_count)
    ]
    frames = []
    direction = state.wind_screen_direction if abs(state.wind_screen_direction) > 0.08 else 0.18
    speed = (5.0 if front else 2.5) * (0.35 + state.wind_intensity) * direction
    for frame_index in range(GIF_FRAME_COUNT):
        indexes = bytearray(width * height)
        for phase, center_y, half_height, color in bands:
            shift = round(speed * frame_index) + phase
            for y in range(max(0, center_y - half_height), min(height, center_y + half_height + 1)):
                for x in range(width):
                    if ((x + shift + y * 3) % 17) < 11:
                        _put(indexes, width, height, x, y, color)
        frames.append(bytes(indexes))
    return tuple(frames), palette


def _draw_precipitation(
    indexes: bytearray,
    width: int,
    height: int,
    kind: str,
    x: int,
    y: int,
    front: bool,
    direction: float,
) -> None:
    if kind == "rain":
        length = 7 if front else 4
        slant = 1 if direction >= 0.0 else -1
        for offset in range(length):
            _put(indexes, width, height, x + slant * (offset // 3), y + offset, 1)
    elif kind == "snow":
        radius = 2 if front else 1
        for dx, dy in ((0, 0), (-radius, 0), (radius, 0), (0, -radius), (0, radius)):
            _put(indexes, width, height, x + dx, y + dy, 2)
    elif kind == "hail":
        radius = 2 if front else 1
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if dx * dx + dy * dy <= radius * radius + 1:
                    _put(indexes, width, height, x + dx, y + dy, 3)
    elif kind == "debris":
        _put(indexes, width, height, x, y, 4)
        if front:
            _put(indexes, width, height, x + 1, y, 4)


def _render_precipitation_frames(
    layer_name: str, state: WeatherState, width: int, height: int
) -> Tuple[Tuple[bytes, ...], Tuple[Tuple[int, int, int], ...]]:
    palette = (
        (0, 0, 0),
        (132, 188, 232),
        (238, 246, 255),
        (255, 255, 242),
        (183, 147, 91),
    )
    front = layer_name.endswith("front")
    kind = state.precipitation_type
    intensity = state.precipitation_intensity
    if kind == "none" and front and state.wind_intensity >= 0.35:
        kind = "debris"
        intensity = 0.10 + 0.35 * state.wind_intensity
    if kind == "none" or intensity <= 0.0005:
        return _transparent_frames(width, height), palette
    rng = random.Random(_seed(layer_name, state))
    maximum = 230 if front else 150
    count = max(1, round(maximum * intensity))
    horizontal_margin = min(80, max(0, width // 4))
    minimum_x = horizontal_margin
    maximum_x = max(minimum_x + 1, width - horizontal_margin)
    particles = [
        (rng.randrange(minimum_x, maximum_x), rng.randrange(height))
        for _ in range(count)
    ]
    speed = {
        "rain": 14 if front else 9,
        "snow": 4 if front else 2,
        "hail": 18 if front else 12,
        "debris": 2,
    }[kind]
    gust_scale = 2.0 + 12.0 * state.wind_intensity
    lateral = gust_scale * state.wind_screen_direction
    frames = []
    for frame_index in range(GIF_FRAME_COUNT):
        indexes = bytearray(width * height)
        for base_x, base_y in particles:
            x = round(base_x + lateral * frame_index) % width
            y = round(base_y + speed * frame_index) % height
            _draw_precipitation(
                indexes, width, height, kind, x, y, front, state.wind_screen_direction
            )
        frames.append(bytes(indexes))
    return tuple(frames), palette


def render_weather_gif(
    layer_name: str,
    state: WeatherState,
    width: int = PIXEL_WIDTH,
    height: int = PIXEL_HEIGHT,
) -> bytes:
    if layer_name == "weather-cloud":
        frames, palette = _render_cloud_frames(state, width, height)
    elif layer_name in ("weather-fog-back", "weather-fog-front"):
        frames, palette = _render_fog_frames(layer_name, state, width, height)
    elif layer_name in ("weather-precip-back", "weather-precip-front"):
        frames, palette = _render_precipitation_frames(layer_name, state, width, height)
    else:
        raise ValueError(f"unknown managed weather layer: {layer_name}")
    payload = encode_palette_gif(width, height, frames, palette)
    validate_gif(payload, width, height)
    return payload


def _base_palette() -> Tuple[Tuple[int, int, int], ...]:
    colors = [(0, 0, 0)]
    levels = (0, 51, 102, 153, 204, 255)
    for red in levels:
        for green in levels:
            for blue in levels:
                colors.append((red, green, blue))
    return tuple(colors)


def _rgba_to_palette_indexes(source_rgba: bytes, width: int, height: int) -> bytes:
    if len(source_rgba) != width * height * 4:
        raise ValueError("heat source must be full RGBA image")
    indexes = bytearray(width * height)
    for pixel in range(width * height):
        red, green, blue, alpha = source_rgba[pixel * 4 : pixel * 4 + 4]
        if alpha < 64:
            indexes[pixel] = 0
            continue
        red_bin = min(5, (red + 25) // 51)
        green_bin = min(5, (green + 25) // 51)
        blue_bin = min(5, (blue + 25) // 51)
        indexes[pixel] = 1 + red_bin * 36 + green_bin * 6 + blue_bin
    return bytes(indexes)


def render_heat_gif(
    source_rgba: bytes,
    heat_intensity: float,
    width: int = PIXEL_WIDTH,
    height: int = PIXEL_HEIGHT,
) -> bytes:
    intensity = _clamp(float(heat_intensity))
    source = _rgba_to_palette_indexes(source_rgba, width, height)
    if intensity <= 0.0005:
        frames = (source,)
    else:
        amplitude = max(1, round(4.0 * intensity))
        generated = []
        for frame_index in range(GIF_FRAME_COUNT):
            frame = bytearray(width * height)
            for y in range(height):
                displacement = round(
                    amplitude * math.sin(2.0 * math.pi * (y / 28.0 + frame_index / GIF_FRAME_COUNT))
                )
                source_start = y * width
                target_start = y * width
                for x in range(width):
                    frame[target_start + x] = source[source_start + ((x + displacement) % width)]
            generated.append(bytes(frame))
        frames = tuple(generated)
    payload = encode_palette_gif(width, height, frames, _base_palette(), delay_cs=10)
    validate_gif(payload, width, height)
    return payload
