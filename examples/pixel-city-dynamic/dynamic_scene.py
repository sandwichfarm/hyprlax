#!/usr/bin/env python3
"""Daily location and astronomy foundation for the dynamic Pixel City example."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import date, datetime, time, timedelta, timezone as datetime_timezone
import fcntl
import json
import math
import os
from pathlib import Path
import tempfile
from typing import Any, Callable, Dict, Mapping, Optional, Tuple
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError


CACHE_SCHEMA_VERSION = 1
MAX_RESPONSE_BYTES = 262144
HTTP_TIMEOUT_SECONDS = 10
MAX_ERROR_LENGTH = 512
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


def _sun_state(
    now: datetime, astronomy: Astronomy
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
    sunrise = astronomy.sunrise or neutral_astronomy(astronomy.day, astronomy.timezone).sunrise
    sunset = astronomy.sunset or neutral_astronomy(astronomy.day, astronomy.timezone).sunset
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


def compute_scene_state(now: datetime, astronomy: Astronomy) -> SceneState:
    if now.tzinfo is None:
        raise ValueError("now must be timezone-aware")
    phase, phase_blend, ambient, colorfulness, stars, city, looks = _lighting_at(now, astronomy)
    sun_visible, sun_progress, sun_x, sun_y, sun_opacity, solar_elevation = _sun_state(
        now, astronomy
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
                    error=entry.get("last_error") if isinstance(entry.get("last_error"), str) else None,
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
        solar_position={
            "sunrise_azimuth": 90.0,
            "sunset_azimuth": 270.0,
            "solar_noon_azimuth": 180.0,
            "solar_noon_altitude": 45.0,
        },
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
        initial_zone = _zone(cached_location.timezone) if cached_location is not None else current.tzinfo
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


if __name__ == "__main__":
    raise SystemExit(
        "dynamic_scene.py is the importable scene foundation; the controller CLI arrives in Phase 4"
    )
