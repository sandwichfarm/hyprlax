#!/usr/bin/env python3
"""Daily astronomy, lighting, generated assets, and IPC for dynamic Pixel City."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field, replace
from datetime import date, datetime, time, timedelta, timezone as datetime_timezone
import fcntl
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time as time_module
from typing import Any, Callable, Dict, Mapping, Optional, Tuple
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
import zlib


_EXAMPLE_MODULE_DIRECTORY = Path(__file__).resolve().parent
if str(_EXAMPLE_MODULE_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(_EXAMPLE_MODULE_DIRECTORY))
import weather as weather_module


CACHE_SCHEMA_VERSION = 1
MAX_RESPONSE_BYTES = 262144
HTTP_TIMEOUT_SECONDS = 10
MAX_ERROR_LENGTH = 512
DEFAULT_DEMO_SECONDS = 60.0
DEFAULT_DEMO_STEP_SECONDS = 1.0
SECONDS_PER_DAY = 86400.0
USER_AGENT = "hyprlax-pixel-city-dynamic/1 (+https://github.com/sandwichfarm/hyprlax)"
IP_API_URL = (
    "http://ip-api.com/json/"
    "?fields=status,message,lat,lon,timezone,city,regionName,country,countryCode"
)
ASTRONOMY_API_URL = "https://api.sunrise-sunset.org/v2"
SUN_STATUSES = frozenset(("normal", "midnight_sun", "polar_night"))
MOON_PHASES = frozenset(
    (
        "New Moon",
        "Waxing Crescent",
        "First Quarter",
        "Waxing Gibbous",
        "Full Moon",
        "Waning Gibbous",
        "Last Quarter",
        "Waning Crescent",
    )
)
EVENT_FIELDS = (
    "civil_twilight_begin",
    "sunrise",
    "solar_noon",
    "sunset",
    "civil_twilight_end",
    "moonrise",
    "moonset",
)


class CacheError(RuntimeError):
    """The on-disk cache could not be read or written safely."""


class ProviderError(RuntimeError):
    """A provider response or request did not satisfy its contract."""


def _clean_text(value: Any, name: str, maximum: int = 160) -> str:
    if not isinstance(value, str):
        raise ProviderError(f"{name} must be a string")
    clean = "".join(char for char in value.strip() if char >= " " and char != "\x7f")
    if not clean or len(clean) > maximum:
        raise ProviderError(f"{name} must contain 1..{maximum} safe characters")
    return clean


def _optional_text(value: Any, name: str, maximum: int = 160) -> str:
    if value in (None, ""):
        return ""
    return _clean_text(value, name, maximum)


def _bounded_error(error: BaseException) -> str:
    clean = "".join(char for char in str(error) if char >= " " and char != "\x7f")
    return clean[:MAX_ERROR_LENGTH] or error.__class__.__name__


def _number(value: Any, name: str, minimum: float, maximum: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ProviderError(f"{name} must be numeric")
    result = float(value)
    if not math.isfinite(result) or result < minimum or result > maximum:
        raise ProviderError(f"{name} must be in {minimum}..{maximum}")
    return result


def _zone(name: Any) -> ZoneInfo:
    zone_name = _clean_text(name, "timezone", 128)
    try:
        return ZoneInfo(zone_name)
    except (ValueError, ZoneInfoNotFoundError) as error:
        raise ProviderError(f"unknown IANA timezone: {zone_name}") from error


def _iso_datetime(value: Any, name: str, allow_null: bool = True) -> Optional[datetime]:
    if value is None and allow_null:
        return None
    text = _clean_text(value, name, 64)
    try:
        result = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError as error:
        raise ProviderError(f"{name} must be an ISO-8601 timestamp") from error
    if result.tzinfo is None:
        raise ProviderError(f"{name} must include a UTC offset")
    return result


def _iso_or_none(value: Optional[datetime]) -> Optional[str]:
    return value.isoformat() if value is not None else None


@dataclass(frozen=True)
class Location:
    latitude: float
    longitude: float
    timezone: str
    city: str = ""
    region: str = ""
    country: str = ""
    country_code: str = ""
    source: str = "ip-api"

    @property
    def identity(self) -> str:
        return f"{self.latitude:.4f},{self.longitude:.4f},{self.timezone}"

    @classmethod
    def from_mapping(cls, value: Mapping[str, Any], source: str = "cache") -> "Location":
        if not isinstance(value, Mapping):
            raise ProviderError("location must be an object")
        timezone_name = _clean_text(value.get("timezone"), "timezone", 128)
        _zone(timezone_name)
        code = _optional_text(value.get("country_code", ""), "country_code", 8)
        return cls(
            latitude=_number(value.get("latitude"), "latitude", -90.0, 90.0),
            longitude=_number(value.get("longitude"), "longitude", -180.0, 180.0),
            timezone=timezone_name,
            city=_optional_text(value.get("city", ""), "city"),
            region=_optional_text(value.get("region", ""), "region"),
            country=_optional_text(value.get("country", ""), "country"),
            country_code=code.upper(),
            source=source,
        )

    def to_mapping(self) -> Dict[str, Any]:
        return {
            "latitude": self.latitude,
            "longitude": self.longitude,
            "timezone": self.timezone,
            "city": self.city,
            "region": self.region,
            "country": self.country,
            "country_code": self.country_code,
        }


@dataclass(frozen=True)
class Astronomy:
    day: date
    timezone: str
    sun_status: str
    civil_twilight_begin: Optional[datetime]
    sunrise: Optional[datetime]
    solar_noon: Optional[datetime]
    sunset: Optional[datetime]
    civil_twilight_end: Optional[datetime]
    moonrise: Optional[datetime]
    moonset: Optional[datetime]
    moon_phase: str
    moon_illumination: float
    solar_position: Mapping[str, Optional[float]] = field(default_factory=dict)
    source: str = "sunrise-sunset-v2"

    @classmethod
    def from_mapping(
        cls,
        value: Mapping[str, Any],
        expected_day: date,
        expected_timezone: Optional[str] = None,
        source: str = "cache",
    ) -> "Astronomy":
        if not isinstance(value, Mapping):
            raise ProviderError("astronomy must be an object")
        try:
            result_day = date.fromisoformat(_clean_text(value.get("date"), "date", 10))
        except ValueError as error:
            raise ProviderError("date must use YYYY-MM-DD") from error
        if result_day != expected_day:
            raise ProviderError("astronomy date does not match the requested date")
        timezone_name = _clean_text(value.get("timezone"), "timezone", 128)
        _zone(timezone_name)
        if expected_timezone is not None and timezone_name != expected_timezone:
            raise ProviderError("astronomy timezone does not match the resolved location")
        status = _clean_text(value.get("sun_status"), "sun_status", 32)
        if status not in SUN_STATUSES:
            raise ProviderError(f"unknown sun_status: {status}")
        phase = _clean_text(value.get("moon_phase"), "moon_phase", 32)
        if phase not in MOON_PHASES:
            raise ProviderError(f"unknown moon phase: {phase}")
        events = {
            name: _iso_datetime(value.get(name), name, allow_null=True) for name in EVENT_FIELDS
        }
        raw_position = value.get("solar_position", {})
        if raw_position is None:
            raw_position = {}
        if not isinstance(raw_position, Mapping):
            raise ProviderError("solar_position must be an object")
        position: Dict[str, Optional[float]] = {}
        for name in ("sunrise_azimuth", "sunset_azimuth", "solar_noon_azimuth"):
            raw = raw_position.get(name)
            position[name] = None if raw is None else _number(raw, name, 0.0, 360.0)
        raw_altitude = raw_position.get("solar_noon_altitude")
        position["solar_noon_altitude"] = (
            None
            if raw_altitude is None
            else _number(raw_altitude, "solar_noon_altitude", -90.0, 90.0)
        )
        return cls(
            day=result_day,
            timezone=timezone_name,
            sun_status=status,
            moon_phase=phase,
            moon_illumination=_number(
                value.get("moon_illumination"), "moon_illumination", 0.0, 100.0
            ),
            solar_position=position,
            source=source,
            **events,
        )

    def to_mapping(self) -> Dict[str, Any]:
        result: Dict[str, Any] = {
            "date": self.day.isoformat(),
            "timezone": self.timezone,
            "sun_status": self.sun_status,
            "moon_phase": self.moon_phase,
            "moon_illumination": self.moon_illumination,
            "solar_position": dict(self.solar_position),
        }
        for name in EVENT_FIELDS:
            result[name] = _iso_or_none(getattr(self, name))
        return result


@dataclass(frozen=True)
class CacheResult:
    data: Optional[Mapping[str, Any]]
    source: str
    stale: bool
    attempted: bool
    error: Optional[str] = None


@dataclass(frozen=True)
class DailyFacts:
    location: Location
    astronomy: Astronomy
    location_source: str
    astronomy_source: str
    stale: bool
    errors: Tuple[str, ...] = ()


@dataclass(frozen=True)
class WeatherFacts:
    timeline: Optional[weather_module.WeatherTimeline]
    sample: weather_module.WeatherSample
    state: weather_module.WeatherState
    source: str
    stale: bool
    age_seconds: Optional[float]
    errors: Tuple[str, ...] = ()


@dataclass(frozen=True)
class LayerLook:
    tint_rgb: Tuple[float, float, float]
    tint_strength: float
    opacity: float
    blur: float


@dataclass(frozen=True)
class SceneState:
    phase: str
    phase_blend: float
    ambient_brightness: float
    saturation_impression: float
    stars_opacity: float
    city_light_intensity: float
    layer_looks: Mapping[str, LayerLook]
    sun_visible: bool
    sun_progress: float
    sun_x: float
    sun_y: float
    sun_opacity: float
    solar_elevation: float
    moon_visible: bool
    moon_progress: float
    moon_x: float
    moon_y: float
    moon_opacity: float
    moon_phase: str
    moon_illumination: float
    lunar_fill: float


@dataclass(frozen=True)
class _LightingPreset:
    name: str
    ambient: float
    saturation_impression: float
    stars: float
    city_lights: float
    tint_rgb: Tuple[float, float, float]
    strengths: Tuple[float, float, float, float, float, float]
    opacities: Tuple[float, float, float, float, float, float]


_BASE_BLURS = (0.0, 2.0, 1.1, 0.3, 0.0, 0.0)
LIGHTING_PRESETS: Mapping[str, _LightingPreset] = {
    "night": _LightingPreset(
        "night",
        0.18,
        0.55,
        1.0,
        1.0,
        (0.28, 0.38, 0.62),
        (0.72, 0.68, 0.62, 0.56, 0.50, 0.44),
        (1.00, 1.00, 0.96, 0.94, 0.92, 0.90),
    ),
    "sunrise": _LightingPreset(
        "sunrise",
        0.48,
        0.82,
        0.25,
        0.72,
        (1.00, 0.52, 0.30),
        (0.62, 0.55, 0.50, 0.42, 0.35, 0.28),
        (1.00, 0.94, 0.96, 0.98, 1.00, 1.00),
    ),
    "morning": _LightingPreset(
        "morning",
        0.78,
        0.95,
        0.02,
        0.20,
        (1.00, 0.82, 0.64),
        (0.32, 0.28, 0.24, 0.20, 0.15, 0.10),
        (1.00, 0.82, 0.96, 1.00, 1.00, 1.00),
    ),
    "high_noon": _LightingPreset(
        "high_noon",
        1.00,
        1.00,
        0.0,
        0.0,
        (1.00, 1.00, 1.00),
        (0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
        (1.00, 0.72, 0.95, 1.00, 1.00, 1.00),
    ),
    "late_afternoon": _LightingPreset(
        "late_afternoon",
        0.74,
        0.92,
        0.02,
        0.22,
        (1.00, 0.68, 0.40),
        (0.38, 0.34, 0.29, 0.23, 0.18, 0.12),
        (1.00, 0.82, 0.96, 1.00, 1.00, 1.00),
    ),
    "sunset": _LightingPreset(
        "sunset",
        0.42,
        0.78,
        0.30,
        0.78,
        (1.00, 0.35, 0.22),
        (0.70, 0.64, 0.57, 0.49, 0.40, 0.32),
        (1.00, 0.96, 0.96, 0.96, 0.98, 1.00),
    ),
}


def _clamp(value: float, minimum: float = 0.0, maximum: float = 1.0) -> float:
    return max(minimum, min(maximum, value))


def _lerp(start: float, end: float, amount: float) -> float:
    return start + (end - start) * amount


def _wrap_degrees(value: float) -> float:
    """Wrap an angle to the signed interval [-180, 180)."""
    return (value + 180.0) % 360.0 - 180.0


def _shortest_angle_lerp(start: float, end: float, amount: float) -> float:
    """Interpolate azimuth through the shortest turn and return 0..360 degrees."""
    return (start + _wrap_degrees(end - start) * amount) % 360.0


def _smoothstep(amount: float) -> float:
    bounded = _clamp(amount)
    return bounded * bounded * (3.0 - 2.0 * bounded)


def _seconds_on_day(value: datetime, requested_day: date, zone: ZoneInfo) -> float:
    local = value.astimezone(zone)
    midnight = datetime.combine(requested_day, time.min, zone)
    return (local - midnight).total_seconds()


def _solar_timeline(astronomy: Astronomy) -> Tuple[Tuple[float, _LightingPreset], ...]:
    zone = _zone(astronomy.timezone)
    fallback = neutral_astronomy(astronomy.day, astronomy.timezone)
    dawn = astronomy.civil_twilight_begin or fallback.civil_twilight_begin
    sunrise = astronomy.sunrise or fallback.sunrise
    noon = astronomy.solar_noon or fallback.solar_noon
    sunset = astronomy.sunset or fallback.sunset
    dusk = astronomy.civil_twilight_end or fallback.civil_twilight_end
    assert dawn is not None and sunrise is not None and noon is not None
    assert sunset is not None and dusk is not None
    values = [
        _seconds_on_day(item, astronomy.day, zone)
        for item in (dawn, sunrise, noon, sunset, dusk)
    ]
    if not all(0.0 <= item <= 86400.0 for item in values) or not all(
        left < right for left, right in zip(values, values[1:])
    ):
        values = [5.5 * 3600.0, 6.0 * 3600.0, 12.0 * 3600.0, 18.0 * 3600.0, 18.5 * 3600.0]
    dawn_s, sunrise_s, noon_s, sunset_s, dusk_s = values
    morning_s = (sunrise_s + noon_s) * 0.5
    afternoon_s = (noon_s + sunset_s) * 0.5
    return (
        (0.0, LIGHTING_PRESETS["night"]),
        (dawn_s, LIGHTING_PRESETS["sunrise"]),
        (morning_s, LIGHTING_PRESETS["morning"]),
        (noon_s, LIGHTING_PRESETS["high_noon"]),
        (afternoon_s, LIGHTING_PRESETS["late_afternoon"]),
        (sunset_s, LIGHTING_PRESETS["sunset"]),
        (dusk_s, LIGHTING_PRESETS["night"]),
        (86400.0, LIGHTING_PRESETS["night"]),
    )


def _interpolate_lighting(
    start: _LightingPreset, end: _LightingPreset, amount: float
) -> Tuple[float, float, float, float, Dict[str, LayerLook]]:
    blend = _smoothstep(amount)
    looks: Dict[str, LayerLook] = {}
    tint = tuple(_lerp(start.tint_rgb[i], end.tint_rgb[i], blend) for i in range(3))
    for index in range(6):
        looks[f"{index + 1}.png"] = LayerLook(
            tint_rgb=tint,
            tint_strength=_lerp(start.strengths[index], end.strengths[index], blend),
            opacity=_lerp(start.opacities[index], end.opacities[index], blend),
            blur=_BASE_BLURS[index],
        )
    return (
        _lerp(start.ambient, end.ambient, blend),
        _lerp(start.saturation_impression, end.saturation_impression, blend),
        _lerp(start.stars, end.stars, blend),
        _lerp(start.city_lights, end.city_lights, blend),
        looks,
    )


def _lighting_at(
    now: datetime, astronomy: Astronomy
) -> Tuple[str, float, float, float, float, float, Dict[str, LayerLook]]:
    if astronomy.sun_status == "polar_night":
        preset = LIGHTING_PRESETS["night"]
        ambient, colorfulness, stars, city, looks = _interpolate_lighting(preset, preset, 0.0)
        return preset.name, 0.0, ambient, colorfulness, stars, city, looks
    if astronomy.sun_status == "midnight_sun":
        preset = LIGHTING_PRESETS["high_noon"]
        ambient, colorfulness, stars, city, looks = _interpolate_lighting(preset, preset, 0.0)
        return preset.name, 0.0, ambient, colorfulness, stars, city, looks
    zone = _zone(astronomy.timezone)
    local = now.astimezone(zone)
    current_seconds = _seconds_on_day(local, astronomy.day, zone)
    if local.date() < astronomy.day:
        current_seconds = 0.0
    elif local.date() > astronomy.day:
        current_seconds = 86400.0
    timeline = _solar_timeline(astronomy)
    for (start_second, start), (end_second, end) in zip(timeline, timeline[1:]):
        if start_second <= current_seconds <= end_second:
            span = max(1.0, end_second - start_second)
            raw = _clamp((current_seconds - start_second) / span)
            phase = end.name if raw >= 1.0 else start.name
            ambient, colorfulness, stars, city, looks = _interpolate_lighting(start, end, raw)
            return phase, raw, ambient, colorfulness, stars, city, looks
    preset = LIGHTING_PRESETS["night"]
    ambient, colorfulness, stars, city, looks = _interpolate_lighting(preset, preset, 0.0)
    return preset.name, 0.0, ambient, colorfulness, stars, city, looks


def _legacy_sun_state(
    local: datetime, astronomy: Astronomy
) -> Tuple[bool, float, float, float, float, float]:
    """Return the original time-only arc used when geographic inputs are unusable."""
    sunrise = astronomy.sunrise or neutral_astronomy(astronomy.day, astronomy.timezone).sunrise
    sunset = astronomy.sunset or neutral_astronomy(astronomy.day, astronomy.timezone).sunset
    zone = _zone(astronomy.timezone)
    assert sunrise is not None and sunset is not None
    sunrise = sunrise.astimezone(zone)
    sunset = sunset.astimezone(zone)
    span = (sunset - sunrise).total_seconds()
    if span <= 0.0:
        return False, 0.0, -0.34, 0.18, 0.0, 0.0
    raw = (local - sunrise).total_seconds() / span
    progress = _clamp(raw)
    elevation = math.sin(math.pi * progress) if 0.0 <= raw <= 1.0 else 0.0
    x = -0.34 + 0.68 * progress
    y = 0.18 - 0.42 * elevation
    horizon = _clamp(min(progress / 0.08, (1.0 - progress) / 0.08))
    visible = 0.0 <= raw <= 1.0
    return visible, progress, x, y, horizon if visible else 0.0, elevation


def _solar_position_number(
    value: Any, minimum: float, maximum: float
) -> Optional[float]:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    number = float(value)
    if not math.isfinite(number) or not minimum <= number <= maximum:
        return None
    return number


def _geographic_sun_position(
    local: datetime,
    astronomy: Astronomy,
    view_azimuth: Optional[float],
) -> Optional[Tuple[float, float, float]]:
    position = astronomy.solar_position
    if not isinstance(position, Mapping):
        return None
    sunrise_azimuth = _solar_position_number(
        position.get("sunrise_azimuth"), 0.0, 360.0
    )
    noon_azimuth = _solar_position_number(
        position.get("solar_noon_azimuth"), 0.0, 360.0
    )
    sunset_azimuth = _solar_position_number(
        position.get("sunset_azimuth"), 0.0, 360.0
    )
    noon_altitude = _solar_position_number(
        position.get("solar_noon_altitude"), 0.0, 90.0
    )
    values = (sunrise_azimuth, noon_azimuth, sunset_azimuth, noon_altitude)
    if any(value is None for value in values):
        return None
    if view_azimuth is not None:
        view_azimuth = _solar_position_number(view_azimuth, 0.0, 360.0)
        if view_azimuth is None:
            return None
    else:
        view_azimuth = noon_azimuth

    sunrise = astronomy.sunrise
    solar_noon = astronomy.solar_noon
    sunset = astronomy.sunset
    if sunrise is None or solar_noon is None or sunset is None:
        return None
    zone = _zone(astronomy.timezone)
    sunrise = sunrise.astimezone(zone)
    solar_noon = solar_noon.astimezone(zone)
    sunset = sunset.astimezone(zone)
    if not sunrise < solar_noon < sunset:
        return None

    if local <= solar_noon:
        span = (solar_noon - sunrise).total_seconds()
        amount = _clamp((local - sunrise).total_seconds() / span)
        altitude = noon_altitude * math.sin(math.pi * amount / 2.0)
        azimuth = _shortest_angle_lerp(sunrise_azimuth, noon_azimuth, amount)
    else:
        span = (sunset - solar_noon).total_seconds()
        amount = _clamp((local - solar_noon).total_seconds() / span)
        altitude = noon_altitude * math.cos(math.pi * amount / 2.0)
        azimuth = _shortest_angle_lerp(noon_azimuth, sunset_azimuth, amount)

    altitude_radians = math.radians(altitude)
    relative_azimuth = math.radians(_wrap_degrees(azimuth - view_azimuth))
    elevation = math.sin(altitude_radians)
    x = 0.34 * math.cos(altitude_radians) * math.sin(relative_azimuth)
    y = 0.18 - 0.42 * elevation
    return x, y, elevation


def _sun_state(
    now: datetime, astronomy: Astronomy, view_azimuth: Optional[float] = None
) -> Tuple[bool, float, float, float, float, float]:
    zone = _zone(astronomy.timezone)
    local = now.astimezone(zone)
    if astronomy.sun_status == "polar_night":
        return False, 0.0, -0.34, 0.18, 0.0, 0.0
    if astronomy.sun_status == "midnight_sun":
        midnight = datetime.combine(local.date(), time.min, zone)
        progress = _clamp((local - midnight).total_seconds() / 86400.0)
        elevation = 0.55 + 0.45 * math.sin(math.pi * progress) ** 2
        x = 0.34 * math.sin(2.0 * math.pi * (progress - 0.25))
        y = 0.05 - 0.25 * elevation
        return True, progress, x, y, 1.0, elevation

    legacy = _legacy_sun_state(local, astronomy)
    geographic = _geographic_sun_position(local, astronomy, view_azimuth)
    if geographic is None:
        return legacy
    visible, progress, _, _, opacity, _ = legacy
    x, y, elevation = geographic
    return visible, progress, x, y, opacity, elevation


def _moon_interval_progress(
    now: datetime, astronomy: Astronomy
) -> Tuple[bool, float]:
    if astronomy.moonrise is None or astronomy.moonset is None:
        return False, 0.0
    zone = _zone(astronomy.timezone)
    local = now.astimezone(zone)
    rise = astronomy.moonrise.astimezone(zone)
    moonset = astronomy.moonset.astimezone(zone)
    if moonset <= rise:
        moonset += timedelta(days=1)
    for offset in (-1, 0, 1):
        shift = timedelta(days=offset)
        candidate_rise = rise + shift
        candidate_set = moonset + shift
        if candidate_rise <= local <= candidate_set:
            span = max(1.0, (candidate_set - candidate_rise).total_seconds())
            return True, _clamp((local - candidate_rise).total_seconds() / span)
    return False, 0.0


def compute_scene_state(
    now: datetime, astronomy: Astronomy, view_azimuth: Optional[float] = None
) -> SceneState:
    if now.tzinfo is None:
        raise ValueError("now must be timezone-aware")
    phase, phase_blend, ambient, colorfulness, stars, city, looks = _lighting_at(now, astronomy)
    sun_visible, sun_progress, sun_x, sun_y, sun_opacity, solar_elevation = _sun_state(
        now, astronomy, view_azimuth
    )
    moon_visible, moon_progress = _moon_interval_progress(now, astronomy)
    moon_elevation = math.sin(math.pi * moon_progress) if moon_visible else 0.0
    moon_x = -0.35 + 0.70 * moon_progress
    moon_y = 0.20 - 0.36 * moon_elevation
    moon_horizon = (
        _clamp(min(moon_progress / 0.08, (1.0 - moon_progress) / 0.08))
        if moon_visible
        else 0.0
    )
    illumination = _clamp(astronomy.moon_illumination / 100.0)
    moon_opacity = moon_horizon * (0.08 + 0.92 * math.sqrt(illumination))
    night_factor = _clamp((0.65 - ambient) / 0.47)
    lunar_fill = moon_horizon * night_factor * illumination ** 0.7
    if lunar_fill > 0.0:
        ambient = _clamp(ambient + 0.16 * lunar_fill)
        stars = _clamp(stars * (1.0 - 0.60 * lunar_fill))
        city = _clamp(city * (1.0 - 0.25 * lunar_fill))
        lifted: Dict[str, LayerLook] = {}
        moon_tint = (0.68, 0.76, 1.00)
        for name, look in looks.items():
            lifted[name] = LayerLook(
                tint_rgb=tuple(
                    _clamp(_lerp(look.tint_rgb[i], moon_tint[i], 0.18 * lunar_fill))
                    for i in range(3)
                ),
                tint_strength=_clamp(look.tint_strength - 0.16 * lunar_fill),
                opacity=_clamp(look.opacity + 0.04 * lunar_fill),
                blur=look.blur,
            )
        looks = lifted
    return SceneState(
        phase=phase,
        phase_blend=phase_blend,
        ambient_brightness=_clamp(ambient),
        saturation_impression=_clamp(colorfulness),
        stars_opacity=_clamp(stars),
        city_light_intensity=_clamp(city),
        layer_looks=looks,
        sun_visible=sun_visible,
        sun_progress=_clamp(sun_progress),
        sun_x=_clamp(sun_x, -0.34, 0.34),
        sun_y=_clamp(sun_y, -0.24, 0.18),
        sun_opacity=_clamp(sun_opacity),
        solar_elevation=_clamp(solar_elevation),
        moon_visible=moon_visible,
        moon_progress=_clamp(moon_progress),
        moon_x=_clamp(moon_x, -0.35, 0.35),
        moon_y=_clamp(moon_y, -0.16, 0.20),
        moon_opacity=_clamp(moon_opacity),
        moon_phase=astronomy.moon_phase,
        moon_illumination=astronomy.moon_illumination,
        lunar_fill=_clamp(lunar_fill),
    )


def compose_weather_scene(
    astronomy_scene: SceneState,
    weather: weather_module.WeatherState,
) -> SceneState:
    """Apply bounded weather modulation after astronomy has been computed."""
    if weather == weather_module.neutral_weather_state():
        return astronomy_scene
    cloud = _clamp(weather.cloud_intensity)
    fog = _clamp(weather.fog_intensity)
    precipitation = _clamp(weather.precipitation_intensity)
    rain_darkening = precipitation if weather.precipitation_type in ("rain", "hail") else 0.0
    cool_tint = (0.48, 0.60, 0.72)
    adjusted_looks: Dict[str, LayerLook] = {}
    for index in range(1, 7):
        name = f"{index}.png"
        look = astronomy_scene.layer_looks[name]
        distance = (7.0 - index) / 6.0
        fog_loss = 0.66 * fog * distance
        cloud_loss = 0.07 * cloud * distance
        tint_mix = _clamp(0.30 * cloud + 0.18 * rain_darkening + 0.12 * fog)
        adjusted_looks[name] = LayerLook(
            tint_rgb=tuple(
                _clamp(_lerp(look.tint_rgb[channel], cool_tint[channel], tint_mix))
                for channel in range(3)
            ),
            tint_strength=_clamp(
                look.tint_strength + 0.22 * cloud + 0.12 * rain_darkening + 0.08 * fog
            ),
            opacity=_clamp(look.opacity * (1.0 - fog_loss - cloud_loss)),
            blur=_clamp(look.blur + 2.2 * fog * distance + 0.45 * cloud, 0.0, 50.0),
        )
    return replace(
        astronomy_scene,
        ambient_brightness=_clamp(
            astronomy_scene.ambient_brightness * (1.0 - 0.22 * cloud - 0.12 * rain_darkening)
        ),
        saturation_impression=_clamp(
            astronomy_scene.saturation_impression * (1.0 - 0.38 * cloud - 0.18 * fog)
        ),
        stars_opacity=_clamp(astronomy_scene.stars_opacity * (1.0 - 0.94 * cloud)),
        layer_looks=adjusted_looks,
        sun_opacity=_clamp(
            astronomy_scene.sun_opacity * (1.0 - 0.88 * cloud) * (1.0 - 0.52 * fog)
        ),
        moon_opacity=_clamp(
            astronomy_scene.moon_opacity * (1.0 - 0.78 * cloud) * (1.0 - 0.42 * fog)
        ),
    )


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PIXEL_WIDTH = 576
PIXEL_HEIGHT = 324
MAX_PNG_DIMENSION = 4096


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def encode_png_rgba(width: int, height: int, pixels: bytes) -> bytes:
    if not isinstance(width, int) or not isinstance(height, int):
        raise ValueError("PNG dimensions must be integers")
    if width <= 0 or height <= 0 or width > MAX_PNG_DIMENSION or height > MAX_PNG_DIMENSION:
        raise ValueError(f"PNG dimensions must be 1..{MAX_PNG_DIMENSION}")
    expected = width * height * 4
    if len(pixels) != expected:
        raise ValueError(f"RGBA payload must contain exactly {expected} bytes")
    stride = width * 4
    rows = b"".join(
        b"\x00" + pixels[offset : offset + stride]
        for offset in range(0, expected, stride)
    )
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        PNG_SIGNATURE
        + _png_chunk(b"IHDR", header)
        + _png_chunk(b"IDAT", zlib.compress(rows, 9))
        + _png_chunk(b"IEND", b"")
    )


def _paeth(left: int, up: int, upper_left: int) -> int:
    estimate = left + up - upper_left
    left_distance = abs(estimate - left)
    up_distance = abs(estimate - up)
    diagonal_distance = abs(estimate - upper_left)
    if left_distance <= up_distance and left_distance <= diagonal_distance:
        return left
    if up_distance <= diagonal_distance:
        return up
    return upper_left


def decode_png_rgba(source: Any) -> Tuple[int, int, bytes]:
    data = (
        Path(source).read_bytes()
        if isinstance(source, (str, os.PathLike, Path))
        else bytes(source)
    )
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("invalid PNG signature")
    offset = len(PNG_SIGNATURE)
    width = height = 0
    compressed = bytearray()
    saw_header = saw_end = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        payload_start = offset + 8
        payload_end = payload_start + length
        crc_end = payload_end + 4
        if crc_end > len(data):
            raise ValueError("truncated PNG payload")
        payload = data[payload_start:payload_end]
        expected_crc = struct.unpack(">I", data[payload_end:crc_end])[0]
        if (zlib.crc32(kind + payload) & 0xFFFFFFFF) != expected_crc:
            raise ValueError(f"PNG {kind.decode('ascii', 'replace')} CRC mismatch")
        offset = crc_end
        if kind == b"IHDR":
            if saw_header or length != 13:
                raise ValueError("invalid PNG IHDR")
            header = struct.unpack(">IIBBBBB", payload)
            width, height, bit_depth, color_type, compression, filter_method, interlace = header
            if (
                width <= 0
                or height <= 0
                or width > MAX_PNG_DIMENSION
                or height > MAX_PNG_DIMENSION
            ):
                raise ValueError("PNG dimensions exceed supported bounds")
            if (bit_depth, color_type, compression, filter_method, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("only non-interlaced 8-bit RGBA PNG is supported")
            saw_header = True
        elif kind == b"IDAT":
            if not saw_header:
                raise ValueError("PNG IDAT precedes IHDR")
            compressed.extend(payload)
        elif kind == b"IEND":
            saw_end = True
            break
    if not saw_header or not saw_end or not compressed:
        raise ValueError("PNG is missing IHDR, IDAT, or IEND")
    try:
        raw = zlib.decompress(bytes(compressed))
    except zlib.error as error:
        raise ValueError("invalid PNG zlib stream") from error
    stride = width * 4
    if len(raw) != height * (stride + 1):
        raise ValueError("PNG scanline length mismatch")
    output = bytearray()
    previous = bytearray(stride)
    cursor = 0
    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1
        scanline = bytearray(raw[cursor : cursor + stride])
        cursor += stride
        if filter_type not in (0, 1, 2, 3, 4):
            raise ValueError(f"unsupported PNG filter {filter_type}")
        for index in range(stride):
            left = scanline[index - 4] if index >= 4 else 0
            up = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 1:
                scanline[index] = (scanline[index] + left) & 0xFF
            elif filter_type == 2:
                scanline[index] = (scanline[index] + up) & 0xFF
            elif filter_type == 3:
                scanline[index] = (scanline[index] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                scanline[index] = (scanline[index] + _paeth(left, up, upper_left)) & 0xFF
        output.extend(scanline)
        previous = scanline
    return width, height, bytes(output)


def _atomic_write_bytes(path: Path, payload: bytes) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        try:
            directory_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except OSError:
            pass
    except OSError:
        try:
            temporary.unlink()
        except OSError:
            pass
        raise


class DoubleBufferedAssets:
    def __init__(self, directory: Path) -> None:
        self.directory = Path(directory)

    @staticmethod
    def _safe_name(name: str) -> str:
        if not name or any(not (character.isalnum() or character in "_-") for character in name):
            raise ValueError(
                "asset name may contain only letters, numbers, underscores, and hyphens"
            )
        return name

    def next_path(
        self,
        name: str,
        current: Optional[Path] = None,
        extension: str = "png",
    ) -> Path:
        safe_name = self._safe_name(name)
        if extension not in ("png", "gif"):
            raise ValueError("asset extension must be png or gif")
        if current is None:
            side = "a"
        else:
            if Path(current).suffix != f".{extension}":
                raise ValueError(f"current asset is not a {safe_name} {extension.upper()} path")
            stem = Path(current).stem
            if stem == f"{safe_name}-a":
                side = "b"
            elif stem == f"{safe_name}-b":
                side = "a"
            else:
                raise ValueError(f"current asset is not a {safe_name} A/B path")
        return self.directory / f"{safe_name}-{side}.{extension}"

    def write(
        self,
        name: str,
        payload: bytes,
        current: Optional[Path] = None,
        extension: str = "png",
    ) -> Path:
        destination = self.next_path(name, current, extension)
        _atomic_write_bytes(destination, payload)
        return destination

    def write_gif(self, name: str, payload: bytes, current: Optional[Path] = None) -> Path:
        weather_module.validate_gif(payload, PIXEL_WIDTH, PIXEL_HEIGHT)
        destination = self.write(name, payload, current, extension="gif")
        weather_module.validate_gif(destination, PIXEL_WIDTH, PIXEL_HEIGHT)
        return destination


def _transparent_pixels() -> bytearray:
    return bytearray(PIXEL_WIDTH * PIXEL_HEIGHT * 4)


def _put_pixel(
    pixels: bytearray, x: int, y: int, red: int, green: int, blue: int, alpha: int
) -> None:
    if x < 0 or y < 0 or x >= PIXEL_WIDTH or y >= PIXEL_HEIGHT:
        return
    index = (y * PIXEL_WIDTH + x) * 4
    if alpha >= pixels[index + 3]:
        pixels[index : index + 4] = bytes((red, green, blue, alpha))


def render_sun_png() -> bytes:
    pixels = _transparent_pixels()
    center_x, center_y = PIXEL_WIDTH // 2, PIXEL_HEIGHT // 2
    for y in range(center_y - 20, center_y + 21):
        for x in range(center_x - 20, center_x + 21):
            distance = math.hypot(x - center_x, y - center_y)
            if distance <= 19.0:
                alpha = int(_clamp((19.0 - distance) / 7.0) * 86)
                _put_pixel(pixels, x, y, 255, 154, 58, alpha)
            if distance <= 12.0:
                color = (255, 226, 112) if distance <= 8.0 else (255, 190, 64)
                _put_pixel(pixels, x, y, *color, 255)
    return encode_png_rgba(PIXEL_WIDTH, PIXEL_HEIGHT, bytes(pixels))


def render_moon_png(phase: str, illumination_percent: float) -> bytes:
    if phase not in MOON_PHASES:
        raise ValueError(f"unsupported moon phase: {phase}")
    illumination = _clamp(float(illumination_percent) / 100.0)
    phase_angle = math.acos(_clamp(2.0 * illumination - 1.0, -1.0, 1.0))
    side = -1.0 if phase.startswith("Waning") or phase == "Last Quarter" else 1.0
    light_x = side * math.sin(phase_angle)
    light_z = math.cos(phase_angle)
    pixels = _transparent_pixels()
    center_x, center_y, radius = PIXEL_WIDTH // 2, PIXEL_HEIGHT // 2, 14
    for y in range(center_y - radius, center_y + radius + 1):
        for x in range(center_x - radius, center_x + radius + 1):
            normal_x = (x - center_x) / radius
            normal_y = (y - center_y) / radius
            radial = normal_x * normal_x + normal_y * normal_y
            if radial > 1.0:
                continue
            normal_z = math.sqrt(max(0.0, 1.0 - radial))
            if illumination <= 0.0:
                light = -1.0
            elif illumination >= 1.0:
                light = 1.0
            else:
                light = normal_x * light_x + normal_z * light_z
            if light > 0.0:
                brightness = 0.78 + 0.22 * light
                _put_pixel(
                    pixels,
                    x,
                    y,
                    int(236 * brightness),
                    int(242 * brightness),
                    int(255 * brightness),
                    255,
                )
            else:
                _put_pixel(pixels, x, y, 24, 32, 52, 92)
    return encode_png_rgba(PIXEL_WIDTH, PIXEL_HEIGHT, bytes(pixels))


def render_shadow_png(source_path: Path, scene: SceneState) -> bytes:
    width, height, source = decode_png_rgba(source_path)
    if (width, height) != (PIXEL_WIDTH, PIXEL_HEIGHT):
        raise ValueError(f"shadow source must be {PIXEL_WIDTH}x{PIXEL_HEIGHT} RGBA")
    output = _transparent_pixels()
    if not scene.sun_visible or scene.sun_opacity <= 0.0:
        return encode_png_rgba(width, height, bytes(output))
    direction = 1.0 if scene.sun_x < 0.0 else -1.0 if scene.sun_x > 0.0 else 0.0
    length_scale = 0.15 + 0.75 * (1.0 - scene.solar_elevation)
    vertical_scale = 0.06 + 0.14 * scene.solar_elevation
    maximum_alpha = int(110.0 * scene.sun_opacity * (1.0 - 0.75 * scene.solar_elevation))
    bottom = height - 1
    for y in range(height):
        height_above_ground = bottom - y
        for x in range(width):
            source_alpha = source[(y * width + x) * 4 + 3]
            if source_alpha <= 32:
                continue
            target_x = round(x + direction * height_above_ground * length_scale)
            target_y = round(bottom - height_above_ground * vertical_scale)
            alpha = int(maximum_alpha * source_alpha / 255.0)
            _put_pixel(output, target_x, target_y, 12, 18, 30, alpha)
            _put_pixel(output, target_x + int(direction), target_y, 12, 18, 30, alpha)
    return encode_png_rgba(width, height, bytes(output))


def prepare_initial_assets(example_directory: Path) -> Mapping[str, Path]:
    example_directory = Path(example_directory)
    buffers = DoubleBufferedAssets(example_directory / "generated")
    transparent = encode_png_rgba(
        PIXEL_WIDTH, PIXEL_HEIGHT, bytes(PIXEL_WIDTH * PIXEL_HEIGHT * 4)
    )
    result = {
        "sun": buffers.write("sun", render_sun_png()),
        "moon": buffers.write("moon", render_moon_png("New Moon", 0.0)),
        "shadow": buffers.write("shadow", transparent),
    }
    neutral = weather_module.neutral_weather_state()
    for name in WEATHER_LAYER_NAMES:
        result[name] = buffers.write_gif(
            name, weather_module.render_weather_gif(name, neutral)
        )
    return result


class IPCError(RuntimeError):
    """Hyprlax control command or managed-layer ownership failure."""


@dataclass(frozen=True)
class ManagedLayer:
    name: str
    layer_id: int
    path: Path


@dataclass(frozen=True)
class ManagedLayers:
    example_directory: Path
    layers: Mapping[str, ManagedLayer]
    assumed_ids: bool = False

    def require(self, name: str) -> ManagedLayer:
        try:
            return self.layers[name]
        except KeyError as error:
            raise IPCError(f"managed layer is missing: {name}") from error


@dataclass(frozen=True)
class IPCCommand:
    target: str
    layer_id: int
    property: str
    value: str

    @property
    def signature(self) -> Tuple[str, str]:
        return self.target, self.property

    def as_json(self) -> Mapping[str, Any]:
        return {
            "target": self.target,
            "layer_id": self.layer_id,
            "property": self.property,
            "value": self.value,
        }


WEATHER_LAYER_NAMES = (
    "weather-cloud",
    "weather-fog-back",
    "weather-precip-back",
    "weather-fog-front",
    "weather-precip-front",
)
MANAGED_NAMES = (
    "1.png",
    "sun",
    "moon",
    "weather-cloud",
    "2.png",
    "3.png",
    "weather-fog-back",
    "4.png",
    "weather-precip-back",
    "5.png",
    "shadow",
    "6.png",
    "weather-fog-front",
    "weather-precip-front",
)
ASSUMED_IDS = {name: index for index, name in enumerate(MANAGED_NAMES, 1)}
SUPPORTED_SCENE_PROPERTIES = frozenset(("path", "x", "y", "opacity", "tint", "blur"))


def _validate_example_directory(example_directory: Path) -> Path:
    resolved = Path(example_directory).expanduser().resolve(strict=False)
    if any(character.isspace() for character in str(resolved)):
        raise IPCError(
            "the example path contains whitespace, which current IPC cannot encode safely"
        )
    return resolved


def _managed_name_for_path(path: Path, example_directory: Path) -> Optional[str]:
    for index in range(1, 7):
        if path == example_directory / f"{index}.png":
            return f"{index}.png"
    generated = example_directory / "generated"
    if path.parent == generated:
        for index in range(1, 4):
            if path.name in (f"heat-{index}-a.gif", f"heat-{index}-b.gif"):
                return f"{index}.png"
        for name in ("sun", "moon", "shadow"):
            if path.name in (f"{name}-a.png", f"{name}-b.png"):
                return name
        for name in WEATHER_LAYER_NAMES:
            if path.name in (f"{name}-a.gif", f"{name}-b.gif"):
                return name
    return None


def discover_managed_layers(
    raw_layers: Any, example_directory: Path, assumed_ids: bool = False
) -> ManagedLayers:
    example = _validate_example_directory(example_directory)
    if not isinstance(raw_layers, list):
        raise IPCError("hyprlax layer list must be a JSON array")
    found: Dict[str, ManagedLayer] = {}
    seen_ids = set()
    for raw in raw_layers:
        if not isinstance(raw, Mapping):
            raise IPCError("hyprlax layer entry must be an object")
        layer_id = raw.get("id")
        path_value = raw.get("path")
        if isinstance(layer_id, bool) or not isinstance(layer_id, int) or layer_id <= 0:
            raise IPCError("hyprlax layer id must be a positive integer")
        if not isinstance(path_value, str) or not path_value:
            raise IPCError("hyprlax layer path must be a nonempty string")
        if layer_id in seen_ids:
            raise IPCError(f"duplicate hyprlax layer id: {layer_id}")
        seen_ids.add(layer_id)
        path = Path(path_value).expanduser().resolve(strict=False)
        name = _managed_name_for_path(path, example)
        if name is None:
            continue
        if name in found:
            raise IPCError(f"duplicate managed layer path for {name}")
        found[name] = ManagedLayer(name, layer_id, path)
    missing = [name for name in MANAGED_NAMES if name not in found]
    if missing:
        raise IPCError(f"managed layers missing from daemon: {', '.join(missing)}")
    return ManagedLayers(example, found, assumed_ids=assumed_ids)


def assumed_managed_layers(example_directory: Path) -> ManagedLayers:
    example = _validate_example_directory(example_directory)
    raw = []
    for name in MANAGED_NAMES:
        if name.endswith(".png"):
            path = example / name
        elif name in WEATHER_LAYER_NAMES:
            path = example / "generated" / f"{name}-a.gif"
        else:
            path = example / "generated" / f"{name}-a.png"
        raw.append({"id": ASSUMED_IDS[name], "path": str(path)})
    return discover_managed_layers(raw, example, assumed_ids=True)


class HyprlaxIPC:
    def __init__(
        self,
        binary: str = "hyprlax",
        runner: Callable[..., Any] = subprocess.run,
        timeout: float = 5.0,
    ) -> None:
        self.binary = binary
        self.runner = runner
        self.timeout = timeout

    def _run(self, arguments: Tuple[str, ...]) -> str:
        command = [self.binary, "ctl", *arguments]
        try:
            result = self.runner(
                command,
                capture_output=True,
                text=True,
                timeout=self.timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise IPCError(f"hyprlax ctl failed for {' '.join(arguments)}: {error}") from error
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "no diagnostic").strip()
            raise IPCError(
                f"hyprlax ctl {' '.join(arguments)} exited {result.returncode}: {detail[:512]}"
            )
        return result.stdout

    def list_layers(self) -> Any:
        output = self._run(("list", "--json"))
        try:
            value = json.loads(output)
        except json.JSONDecodeError as error:
            raise IPCError("hyprlax ctl list --json returned invalid JSON") from error
        if not isinstance(value, list):
            raise IPCError("hyprlax ctl list --json must return a JSON array")
        return value

    def modify(self, command: IPCCommand) -> None:
        if command.property not in SUPPORTED_SCENE_PROPERTIES:
            raise IPCError(f"unsupported scene property: {command.property}")
        self._run(
            (
                "modify",
                str(command.layer_id),
                command.property,
                command.value,
            )
        )


def _tint_value(look: LayerLook) -> str:
    if look.tint_strength <= 0.0005:
        return "none"
    red, green, blue = (round(_clamp(channel) * 255.0) for channel in look.tint_rgb)
    return f"#{red:02x}{green:02x}{blue:02x}:{look.tint_strength:.3f}"


def _screen_position_as_uv_offset(value: float) -> str:
    """Convert a signed screen displacement to the renderer's inverse UV direction."""
    return f"{-value:.5f}"


def plan_asset_paths(managed: ManagedLayers) -> Mapping[str, Path]:
    buffers = DoubleBufferedAssets(managed.example_directory / "generated")
    paths = {
        "moon": buffers.next_path("moon", managed.require("moon").path),
        "shadow": buffers.next_path("shadow", managed.require("shadow").path),
    }
    for name in WEATHER_LAYER_NAMES:
        paths[name] = managed.require(name).path
    for index in range(1, 4):
        paths[f"{index}.png"] = managed.example_directory / f"{index}.png"
    return paths


def update_scene_assets(
    managed: ManagedLayers,
    scene: SceneState,
    names: Tuple[str, ...] = ("moon", "shadow"),
    weather: Optional[weather_module.WeatherState] = None,
) -> Mapping[str, Path]:
    buffers = DoubleBufferedAssets(managed.example_directory / "generated")
    weather_state = weather or weather_module.neutral_weather_state()
    paths = {}
    if "moon" in names:
        paths["moon"] = buffers.write(
            "moon",
            render_moon_png(scene.moon_phase, scene.moon_illumination),
            managed.require("moon").path,
        )
    if "shadow" in names:
        paths["shadow"] = buffers.write(
            "shadow",
            render_shadow_png(managed.example_directory / "6.png", scene),
            managed.require("shadow").path,
        )
    for name in WEATHER_LAYER_NAMES:
        if name not in names:
            continue
        paths[name] = buffers.write_gif(
            name,
            weather_module.render_weather_gif(name, weather_state),
            managed.require(name).path,
        )
    for index in range(1, 4):
        asset_name = f"heat-{index}"
        if asset_name not in names:
            continue
        width, height, source = decode_png_rgba(
            managed.example_directory / f"{index}.png"
        )
        current = managed.require(f"{index}.png").path
        if current.parent != managed.example_directory / "generated":
            current = None
        paths[f"{index}.png"] = buffers.write_gif(
            asset_name,
            weather_module.render_heat_gif(
                source, weather_state.heat_intensity, width, height
            ),
            current,
        )
    supported = {"moon", "shadow", *WEATHER_LAYER_NAMES}
    supported.update(f"heat-{index}" for index in range(1, 4))
    unknown = set(names) - supported
    if unknown:
        raise ValueError(f"unknown dynamic asset names: {', '.join(sorted(unknown))}")
    return paths


def build_ipc_commands(
    managed: ManagedLayers,
    scene: SceneState,
    asset_paths: Mapping[str, Path],
    weather: Optional[weather_module.WeatherState] = None,
) -> Tuple[IPCCommand, ...]:
    weather_state = weather or weather_module.neutral_weather_state()
    commands = []
    for index in range(1, 7):
        name = f"{index}.png"
        layer = managed.require(name)
        look = scene.layer_looks[name]
        if index <= 3:
            desired_path = Path(
                asset_paths.get(name, managed.example_directory / name)
            ).resolve(strict=False)
            commands.append(IPCCommand(name, layer.layer_id, "path", str(desired_path)))
        commands.extend(
            (
                IPCCommand(name, layer.layer_id, "tint", _tint_value(look)),
                IPCCommand(name, layer.layer_id, "opacity", f"{look.opacity:.3f}"),
                IPCCommand(name, layer.layer_id, "blur", f"{look.blur:.3f}"),
            )
        )
    overlay_opacities = {
        "weather-cloud": 0.78 * weather_state.cloud_intensity,
        "weather-fog-back": 0.54 * weather_state.fog_intensity,
        "weather-fog-front": 0.72 * weather_state.fog_intensity,
        "weather-precip-back": 0.72 * weather_state.precipitation_intensity,
        "weather-precip-front": weather_state.precipitation_intensity,
    }
    if weather_state.precipitation_type == "none":
        overlay_opacities["weather-precip-front"] = (
            0.34 * weather_state.wind_intensity
            if weather_state.wind_intensity >= 0.35
            else 0.0
        )
        overlay_opacities["weather-precip-back"] = 0.0
    for name in WEATHER_LAYER_NAMES:
        layer = managed.require(name)
        path = Path(asset_paths.get(name, layer.path)).resolve(strict=False)
        commands.extend(
            (
                IPCCommand(name, layer.layer_id, "path", str(path)),
                IPCCommand(
                    name,
                    layer.layer_id,
                    "opacity",
                    f"{_clamp(overlay_opacities[name]):.3f}",
                ),
            )
        )
    sun = managed.require("sun")
    moon = managed.require("moon")
    shadow = managed.require("shadow")
    moon_path = Path(asset_paths["moon"]).resolve(strict=False)
    shadow_path = Path(asset_paths["shadow"]).resolve(strict=False)
    commands.extend(
        (
            IPCCommand("moon", moon.layer_id, "path", str(moon_path)),
            IPCCommand(
                "sun", sun.layer_id, "x", _screen_position_as_uv_offset(scene.sun_x)
            ),
            IPCCommand(
                "sun", sun.layer_id, "y", _screen_position_as_uv_offset(scene.sun_y)
            ),
            IPCCommand("sun", sun.layer_id, "opacity", f"{scene.sun_opacity:.3f}"),
            IPCCommand(
                "moon", moon.layer_id, "x", _screen_position_as_uv_offset(scene.moon_x)
            ),
            IPCCommand(
                "moon", moon.layer_id, "y", _screen_position_as_uv_offset(scene.moon_y)
            ),
            IPCCommand("moon", moon.layer_id, "opacity", f"{scene.moon_opacity:.3f}"),
            IPCCommand("shadow", shadow.layer_id, "path", str(shadow_path)),
            IPCCommand(
                "shadow",
                shadow.layer_id,
                "opacity",
                "1.000" if scene.sun_visible and scene.sun_opacity > 0.0 else "0.000",
            ),
        )
    )
    if any(command.property not in SUPPORTED_SCENE_PROPERTIES for command in commands):
        raise IPCError("command plan contains an unsupported property")
    return tuple(commands)


class CommandDelta:
    def __init__(self) -> None:
        self.applied: Dict[Tuple[str, str], str] = {}

    def pending(self, commands: Tuple[IPCCommand, ...]) -> Tuple[IPCCommand, ...]:
        return tuple(
            command
            for command in commands
            if self.applied.get(command.signature) != command.value
        )

    def mark(self, command: IPCCommand) -> None:
        self.applied[command.signature] = command.value


class SceneController:
    def __init__(self, ipc: HyprlaxIPC, example_directory: Path) -> None:
        self.ipc = ipc
        self.example_directory = _validate_example_directory(example_directory)
        self.delta = CommandDelta()
        self.asset_signatures: Dict[str, Tuple[Any, ...]] = {}
        self.pending_asset_signatures: Dict[str, Tuple[Any, ...]] = {}

    @staticmethod
    def _asset_signature(
        scene: SceneState,
        weather: weather_module.WeatherState,
    ) -> Mapping[str, Tuple[Any, ...]]:
        weather_signature = weather_module.quantized_weather_signature(weather)
        signatures = {
            "moon": (scene.moon_phase, round(scene.moon_illumination, 3)),
            "shadow": (
                scene.sun_visible,
                round(scene.sun_x, 5),
                round(scene.sun_y, 5),
                round(scene.sun_opacity, 3),
                round(scene.solar_elevation, 5),
            ),
        }
        signatures["weather-cloud"] = (
            weather_signature[2], weather_signature[5], weather_signature[6]
        )
        for name in ("weather-fog-back", "weather-fog-front"):
            signatures[name] = (
                weather_signature[3], weather_signature[5], weather_signature[6]
            )
        for name in ("weather-precip-back", "weather-precip-front"):
            signatures[name] = (
                weather_signature[1], weather_signature[4],
                weather_signature[5], weather_signature[6],
            )
        for index in range(1, 4):
            signatures[f"heat-{index}"] = (weather_signature[-1],)
        return signatures

    @staticmethod
    def _transparent_gif(path: Path) -> bool:
        try:
            decoded = weather_module.validate_gif(path, PIXEL_WIDTH, PIXEL_HEIGHT)
        except (OSError, ValueError):
            return False
        transparent = decoded.transparent_index
        return transparent is not None and all(
            all(index == transparent for index in frame) for frame in decoded.frames
        )

    def plan(
        self,
        scene: SceneState,
        weather: Optional[weather_module.WeatherState] = None,
        dry_run: bool = False,
    ) -> Tuple[ManagedLayers, Tuple[IPCCommand, ...]]:
        weather_state = weather or weather_module.neutral_weather_state()
        if dry_run:
            managed = assumed_managed_layers(self.example_directory)
            assets = plan_asset_paths(managed)
            self.pending_asset_signatures = {}
        else:
            managed = discover_managed_layers(self.ipc.list_layers(), self.example_directory)
            signatures = self._asset_signature(scene, weather_state)
            assets = {
                "moon": managed.require("moon").path,
                "shadow": managed.require("shadow").path,
            }
            for name in WEATHER_LAYER_NAMES:
                assets[name] = managed.require(name).path
            for index in range(1, 4):
                name = f"{index}.png"
                assets[name] = managed.require(name).path
            pending = {}
            changed = {
                name for name, signature in signatures.items()
                if self.asset_signatures.get(name) != signature
            }
            generate = set(changed)
            for name in WEATHER_LAYER_NAMES:
                if name not in changed:
                    continue
                inactive = (
                    name == "weather-cloud" and weather_state.cloud_intensity <= 0.0005
                ) or (
                    name.startswith("weather-fog") and weather_state.fog_intensity <= 0.0005
                ) or (
                    name == "weather-precip-back"
                    and weather_state.precipitation_intensity <= 0.0005
                ) or (
                    name == "weather-precip-front"
                    and weather_state.precipitation_intensity <= 0.0005
                    and weather_state.wind_intensity < 0.35
                )
                if inactive and self._transparent_gif(managed.require(name).path):
                    generate.remove(name)
                    pending[name] = signatures[name]
            for index in range(1, 4):
                asset_name = f"heat-{index}"
                if asset_name not in changed:
                    continue
                if weather_state.heat_intensity <= 0.0005:
                    generate.remove(asset_name)
                    assets[f"{index}.png"] = managed.example_directory / f"{index}.png"
                    pending[asset_name] = signatures[asset_name]
            if generate:
                generated = update_scene_assets(
                    managed,
                    scene,
                    tuple(sorted(generate)),
                    weather_state,
                )
                for name in generate:
                    target = name
                    if name.startswith("heat-"):
                        target = f"{name.removeprefix('heat-')}.png"
                    assets[target] = generated[target]
                    pending[name] = signatures[name]
            self.pending_asset_signatures = pending
        return managed, self.delta.pending(
            build_ipc_commands(managed, scene, assets, weather_state)
        )

    def apply_once(
        self,
        scene: SceneState,
        weather: Optional[weather_module.WeatherState] = None,
    ) -> Tuple[IPCCommand, ...]:
        _, commands = self.plan(scene, weather, dry_run=False)
        executed = []
        for command in commands:
            self.ipc.modify(command)
            self.delta.mark(command)
            executed.append(command)
        self.asset_signatures.update(self.pending_asset_signatures)
        self.pending_asset_signatures = {}
        return tuple(executed)


class DailyCache:
    """Schema-versioned daily provider cache with process-shared attempt reservation."""

    def __init__(
        self,
        path: Optional[Path] = None,
        clock: Callable[[], datetime] = lambda: datetime.now(datetime_timezone.utc),
    ) -> None:
        cache_root = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
        self.path = Path(path) if path is not None else (
            cache_root / "hyprlax" / "pixel-city-dynamic" / "daily-v1.json"
        )
        self.lock_path = self.path.with_name("daily-v1.lock")
        self.clock = clock

    @staticmethod
    def _empty() -> Dict[str, Any]:
        return {"schema_version": CACHE_SCHEMA_VERSION, "providers": {}}

    def _load_unlocked(self) -> Dict[str, Any]:
        if not self.path.exists():
            return self._empty()
        try:
            with self.path.open("r", encoding="utf-8") as handle:
                value = json.load(handle)
            if not isinstance(value, dict):
                raise ValueError("cache root is not an object")
            if value.get("schema_version") != CACHE_SCHEMA_VERSION:
                raise ValueError("cache schema version mismatch")
            if not isinstance(value.get("providers"), dict):
                raise ValueError("cache providers is not an object")
            return value
        except (OSError, ValueError, json.JSONDecodeError):
            return self._empty()

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
            raise CacheError(f"failed to write daily cache: {error}") from error

    def _locked(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lock_handle = self.lock_path.open("a+", encoding="utf-8")
        os.chmod(self.lock_path, 0o600)
        fcntl.flock(lock_handle.fileno(), fcntl.LOCK_EX)
        return lock_handle

    @staticmethod
    def _success_data(entry: Mapping[str, Any], identity: str) -> Optional[Mapping[str, Any]]:
        success = entry.get("last_success")
        if not isinstance(success, Mapping) or success.get("identity", "") != identity:
            return None
        data = success.get("data")
        return data if isinstance(data, Mapping) else None

    def last_success(self, provider: str, identity: str = "") -> Optional[Mapping[str, Any]]:
        lock_handle = self._locked()
        try:
            cache = self._load_unlocked()
            entry = cache["providers"].get(provider, {})
            if not isinstance(entry, Mapping):
                return None
            return self._success_data(entry, identity)
        finally:
            fcntl.flock(lock_handle.fileno(), fcntl.LOCK_UN)
            lock_handle.close()

    def run_daily(
        self,
        provider: str,
        attempt_date: date,
        fetch: Callable[[], Mapping[str, Any]],
        identity: str = "",
        canonical_date: Optional[Callable[[Mapping[str, Any]], date]] = None,
    ) -> CacheResult:
        provider = _clean_text(provider, "provider", 64)
        day_string = attempt_date.isoformat()
        lock_handle = self._locked()
        try:
            cache = self._load_unlocked()
            providers = cache["providers"]
            entry = providers.get(provider)
            if not isinstance(entry, dict):
                entry = {}
                providers[provider] = entry
            previous_data = self._success_data(entry, identity)
            previous_success = entry.get("last_success", {})
            previous_day = (
                previous_success.get("attempt_date")
                if isinstance(previous_success, Mapping)
                else None
            )
            attempted_dates = {
                entry.get("last_attempt_date"),
                entry.get("last_attempt_input_date"),
            }
            if day_string in attempted_dates:
                return CacheResult(
                    data=previous_data,
                    source="cache" if previous_data is not None else "missing",
                    stale=previous_data is not None and previous_day != day_string,
                    attempted=False,
                    error=(
                        entry.get("last_error")
                        if isinstance(entry.get("last_error"), str)
                        else None
                    ),
                )

            now_string = self.clock().astimezone(datetime_timezone.utc).isoformat()
            entry["last_attempt_date"] = day_string
            entry["last_attempt_input_date"] = day_string
            entry["last_attempt_at"] = now_string
            entry["last_error"] = None
            self._write_unlocked(cache)

            try:
                fetched = fetch()
                if not isinstance(fetched, Mapping):
                    raise ProviderError("normalized provider data must be an object")
                final_date = canonical_date(fetched) if canonical_date is not None else attempt_date
                if not isinstance(final_date, date):
                    raise ProviderError("canonical provider date must be a date")
                final_day = final_date.isoformat()
                entry["last_attempt_date"] = final_day
                entry["last_error"] = None
                entry["last_success"] = {
                    "attempt_date": final_day,
                    "fetched_at": now_string,
                    "identity": identity,
                    "data": dict(fetched),
                }
                self._write_unlocked(cache)
                return CacheResult(dict(fetched), "fresh", False, True)
            except (CacheError, ProviderError, HTTPError, URLError, OSError, ValueError) as error:
                entry["last_error"] = _bounded_error(error)
                self._write_unlocked(cache)
                return CacheResult(
                    data=previous_data,
                    source="stale" if previous_data is not None else "missing",
                    stale=previous_data is not None,
                    attempted=True,
                    error=entry["last_error"],
                )
        finally:
            fcntl.flock(lock_handle.fileno(), fcntl.LOCK_UN)
            lock_handle.close()


class HttpJsonClient:
    def __init__(
        self,
        opener: Callable[..., Any] = urlopen,
        timeout: int = HTTP_TIMEOUT_SECONDS,
    ) -> None:
        self.opener = opener
        self.timeout = timeout

    def get_json(self, url: str) -> Mapping[str, Any]:
        request = Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
        try:
            with self.opener(request, timeout=self.timeout) as response:
                payload = response.read(MAX_RESPONSE_BYTES + 1)
        except (HTTPError, URLError, OSError, TimeoutError) as error:
            raise ProviderError(f"request failed: {error}") from error
        if len(payload) > MAX_RESPONSE_BYTES:
            raise ProviderError(f"response exceeds {MAX_RESPONSE_BYTES} bytes")
        try:
            value = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ProviderError("response is not valid UTF-8 JSON") from error
        if not isinstance(value, dict):
            raise ProviderError("response JSON root must be an object")
        return value


class LocationProvider:
    def __init__(self, client: HttpJsonClient) -> None:
        self.client = client

    def fetch(self) -> Location:
        value = self.client.get_json(IP_API_URL)
        if value.get("status") != "success":
            message = _optional_text(value.get("message", "unknown failure"), "message")
            raise ProviderError(f"ip-api failed: {message or 'unknown failure'}")
        return Location.from_mapping(
            {
                "latitude": value.get("lat"),
                "longitude": value.get("lon"),
                "timezone": value.get("timezone"),
                "city": value.get("city", ""),
                "region": value.get("regionName", ""),
                "country": value.get("country", ""),
                "country_code": value.get("countryCode", ""),
            },
            source="ip-api",
        )


class AstronomyProvider:
    def __init__(self, client: HttpJsonClient) -> None:
        self.client = client

    def fetch(self, location: Location, requested_day: date) -> Astronomy:
        query = urlencode(
            {
                "lat": f"{location.latitude:.4f}",
                "lng": f"{location.longitude:.4f}",
                "date": requested_day.isoformat(),
            }
        )
        value = self.client.get_json(f"{ASTRONOMY_API_URL}?{query}")
        if "error" in value:
            message = _optional_text(value.get("message", value.get("error")), "message")
            raise ProviderError(f"astronomy provider failed: {message or value['error']}")
        normalized = dict(value)
        normalized["timezone"] = value.get("tzid")
        return Astronomy.from_mapping(
            normalized,
            expected_day=requested_day,
            expected_timezone=location.timezone,
            source="sunrise-sunset-v2",
        )


def manual_location(
    latitude: float,
    longitude: float,
    timezone_name: str,
    locality: str = "Manual location",
) -> Location:
    return Location.from_mapping(
        {
            "latitude": latitude,
            "longitude": longitude,
            "timezone": timezone_name,
            "city": locality,
        },
        source="manual",
    )


def neutral_astronomy(requested_day: date, timezone_name: str) -> Astronomy:
    zone = _zone(timezone_name)

    def at(hour: int, minute: int) -> datetime:
        return datetime.combine(requested_day, time(hour, minute), zone)

    return Astronomy(
        day=requested_day,
        timezone=timezone_name,
        sun_status="normal",
        civil_twilight_begin=at(5, 30),
        sunrise=at(6, 0),
        solar_noon=at(12, 0),
        sunset=at(18, 0),
        civil_twilight_end=at(18, 30),
        moonrise=None,
        moonset=None,
        moon_phase="New Moon",
        moon_illumination=0.0,
        solar_position={},
        source="fallback",
    )


def resolve_daily_facts(
    cache: DailyCache,
    client: HttpJsonClient,
    now: Optional[datetime] = None,
    latitude: Optional[float] = None,
    longitude: Optional[float] = None,
    timezone_name: Optional[str] = None,
    locality: str = "Manual location",
) -> DailyFacts:
    current = now or datetime.now().astimezone()
    if current.tzinfo is None:
        raise ValueError("now must be timezone-aware")
    errors = []
    manual_values = (latitude, longitude, timezone_name)
    if any(value is not None for value in manual_values) and not all(
        value is not None for value in manual_values
    ):
        raise ValueError("latitude, longitude, and timezone must be supplied together")

    if all(value is not None for value in manual_values):
        location = manual_location(float(latitude), float(longitude), str(timezone_name), locality)
        location_source = "manual"
        location_stale = False
    else:
        cached_mapping = cache.last_success("ip-api")
        cached_location: Optional[Location] = None
        if cached_mapping is not None:
            try:
                cached_location = Location.from_mapping(cached_mapping, source="cache")
            except ProviderError:
                cached_location = None
        initial_zone = (
            _zone(cached_location.timezone)
            if cached_location is not None
            else current.tzinfo
        )
        attempt_day = current.astimezone(initial_zone).date()
        provider = LocationProvider(client)

        def fetch_location() -> Mapping[str, Any]:
            return provider.fetch().to_mapping()

        def location_day(value: Mapping[str, Any]) -> date:
            normalized = Location.from_mapping(value, source="ip-api")
            return current.astimezone(_zone(normalized.timezone)).date()

        result = cache.run_daily(
            "ip-api", attempt_day, fetch_location, canonical_date=location_day
        )
        if result.error:
            errors.append(f"ip-api: {result.error}")
        if result.data is not None:
            location = Location.from_mapping(result.data, source=result.source)
            location_source = result.source
            location_stale = result.stale
        elif cached_location is not None:
            location = cached_location
            location_source = "stale"
            location_stale = True
        else:
            location = Location(0.0, 0.0, "UTC", city="Unknown", source="fallback")
            location_source = "fallback"
            location_stale = True

    local_day = current.astimezone(_zone(location.timezone)).date()
    if location_source == "fallback":
        astronomy = neutral_astronomy(local_day, location.timezone)
        errors.append("sunrise-sunset-v2: skipped because no valid location is available")
        return DailyFacts(
            location=location,
            astronomy=astronomy,
            location_source=location_source,
            astronomy_source="fallback",
            stale=True,
            errors=tuple(errors),
        )

    astronomy_provider = AstronomyProvider(client)

    def fetch_astronomy() -> Mapping[str, Any]:
        return astronomy_provider.fetch(location, local_day).to_mapping()

    astronomy_result = cache.run_daily(
        "sunrise-sunset-v2",
        local_day,
        fetch_astronomy,
        identity=location.identity,
    )
    if astronomy_result.error:
        errors.append(f"sunrise-sunset-v2: {astronomy_result.error}")
    if astronomy_result.data is not None:
        try:
            astronomy = Astronomy.from_mapping(
                astronomy_result.data,
                expected_day=local_day,
                expected_timezone=location.timezone,
                source=astronomy_result.source,
            )
            astronomy_source = astronomy_result.source
            astronomy_stale = astronomy_result.stale
        except ProviderError as error:
            errors.append(f"cached astronomy: {_bounded_error(error)}")
            astronomy = neutral_astronomy(local_day, location.timezone)
            astronomy_source = "fallback"
            astronomy_stale = True
    else:
        astronomy = neutral_astronomy(local_day, location.timezone)
        astronomy_source = "fallback"
        astronomy_stale = True

    return DailyFacts(
        location=location,
        astronomy=astronomy,
        location_source=location_source,
        astronomy_source=astronomy_source,
        stale=location_stale or astronomy_stale,
        errors=tuple(errors),
    )


def _aware_datetime(value: str) -> datetime:
    try:
        parsed = datetime.fromisoformat(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an ISO-8601 timestamp") from error
    if parsed.tzinfo is None:
        raise argparse.ArgumentTypeError("must include a UTC offset")
    return parsed


def _azimuth_argument(value: str) -> float:
    try:
        result = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number from 0 through 360") from error
    if not math.isfinite(result) or not 0.0 <= result <= 360.0:
        raise argparse.ArgumentTypeError("must be a number from 0 through 360")
    return result


def _location_json(location: Location) -> Mapping[str, Any]:
    result = location.to_mapping()
    result["source"] = location.source
    return result


def _state_json(state: SceneState) -> Mapping[str, Any]:
    return {
        "phase": state.phase,
        "phase_blend": state.phase_blend,
        "ambient_brightness": state.ambient_brightness,
        "saturation_impression": state.saturation_impression,
        "stars_opacity": state.stars_opacity,
        "city_light_intensity": state.city_light_intensity,
        "sun": {
            "visible": state.sun_visible,
            "progress": state.sun_progress,
            "x": state.sun_x,
            "y": state.sun_y,
            "opacity": state.sun_opacity,
            "elevation": state.solar_elevation,
        },
        "moon": {
            "visible": state.moon_visible,
            "progress": state.moon_progress,
            "x": state.moon_x,
            "y": state.moon_y,
            "opacity": state.moon_opacity,
            "phase": state.moon_phase,
            "illumination": state.moon_illumination,
            "lunar_fill": state.lunar_fill,
        },
    }


def _facts_json(facts: DailyFacts) -> Mapping[str, Any]:
    return {
        "location": _location_json(facts.location),
        "astronomy": facts.astronomy.to_mapping(),
        "location_source": facts.location_source,
        "astronomy_source": facts.astronomy_source,
        "stale": facts.stale,
        "errors": list(facts.errors),
    }


def _effective_view_azimuth(
    astronomy: Astronomy, requested: Optional[float]
) -> float:
    if requested is not None:
        return requested % 360.0
    value = _solar_position_number(
        astronomy.solar_position.get("solar_noon_azimuth"), 0.0, 360.0
    )
    return value if value is not None else 0.0


def _weather_facts_json(facts: WeatherFacts) -> Mapping[str, Any]:
    return {
        "sample": facts.sample.to_mapping(),
        "state": facts.state.to_mapping(),
        "source": facts.source,
        "stale": facts.stale,
        "age_seconds": facts.age_seconds,
        "errors": list(facts.errors),
        "provider_current": (
            facts.timeline.current.to_mapping() if facts.timeline is not None else None
        ),
        "warnings": list(facts.timeline.warnings) if facts.timeline is not None else [],
    }


def _neutral_weather_facts(
    now: datetime,
    source: str,
    error: Optional[str] = None,
) -> WeatherFacts:
    sample = weather_module.neutral_weather_sample(now)
    return WeatherFacts(
        timeline=None,
        sample=sample,
        state=weather_module.neutral_weather_state(),
        source=source,
        stale=source == "neutral",
        age_seconds=None,
        errors=(error,) if error else (),
    )


def resolve_weather_facts(
    arguments: argparse.Namespace,
    facts: DailyFacts,
    client: HttpJsonClient,
    now: datetime,
    cache: Optional[weather_module.WeatherCache] = None,
) -> WeatherFacts:
    mode = getattr(arguments, "weather", "auto")
    preset = getattr(arguments, "weather_preset", None)
    view = _effective_view_azimuth(facts.astronomy, arguments.view_azimuth)
    if mode == "off":
        return _neutral_weather_facts(now, "off")
    if preset is not None:
        sample = weather_module.preset_weather_sample(preset, now)
        return WeatherFacts(
            timeline=None,
            sample=sample,
            state=weather_module.derive_weather_state(sample, view),
            source=f"preset:{preset}",
            stale=False,
            age_seconds=0.0,
        )
    if facts.location_source == "fallback":
        return _neutral_weather_facts(
            now,
            "neutral",
            "Open-Meteo: skipped because no valid location is available",
        )
    weather_cache = cache or weather_module.WeatherCache(
        getattr(arguments, "weather_cache", None)
    )
    provider = weather_module.OpenMeteoProvider(client)

    def fetch_weather(fetched_at: datetime) -> weather_module.WeatherTimeline:
        try:
            return provider.fetch(facts.location, fetched_at)
        except ProviderError as error:
            raise weather_module.WeatherError(_bounded_error(error)) from error

    try:
        result = weather_cache.resolve(
            facts.location,
            getattr(
                arguments,
                "weather_refresh",
                weather_module.DEFAULT_WEATHER_REFRESH_SECONDS,
            ),
            fetch_weather,
        )
    except (weather_module.WeatherCacheError, OSError, ValueError) as error:
        return _neutral_weather_facts(
            now, "neutral", f"Open-Meteo cache: {_bounded_error(error)}"
        )
    errors = []
    if result.error:
        errors.append(f"Open-Meteo: {result.error}")
    if result.timeline is None:
        neutral = _neutral_weather_facts(
            now,
            "neutral",
            errors[0] if errors else "Open-Meteo: no usable weather timeline",
        )
        return neutral
    sample = result.timeline.current
    state = weather_module.derive_weather_state(sample, view)
    errors.extend(result.timeline.warnings)
    return WeatherFacts(
        timeline=result.timeline,
        sample=sample,
        state=state,
        source=result.source,
        stale=result.stale,
        age_seconds=result.age_seconds,
        errors=tuple(errors),
    )


def resolve_daily_facts_offline(
    arguments: argparse.Namespace,
    cache: DailyCache,
    now: datetime,
) -> DailyFacts:
    latitude, longitude, timezone_name = _manual_values(arguments)
    errors = []
    if latitude is not None and longitude is not None and timezone_name is not None:
        location = manual_location(
            latitude, longitude, timezone_name, arguments.locality
        )
        location_source = "manual"
    else:
        cached_location = cache.last_success("ip-api")
        try:
            location = (
                Location.from_mapping(cached_location, source="cache")
                if cached_location is not None
                else Location(0.0, 0.0, "UTC", city="Unknown", source="fallback")
            )
            location_source = "cache" if cached_location is not None else "fallback"
        except ProviderError as error:
            location = Location(0.0, 0.0, "UTC", city="Unknown", source="fallback")
            location_source = "fallback"
            errors.append(f"cached location: {_bounded_error(error)}")
    local_day = now.astimezone(_zone(location.timezone)).date()
    astronomy_mapping = (
        cache.last_success("sunrise-sunset-v2", location.identity)
        if location_source != "fallback"
        else None
    )
    try:
        astronomy = (
            Astronomy.from_mapping(
                astronomy_mapping,
                expected_day=local_day,
                expected_timezone=location.timezone,
                source="cache",
            )
            if astronomy_mapping is not None
            else neutral_astronomy(local_day, location.timezone)
        )
        astronomy_source = "cache" if astronomy_mapping is not None else "fallback"
    except ProviderError as error:
        astronomy = neutral_astronomy(local_day, location.timezone)
        astronomy_source = "fallback"
        errors.append(f"cached astronomy: {_bounded_error(error)}")
    if astronomy_source == "fallback":
        errors.append("demo-weather uses neutral astronomy because no same-day cache is available")
    return DailyFacts(
        location=location,
        astronomy=astronomy,
        location_source=location_source,
        astronomy_source=astronomy_source,
        stale=location_source != "manual" or astronomy_source != "cache",
        errors=tuple(errors),
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Drive the dynamic Pixel City example from daily sun and moon data."
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--once", action="store_true", help="apply one update (default)")
    mode.add_argument("--loop", action="store_true", help="keep applying timed updates")
    mode.add_argument(
        "--demo-day",
        action="store_true",
        help="play one local 24-hour cycle using today's astronomy",
    )
    mode.add_argument(
        "--demo-weather",
        action="store_true",
        help="play every weather preset once without network access",
    )
    parser.add_argument("--status", action="store_true", help="print resolved state without IPC")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print a deterministic neutral preview without network, cache, files, or IPC",
    )
    parser.add_argument("--at", type=_aware_datetime, help="evaluate an aware ISO-8601 time")
    parser.add_argument("--latitude", type=float)
    parser.add_argument("--longitude", type=float)
    parser.add_argument("--timezone", dest="timezone_name")
    parser.add_argument("--locality", default="Manual location")
    parser.add_argument(
        "--view-azimuth",
        type=_azimuth_argument,
        help=(
            "viewer-facing compass azimuth in degrees, 0..360; "
            "defaults to solar-noon azimuth"
        ),
    )
    parser.add_argument("--interval", type=int, default=60, help="loop interval, 15..3600 seconds")
    parser.add_argument(
        "--demo-seconds",
        type=float,
        default=DEFAULT_DEMO_SECONDS,
        help="real duration of --demo-day, 1..3600 seconds (default: 60)",
    )
    parser.add_argument(
        "--demo-step",
        type=float,
        default=DEFAULT_DEMO_STEP_SECONDS,
        help="real delay between demo frames, 0.25..5 seconds (default: 1)",
    )
    parser.add_argument("--cache", type=Path)
    parser.add_argument(
        "--weather",
        choices=("auto", "off"),
        default="auto",
        help="resolve Open-Meteo weather or preserve astronomy unchanged",
    )
    parser.add_argument(
        "--weather-refresh",
        type=int,
        default=weather_module.DEFAULT_WEATHER_REFRESH_SECONDS,
        help="Open-Meteo refresh interval in seconds (600..21600)",
    )
    parser.add_argument("--weather-cache", type=Path)
    parser.add_argument(
        "--weather-preset",
        choices=weather_module.WEATHER_PRESETS,
        help="bypass weather provider/cache with a deterministic condition",
    )
    parser.add_argument(
        "--example-dir",
        type=Path,
        default=Path(__file__).resolve().parent,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--hyprlax-bin", default=os.environ.get("HYPRLAX_BIN", "hyprlax")
    )
    return parser


def _manual_values(
    arguments: argparse.Namespace,
) -> Tuple[Optional[float], Optional[float], Optional[str]]:
    return arguments.latitude, arguments.longitude, arguments.timezone_name


def _preview(arguments: argparse.Namespace, now: datetime) -> Mapping[str, Any]:
    latitude, longitude, timezone_name = _manual_values(arguments)
    if timezone_name is None:
        timezone_name = getattr(now.tzinfo, "key", None) or "UTC"
    if latitude is None:
        location = Location(0.0, 0.0, timezone_name, city="Preview", source="preview")
    else:
        location = manual_location(latitude, longitude, timezone_name, arguments.locality)
    local_day = now.astimezone(_zone(location.timezone)).date()
    astronomy = neutral_astronomy(local_day, location.timezone)
    weather_sample = weather_module.neutral_weather_sample(now)
    weather_state = weather_module.neutral_weather_state()
    state = compose_weather_scene(
        compute_scene_state(now, astronomy, arguments.view_azimuth), weather_state
    )
    managed = assumed_managed_layers(arguments.example_dir)
    commands = build_ipc_commands(
        managed, state, plan_asset_paths(managed), weather_state
    )
    return {
        "mode": "dry-run",
        "source": "preview",
        "assumed_ids": True,
        "location": _location_json(location),
        "astronomy": astronomy.to_mapping(),
        "weather": {
            "sample": weather_sample.to_mapping(),
            "state": weather_state.to_mapping(),
            "source": "preview-neutral",
        },
        "state": _state_json(state),
        "commands": [command.as_json() for command in commands],
    }


def _resolve(
    arguments: argparse.Namespace,
    cache: DailyCache,
    client: HttpJsonClient,
    now: datetime,
) -> DailyFacts:
    return resolve_daily_facts(
        cache,
        client,
        now=now,
        latitude=arguments.latitude,
        longitude=arguments.longitude,
        timezone_name=arguments.timezone_name,
        locality=arguments.locality,
    )


def _run_demo_day(
    arguments: argparse.Namespace,
    controller: SceneController,
    facts: DailyFacts,
    anchor: datetime,
    sleep: Callable[[float], None],
    monotonic: Callable[[], float],
    weather_facts: Optional[WeatherFacts] = None,
) -> int:
    zone = _zone(facts.location.timezone)
    local_day = anchor.astimezone(zone).date()
    day_start = datetime.combine(local_day, time.min, zone)
    real_start = monotonic()

    while True:
        elapsed = _clamp(monotonic() - real_start, 0.0, arguments.demo_seconds)
        progress = elapsed / arguments.demo_seconds
        if progress >= 1.0:
            simulated = day_start + timedelta(days=1) - timedelta(microseconds=1)
        else:
            simulated = day_start + timedelta(seconds=SECONDS_PER_DAY * progress)
        astronomy_scene = compute_scene_state(
            simulated, facts.astronomy, arguments.view_azimuth
        )
        if weather_facts is not None and weather_facts.timeline is not None:
            _, weather_state = weather_module.weather_state_at(
                weather_facts.timeline,
                simulated,
                _effective_view_azimuth(facts.astronomy, arguments.view_azimuth),
            )
        elif weather_facts is not None:
            weather_state = weather_facts.state
        else:
            weather_state = weather_module.neutral_weather_state()
        state = compose_weather_scene(astronomy_scene, weather_state)
        commands = controller.apply_once(state, weather_state)
        payload = {
            "mode": "demo-day",
            "simulated_at": simulated.isoformat(),
            "progress": round(progress, 6),
            "phase": state.phase,
            "sun_x": round(state.sun_x, 6),
            "sun_y": round(state.sun_y, 6),
            "weather_condition": weather_state.condition,
            "commands_applied": len(commands),
            "location_source": facts.location_source,
            "astronomy_source": facts.astronomy_source,
            "stale": facts.stale,
            "errors": list(facts.errors),
        }
        print(json.dumps(payload, sort_keys=True), flush=True)
        if progress >= 1.0:
            return 0

        elapsed_after_apply = max(0.0, monotonic() - real_start)
        next_step = (
            math.floor(elapsed_after_apply / arguments.demo_step) + 1
        ) * arguments.demo_step
        next_step = min(arguments.demo_seconds, next_step)
        delay = max(0.0, real_start + next_step - monotonic())
        sleep(delay)


def _blend_weather_states(
    start: weather_module.WeatherState,
    end: weather_module.WeatherState,
    amount: float,
) -> weather_module.WeatherState:
    blend = _smoothstep(amount)
    precipitation_type = (
        start.precipitation_type if blend < 0.5 else end.precipitation_type
    )
    condition = start.condition if blend < 0.5 else end.condition
    return weather_module.WeatherState(
        condition=condition,
        cloud_intensity=_clamp(_lerp(start.cloud_intensity, end.cloud_intensity, blend)),
        fog_intensity=_clamp(_lerp(start.fog_intensity, end.fog_intensity, blend)),
        precipitation_type=precipitation_type,
        precipitation_intensity=_clamp(
            _lerp(start.precipitation_intensity, end.precipitation_intensity, blend)
        ),
        wind_intensity=_clamp(_lerp(start.wind_intensity, end.wind_intensity, blend)),
        wind_screen_direction=_clamp(
            _lerp(start.wind_screen_direction, end.wind_screen_direction, blend),
            -1.0,
            1.0,
        ),
        heat_intensity=_clamp(_lerp(start.heat_intensity, end.heat_intensity, blend)),
        snow_cover=_clamp(_lerp(start.snow_cover, end.snow_cover, blend)),
    )


def _run_demo_weather(
    arguments: argparse.Namespace,
    controller: SceneController,
    facts: DailyFacts,
    anchor: datetime,
    sleep: Callable[[float], None],
    monotonic: Callable[[], float],
) -> int:
    presets = weather_module.WEATHER_PRESETS
    real_start = monotonic()
    astronomy_scene = compute_scene_state(
        anchor, facts.astronomy, arguments.view_azimuth
    )
    view = _effective_view_azimuth(facts.astronomy, arguments.view_azimuth)
    while True:
        elapsed = _clamp(monotonic() - real_start, 0.0, arguments.demo_seconds)
        progress = elapsed / arguments.demo_seconds
        scaled = min(len(presets) - 1e-9, progress * len(presets))
        preset_index = min(len(presets) - 1, int(scaled))
        segment_progress = scaled - preset_index
        preset = presets[preset_index]
        target_sample = weather_module.preset_weather_sample(preset, anchor)
        target_state = weather_module.derive_weather_state(target_sample, view)
        if preset_index == 0:
            weather_state = target_state
        else:
            previous_sample = weather_module.preset_weather_sample(
                presets[preset_index - 1], anchor
            )
            previous_state = weather_module.derive_weather_state(previous_sample, view)
            transition = _clamp(segment_progress / 0.35)
            weather_state = _blend_weather_states(
                previous_state, target_state, transition
            )
        scene = compose_weather_scene(astronomy_scene, weather_state)
        commands = controller.apply_once(scene, weather_state)
        print(
            json.dumps(
                {
                    "mode": "demo-weather",
                    "progress": round(progress, 6),
                    "preset": preset,
                    "condition": weather_state.condition,
                    "commands_applied": len(commands),
                    "network": "disabled",
                    "astronomy_source": facts.astronomy_source,
                    "errors": list(facts.errors),
                },
                sort_keys=True,
            ),
            flush=True,
        )
        if progress >= 1.0:
            return 0
        elapsed_after_apply = max(0.0, monotonic() - real_start)
        next_step = (
            math.floor(elapsed_after_apply / arguments.demo_step) + 1
        ) * arguments.demo_step
        next_step = min(arguments.demo_seconds, next_step)
        delay = max(0.0, real_start + next_step - monotonic())
        sleep(delay)


def main(
    argv: Optional[Tuple[str, ...]] = None,
    *,
    ipc_factory: Callable[..., HyprlaxIPC] = HyprlaxIPC,
    client_factory: Callable[[], HttpJsonClient] = HttpJsonClient,
    sleep: Callable[[float], None] = time_module.sleep,
    monotonic: Callable[[], float] = time_module.monotonic,
    now_provider: Callable[[], datetime] = lambda: datetime.now().astimezone(),
) -> int:
    parser = _parser()
    arguments = parser.parse_args(argv)
    manual = _manual_values(arguments)
    if any(value is not None for value in manual) and not all(
        value is not None for value in manual
    ):
        parser.error("--latitude, --longitude, and --timezone must be supplied together")
    if not 15 <= arguments.interval <= 3600:
        parser.error("--interval must be between 15 and 3600 seconds")
    if not 1.0 <= arguments.demo_seconds <= 3600.0:
        parser.error("--demo-seconds must be between 1 and 3600 seconds")
    if not 0.25 <= arguments.demo_step <= 5.0:
        parser.error("--demo-step must be between 0.25 and 5 seconds")
    if arguments.demo_step > arguments.demo_seconds:
        parser.error("--demo-step cannot exceed --demo-seconds")
    if not (
        weather_module.MIN_WEATHER_REFRESH_SECONDS
        <= arguments.weather_refresh
        <= weather_module.MAX_WEATHER_REFRESH_SECONDS
    ):
        parser.error("--weather-refresh must be between 600 and 21600 seconds")
    if arguments.weather == "off" and arguments.weather_preset is not None:
        parser.error("--weather off cannot be combined with --weather-preset")
    if arguments.loop and (arguments.status or arguments.dry_run):
        parser.error("--loop cannot be combined with --status or --dry-run")
    if arguments.demo_day and (arguments.status or arguments.dry_run):
        parser.error("--demo-day cannot be combined with --status or --dry-run")
    if arguments.demo_weather and (arguments.status or arguments.dry_run):
        parser.error("--demo-weather cannot be combined with --status or --dry-run")

    def current_time() -> datetime:
        value = arguments.at or now_provider()
        if value.tzinfo is None:
            raise ValueError("current time must be timezone-aware")
        return value

    try:
        if arguments.dry_run:
            print(json.dumps(_preview(arguments, current_time()), sort_keys=True))
            return 0

        cache = DailyCache(arguments.cache)
        if arguments.demo_weather:
            anchor = current_time()
            facts = resolve_daily_facts_offline(arguments, cache, anchor)
            controller = SceneController(
                ipc_factory(binary=arguments.hyprlax_bin), arguments.example_dir
            )
            return _run_demo_weather(
                arguments, controller, facts, anchor, sleep, monotonic
            )

        client = client_factory()
        weather_cache = weather_module.WeatherCache(arguments.weather_cache)
        if arguments.status:
            now = current_time()
            facts = _resolve(arguments, cache, client, now)
            weather_facts = resolve_weather_facts(
                arguments, facts, client, now, weather_cache
            )
            state = compose_weather_scene(
                compute_scene_state(now, facts.astronomy, arguments.view_azimuth),
                weather_facts.state,
            )
            payload = {
                "mode": "status",
                "facts": _facts_json(facts),
                "weather": _weather_facts_json(weather_facts),
                "state": _state_json(state),
            }
            print(json.dumps(payload, sort_keys=True))
            return 0

        controller = SceneController(
            ipc_factory(binary=arguments.hyprlax_bin), arguments.example_dir
        )
        if arguments.demo_day:
            anchor = current_time()
            facts = _resolve(arguments, cache, client, anchor)
            weather_facts = resolve_weather_facts(
                arguments, facts, client, anchor, weather_cache
            )
            return _run_demo_day(
                arguments,
                controller,
                facts,
                anchor,
                sleep,
                monotonic,
                weather_facts,
            )
        while True:
            now = current_time()
            try:
                facts = _resolve(arguments, cache, client, now)
                weather_facts = resolve_weather_facts(
                    arguments, facts, client, now, weather_cache
                )
                state = compose_weather_scene(
                    compute_scene_state(
                        now, facts.astronomy, arguments.view_azimuth
                    ),
                    weather_facts.state,
                )
                commands = controller.apply_once(state, weather_facts.state)
                payload = {
                    "mode": "loop" if arguments.loop else "once",
                    "phase": state.phase,
                    "commands_applied": len(commands),
                    "location_source": facts.location_source,
                    "astronomy_source": facts.astronomy_source,
                    "weather_source": weather_facts.source,
                    "weather_condition": weather_facts.state.condition,
                    "weather_age_seconds": weather_facts.age_seconds,
                    "stale": facts.stale or weather_facts.stale,
                    "errors": [*facts.errors, *weather_facts.errors],
                }
                print(json.dumps(payload, sort_keys=True), flush=True)
            except (CacheError, IPCError, ProviderError, OSError, ValueError) as error:
                if not arguments.loop:
                    raise
                print(
                    f"dynamic pixel city update failed: {_bounded_error(error)}",
                    file=sys.stderr,
                )
            if not arguments.loop:
                return 0
            sleep(arguments.interval)
    except KeyboardInterrupt:
        return 0
    except (CacheError, IPCError, ProviderError, OSError, ValueError) as error:
        print(f"dynamic pixel city failed: {_bounded_error(error)}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
