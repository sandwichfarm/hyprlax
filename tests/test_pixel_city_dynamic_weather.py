#!/usr/bin/env python3
"""Deterministic provider, cache, model, and asset tests for Pixel City weather."""

from __future__ import annotations

import copy
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone
import importlib.util
import inspect
import json
import math
import multiprocessing
from pathlib import Path
import stat
import struct
import sys
import tempfile
import time
from typing import Any, Mapping, Sequence
import unittest
from urllib.parse import parse_qs, urlsplit


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "examples" / "pixel-city-dynamic" / "weather.py"
FIXTURE_PATH = (
    ROOT
    / "tests"
    / "fixtures"
    / "pixel-city-weather"
    / "open-meteo-valid.json"
)
SPEC = importlib.util.spec_from_file_location("pixel_city_dynamic_weather", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"could not load {MODULE_PATH}")
WEATHER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = WEATHER
SPEC.loader.exec_module(WEATHER)

SCENE_PATH = ROOT / "examples" / "pixel-city-dynamic" / "dynamic_scene.py"
SCENE_SPEC = importlib.util.spec_from_file_location(
    "pixel_city_dynamic_weather_scene", SCENE_PATH
)
if SCENE_SPEC is None or SCENE_SPEC.loader is None:
    raise RuntimeError(f"could not load {SCENE_PATH}")
SCENE = importlib.util.module_from_spec(SCENE_SPEC)
sys.modules[SCENE_SPEC.name] = SCENE
SCENE_SPEC.loader.exec_module(SCENE)

FIXED_NOW = datetime(2026, 7, 12, 8, 30, tzinfo=timezone.utc)


@dataclass(frozen=True)
class LocationStub:
    latitude: float = 47.4979
    longitude: float = 19.0402
    timezone: str = "Europe/Budapest"
    forced_identity: str | None = None

    @property
    def identity(self) -> str:
        if self.forced_identity is not None:
            return self.forced_identity
        return f"{self.latitude:.4f},{self.longitude:.4f},{self.timezone}"


class StubClient:
    def __init__(self, *responses: Any) -> None:
        self.responses = list(responses)
        self.calls: list[str] = []

    def get_json(self, url: str) -> Mapping[str, Any]:
        self.calls.append(url)
        if not self.responses:
            raise AssertionError(f"unexpected network request: {url}")
        response = self.responses.pop(0)
        if isinstance(response, BaseException):
            raise response
        return response


class MutableClock:
    def __init__(self, value: datetime) -> None:
        self.value = value

    def __call__(self) -> datetime:
        return self.value


def fixture_payload() -> dict[str, Any]:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))


def provider_timeline(
    fetched_at: datetime = FIXED_NOW,
    location: LocationStub = LocationStub(),
    payload: Mapping[str, Any] | None = None,
):
    client = StubClient(copy.deepcopy(payload if payload is not None else fixture_payload()))
    return WEATHER.OpenMeteoProvider(client).fetch(location, fetched_at)


def sample(**changes: Any):
    base = WEATHER.neutral_weather_sample(FIXED_NOW)
    return replace(base, **changes)


def timeline_with(samples: Sequence[Any], fetched_at: datetime = FIXED_NOW):
    location = LocationStub()
    return WEATHER.WeatherTimeline(
        location_identity=location.identity,
        timezone=location.timezone,
        requested_latitude=location.latitude,
        requested_longitude=location.longitude,
        response_latitude=47.5,
        response_longitude=19.0625,
        fetched_at=fetched_at,
        current=samples[0],
        hourly=tuple(samples),
    )


def weather_cache_worker(
    cache_path: str,
    counter_path: str,
    start_event: Any,
    result_queue: Any,
    fail: bool,
) -> None:
    location = LocationStub()
    cache = WEATHER.WeatherCache(Path(cache_path), clock=lambda: FIXED_NOW)
    start_event.wait(5)

    def fetch(now: datetime):
        with Path(counter_path).open("a", encoding="utf-8") as handle:
            handle.write("fetch\n")
            handle.flush()
        time.sleep(0.2)
        if fail:
            raise WEATHER.WeatherError("offline")
        return provider_timeline(now, location)

    result = cache.resolve(location, 900, fetch)
    result_queue.put((result.source, result.attempted, result.error))


def gif_metadata(payload: bytes) -> dict[str, Any]:
    """Parse structural GIF metadata independently from production decoder."""
    if not isinstance(payload, bytes) or payload[:6] not in (b"GIF87a", b"GIF89a"):
        raise AssertionError("generated payload is not a GIF")
    if len(payload) < 14:
        raise AssertionError("truncated GIF header")
    width, height = struct.unpack_from("<HH", payload, 6)
    packed = payload[10]
    offset = 13
    if packed & 0x80:
        offset += 3 * (1 << ((packed & 0x07) + 1))
    frames = 0
    delays: list[int] = []
    transparent_frames = 0
    loop_count: int | None = None

    def subblocks(position: int) -> tuple[bytes, int]:
        parts = []
        while True:
            if position >= len(payload):
                raise AssertionError("truncated GIF sub-block")
            size = payload[position]
            position += 1
            if size == 0:
                return b"".join(parts), position
            if position + size > len(payload):
                raise AssertionError("truncated GIF sub-block payload")
            parts.append(payload[position : position + size])
            position += size

    pending_delay = None
    pending_transparency = False
    while offset < len(payload):
        marker = payload[offset]
        offset += 1
        if marker == 0x3B:
            break
        if marker == 0x21:
            if offset >= len(payload):
                raise AssertionError("truncated GIF extension")
            label = payload[offset]
            offset += 1
            if label == 0xF9:
                if offset + 6 > len(payload) or payload[offset] != 4:
                    raise AssertionError("invalid GIF graphics-control extension")
                control = payload[offset + 1]
                pending_delay = struct.unpack_from("<H", payload, offset + 2)[0]
                pending_transparency = bool(control & 0x01)
                offset += 6
            elif label == 0xFF:
                if offset >= len(payload):
                    raise AssertionError("truncated GIF application extension")
                size = payload[offset]
                offset += 1
                identifier = payload[offset : offset + size]
                offset += size
                data, offset = subblocks(offset)
                if identifier.startswith((b"NETSCAPE", b"ANIMEXTS")) and len(data) >= 3:
                    loop_count = struct.unpack_from("<H", data, 1)[0]
            else:
                _, offset = subblocks(offset)
            continue
        if marker != 0x2C:
            raise AssertionError(f"unknown GIF block marker 0x{marker:02x}")
        if offset + 9 > len(payload):
            raise AssertionError("truncated GIF image descriptor")
        image_packed = payload[offset + 8]
        offset += 9
        if image_packed & 0x80:
            offset += 3 * (1 << ((image_packed & 0x07) + 1))
        if offset >= len(payload):
            raise AssertionError("missing GIF LZW code size")
        offset += 1
        _, offset = subblocks(offset)
        frames += 1
        delays.append(0 if pending_delay is None else pending_delay)
        transparent_frames += int(pending_transparency)
        pending_delay = None
        pending_transparency = False
    return {
        "width": width,
        "height": height,
        "frames": frames,
        "delays": tuple(delays),
        "transparent_frames": transparent_frames,
        "loop_count": loop_count,
    }


def _call_with_aliases(function: Any, values: Mapping[str, Any]) -> Any:
    signature = inspect.signature(function)
    arguments: dict[str, Any] = {}
    aliases = {
        "effect": "kind",
        "weather_kind": "kind",
        "layer": "kind",
        "layer_name": "kind",
        "weather_state": "state",
        "source_rgba": "pixels",
        "source_pixels": "pixels",
        "rgba": "pixels",
        "image_path": "source_path",
        "path": "source_path",
        "frame_delays": "delays",
        "delays_cs": "delays",
        "delay_cs": "delay",
        "delay_centiseconds": "delay",
        "transparent": "transparent_index",
    }
    for name, parameter in signature.parameters.items():
        key = name if name in values else aliases.get(name)
        if key is not None and key in values:
            arguments[name] = values[key]
        elif parameter.default is inspect.Parameter.empty:
            raise AssertionError(
                f"unsupported required parameter {name!r} in {function.__name__}{signature}"
            )
    return function(**arguments)


def _gif_bytes(value: Any) -> bytes:
    if isinstance(value, bytes):
        return value
    if isinstance(value, Mapping):
        for key in ("gif", "payload", "data", "bytes"):
            if isinstance(value.get(key), bytes):
                return value[key]
    for name in ("gif", "payload", "data"):
        result = getattr(value, name, None)
        if isinstance(result, bytes):
            return result
    if isinstance(value, tuple) and value and isinstance(value[0], bytes):
        return value[0]
    raise AssertionError("GIF API must return bytes or a result carrying GIF bytes")


def _decoded_frames(value: Any) -> Sequence[Any]:
    if isinstance(value, Mapping):
        result = value.get("frames")
    else:
        result = getattr(value, "frames", None)
        if result is None and isinstance(value, tuple) and len(value) >= 3:
            result = value[2]
    if not isinstance(result, Sequence):
        raise AssertionError("decode_palette_gif must expose decoded frames")
    return result


def _frame_pixels(frame: Any) -> bytes:
    if isinstance(frame, bytes):
        return frame
    if isinstance(frame, Mapping):
        for key in ("pixels", "indices", "rgba", "data"):
            if isinstance(frame.get(key), bytes):
                return frame[key]
    for name in ("pixels", "indices", "rgba", "data"):
        value = getattr(frame, name, None)
        if isinstance(value, bytes):
            return value
    raise AssertionError("decoded GIF frame must expose bytes")


class OpenMeteoProviderTests(unittest.TestCase):
    def test_exact_query_contains_current_and_hourly_without_api_key(self):
        location = LocationStub()
        url = WEATHER.OpenMeteoProvider.request_url(location)
        parsed = urlsplit(url)
        self.assertEqual(WEATHER.OPEN_METEO_URL, f"{parsed.scheme}://{parsed.netloc}{parsed.path}")
        query = parse_qs(parsed.query, keep_blank_values=True)
        self.assertEqual(
            {
                "latitude",
                "longitude",
                "timezone",
                "forecast_days",
                "temperature_unit",
                "wind_speed_unit",
                "precipitation_unit",
                "current",
                "hourly",
            },
            set(query),
        )
        self.assertEqual(["47.4979"], query["latitude"])
        self.assertEqual(["19.0402"], query["longitude"])
        self.assertEqual(["Europe/Budapest"], query["timezone"])
        self.assertEqual(["2"], query["forecast_days"])
        self.assertEqual(["celsius"], query["temperature_unit"])
        self.assertEqual(["ms"], query["wind_speed_unit"])
        self.assertEqual(["mm"], query["precipitation_unit"])
        expected_fields = ",".join(WEATHER.WEATHER_FIELDS)
        self.assertEqual([expected_fields], query["current"])
        self.assertEqual([expected_fields], query["hourly"])
        lowered = url.lower()
        self.assertNotIn("api_key", lowered)
        self.assertNotIn("apikey", lowered)
        self.assertNotIn("token", lowered)

    def test_valid_fixture_normalizes_every_field_and_local_timestamps(self):
        client = StubClient(fixture_payload())
        timeline = WEATHER.OpenMeteoProvider(client).fetch(LocationStub(), FIXED_NOW)
        self.assertEqual(1, len(client.calls))
        self.assertEqual(LocationStub().identity, timeline.location_identity)
        self.assertEqual("Europe/Budapest", timeline.timezone)
        self.assertEqual(4, len(timeline.hourly))
        self.assertEqual(1, timeline.current.weather_code)
        self.assertEqual(19.0, timeline.current.temperature_c)
        self.assertEqual(350.0, timeline.current.wind_direction_degrees)
        self.assertEqual("Europe/Budapest", timeline.current.timestamp.tzinfo.key)
        self.assertEqual(10, timeline.hourly[0].timestamp.hour)
        self.assertEqual(
            {
                "timestamp",
                "weather_code",
                "temperature_c",
                "apparent_temperature_c",
                "cloud_cover_percent",
                "visibility_m",
                "precipitation_mm",
                "rain_mm",
                "showers_mm",
                "snowfall_cm",
                "snow_depth_m",
                "wind_speed_ms",
                "wind_direction_degrees",
                "wind_gusts_ms",
            },
            set(WEATHER.WeatherSample.__dataclass_fields__),
        )

    def test_every_declared_unit_is_required_for_current_and_hourly(self):
        for group in ("current_units", "hourly_units"):
            fields = ("time", *WEATHER.WEATHER_FIELDS)
            if group == "current_units":
                fields = (*fields, "interval")
            for field in fields:
                payload = fixture_payload()
                payload[group][field] = "wrong-unit"
                with self.subTest(group=group, field=field):
                    with self.assertRaises(WEATHER.WeatherError):
                        WEATHER.OpenMeteoProvider(StubClient(payload)).fetch(
                            LocationStub(), FIXED_NOW
                        )

    def test_malformed_lengths_nonfinite_ranges_and_timestamps_fail_closed(self):
        cases = []
        missing = fixture_payload()
        del missing["current"]["rain"]
        cases.append(("missing current field", missing))
        short = fixture_payload()
        short["hourly"]["wind_gusts_10m"].pop()
        cases.append(("mismatched hourly length", short))
        nonfinite = fixture_payload()
        nonfinite["current"]["temperature_2m"] = float("nan")
        cases.append(("non-finite", nonfinite))
        range_error = fixture_payload()
        range_error["hourly"]["cloud_cover"][1] = 101.0
        cases.append(("out of range", range_error))
        fractional_code = fixture_payload()
        fractional_code["hourly"]["weather_code"][0] = 1.5
        cases.append(("fractional weather code", fractional_code))
        duplicate_time = fixture_payload()
        duplicate_time["hourly"]["time"][2] = duplicate_time["hourly"]["time"][1]
        cases.append(("duplicate timestamp", duplicate_time))
        bad_time = fixture_payload()
        bad_time["hourly"]["time"][0] = "not-a-time"
        cases.append(("malformed timestamp", bad_time))
        bad_interval = fixture_payload()
        bad_interval["current"]["interval"] = 0
        cases.append(("invalid current interval", bad_interval))
        for name, payload in cases:
            with self.subTest(name=name):
                with self.assertRaises(WEATHER.WeatherError):
                    WEATHER.OpenMeteoProvider(StubClient(payload)).fetch(
                        LocationStub(), FIXED_NOW
                    )

    def test_wrong_timezone_and_utc_offset_fail_closed(self):
        wrong_timezone = fixture_payload()
        wrong_timezone["timezone"] = "UTC"
        wrong_offset = fixture_payload()
        wrong_offset["utc_offset_seconds"] = 0
        for name, payload in (
            ("timezone", wrong_timezone),
            ("utc offset", wrong_offset),
        ):
            with self.subTest(name=name):
                with self.assertRaises(WEATHER.WeatherError):
                    WEATHER.OpenMeteoProvider(StubClient(payload)).fetch(
                        LocationStub(), FIXED_NOW
                    )

    def test_non_object_response_root_fails_closed(self):
        with self.assertRaisesRegex(WEATHER.WeatherError, "root must be an object"):
            WEATHER.OpenMeteoProvider(StubClient([])).fetch(
                LocationStub(), FIXED_NOW
            )

    def test_unknown_wmo_codes_warn_but_numeric_fields_still_apply(self):
        payload = fixture_payload()
        payload["current"]["weather_code"] = 350
        payload["current"]["rain"] = 3.0
        payload["current"]["precipitation"] = 3.0
        timeline = WEATHER.OpenMeteoProvider(StubClient(payload)).fetch(
            LocationStub(), FIXED_NOW
        )
        state = WEATHER.derive_weather_state(timeline.current)
        self.assertEqual("rain", state.precipitation_type)
        self.assertGreater(state.precipitation_intensity, 0.0)
        self.assertEqual(1, len(timeline.warnings))
        self.assertIn("350", timeline.warnings[0])
        self.assertLessEqual(len(timeline.warnings[0]), WEATHER.MAX_ERROR_LENGTH)


class WeatherCacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "weather-v1.json"
        self.clock = MutableClock(FIXED_NOW)
        self.location = LocationStub()
        self.cache = WEATHER.WeatherCache(self.path, clock=self.clock)

    def success(self, now: datetime):
        return provider_timeline(now, self.location)

    def test_utc_bucket_and_refresh_range(self):
        same_instant_local = FIXED_NOW.astimezone(timezone(timedelta(hours=2)))
        self.assertEqual(
            self.cache._bucket(FIXED_NOW, 900),
            self.cache._bucket(same_instant_local, 900),
        )
        for refresh in (599, 21601):
            with self.subTest(refresh=refresh), self.assertRaises(ValueError):
                self.cache.resolve(self.location, refresh, self.success)

    def test_success_and_failure_attempt_only_once_per_bucket(self):
        calls: list[str] = []

        def success(now: datetime):
            calls.append("success")
            return self.success(now)

        first = self.cache.resolve(self.location, 900, success)
        second = self.cache.resolve(self.location, 900, success)
        self.assertEqual(["success"], calls)
        self.assertEqual("fresh", first.source)
        self.assertEqual("cache", second.source)
        self.assertFalse(second.attempted)

        other_path = Path(self.temporary.name) / "failed.json"
        failed_cache = WEATHER.WeatherCache(other_path, clock=self.clock)

        def fail(now: datetime):
            calls.append("fail")
            raise WEATHER.WeatherError("offline")

        failed_first = failed_cache.resolve(self.location, 900, fail)
        failed_second = failed_cache.resolve(self.location, 900, fail)
        self.assertEqual(1, calls.count("fail"))
        self.assertEqual("neutral", failed_first.source)
        self.assertEqual("neutral", failed_second.source)
        self.assertFalse(failed_second.attempted)
        self.assertIn("offline", failed_second.error)

    def test_stale_cutoff_accepts_exactly_three_hours_then_rejects_older(self):
        self.cache.resolve(self.location, 900, self.success)

        def fail(now: datetime):
            raise WEATHER.WeatherError("offline")

        self.clock.value = FIXED_NOW + timedelta(hours=3)
        exact = self.cache.resolve(self.location, 900, fail)
        self.assertEqual("stale", exact.source)
        self.assertIsNotNone(exact.timeline)
        self.assertEqual(10800.0, exact.age_seconds)

        self.clock.value += timedelta(microseconds=1)
        expired = self.cache.resolve(self.location, 900, fail)
        self.assertEqual("neutral", expired.source)
        self.assertIsNone(expired.timeline)
        self.assertGreater(expired.age_seconds, 10800.0)

    def test_wrong_location_and_timezone_never_reuse_last_good(self):
        self.cache.resolve(self.location, 900, self.success)
        self.clock.value += timedelta(minutes=15)

        def fail(now: datetime):
            raise WEATHER.WeatherError("offline")

        other_location = LocationStub(latitude=48.2082, longitude=16.3738)
        other = self.cache.resolve(other_location, 900, fail)
        self.assertEqual("neutral", other.source)
        self.assertIsNone(other.timeline)

        forced_identity = self.location.identity
        wrong_zone = LocationStub(
            latitude=self.location.latitude,
            longitude=self.location.longitude,
            timezone="UTC",
            forced_identity=forced_identity,
        )
        self.clock.value += timedelta(minutes=15)
        wrong = self.cache.resolve(wrong_zone, 900, fail)
        self.assertEqual("neutral", wrong.source)
        self.assertIsNone(wrong.timeline)

    def test_forged_cached_coordinates_and_late_response_never_overwrite(self):
        self.cache.resolve(self.location, 900, self.success)
        cached = json.loads(self.path.read_text(encoding="utf-8"))
        timeline = cached["locations"][self.location.identity]["last_success"]["timeline"]
        timeline["requested_latitude"] = 40.0
        self.path.write_text(json.dumps(cached), encoding="utf-8")
        self.clock.value += timedelta(minutes=15)
        forged = self.cache.resolve(
            self.location,
            900,
            lambda now: (_ for _ in ()).throw(WEATHER.WeatherError("offline")),
        )
        self.assertEqual("neutral", forged.source)
        self.assertIsNone(forged.timeline)

        race_path = Path(self.temporary.name) / "race.json"
        race_clock = MutableClock(FIXED_NOW)
        race_cache = WEATHER.WeatherCache(race_path, clock=race_clock)

        def older_fetch(old_now: datetime):
            race_clock.value = FIXED_NOW + timedelta(minutes=15)
            newer = race_cache.resolve(
                self.location,
                900,
                lambda new_now: provider_timeline(new_now, self.location),
            )
            self.assertEqual("fresh", newer.source)
            return provider_timeline(old_now, self.location)

        result = race_cache.resolve(self.location, 900, older_fetch)
        self.assertIsNotNone(result.timeline)
        self.assertEqual(
            FIXED_NOW + timedelta(minutes=15), result.timeline.fetched_at
        )

    def test_cache_write_is_private_valid_and_atomic(self):
        self.cache.resolve(self.location, 900, self.success)
        parsed = json.loads(self.path.read_text(encoding="utf-8"))
        self.assertEqual(WEATHER.WEATHER_CACHE_SCHEMA_VERSION, parsed["schema_version"])
        self.assertEqual(0o600, stat.S_IMODE(self.path.stat().st_mode))
        leftovers = list(self.path.parent.glob(f".{self.path.name}.*"))
        self.assertEqual([], leftovers)

    def test_malformed_cache_reports_bounded_failure(self):
        self.path.write_text("{not-json", encoding="utf-8")
        with self.assertRaisesRegex(WEATHER.WeatherCacheError, "invalid or unreadable"):
            self.cache.resolve(self.location, 900, self.success)


class WeatherCacheConcurrencyTests(unittest.TestCase):
    def run_race(self, fail: bool) -> list[Any]:
        context = multiprocessing.get_context("fork")
        with tempfile.TemporaryDirectory() as temporary:
            cache_path = str(Path(temporary) / "weather-v1.json")
            counter_path = str(Path(temporary) / "counter.txt")
            start_event = context.Event()
            result_queue = context.Queue()
            processes = [
                context.Process(
                    target=weather_cache_worker,
                    args=(cache_path, counter_path, start_event, result_queue, fail),
                )
                for _ in range(3)
            ]
            for process in processes:
                process.start()
            start_event.set()
            for process in processes:
                process.join(6)
                if process.is_alive():
                    process.terminate()
                    process.join(2)
                    self.fail("weather cache race child did not exit")
                self.assertEqual(0, process.exitcode)
            results = [result_queue.get(timeout=2) for _ in processes]
            lines = Path(counter_path).read_text(encoding="utf-8").splitlines()
            self.assertEqual(["fetch"], lines)
            self.assertEqual(1, sum(1 for _, attempted, _ in results if attempted))
            return results

    def test_concurrent_success_reserves_exactly_one_request(self):
        results = self.run_race(False)
        self.assertIn("fresh", {source for source, _, _ in results})

    def test_concurrent_failure_is_not_retried_in_bucket(self):
        results = self.run_race(True)
        self.assertTrue(all(source == "neutral" for source, _, _ in results))


class WeatherModelTests(unittest.TestCase):
    def test_numeric_interpolation_and_shortest_wind_angle(self):
        left = sample(
            timestamp=datetime(2026, 7, 12, 10, tzinfo=timezone.utc),
            weather_code=0,
            temperature_c=10.0,
            cloud_cover_percent=0.0,
            wind_direction_degrees=350.0,
        )
        right = sample(
            timestamp=datetime(2026, 7, 12, 11, tzinfo=timezone.utc),
            weather_code=3,
            temperature_c=30.0,
            cloud_cover_percent=100.0,
            wind_direction_degrees=10.0,
        )
        timeline = timeline_with((left, right))
        quarter = WEATHER.interpolate_weather_sample(
            timeline, datetime(2026, 7, 12, 10, 15, tzinfo=timezone.utc)
        )
        half = WEATHER.interpolate_weather_sample(
            timeline, datetime(2026, 7, 12, 10, 30, tzinfo=timezone.utc)
        )
        three_quarters = WEATHER.interpolate_weather_sample(
            timeline, datetime(2026, 7, 12, 10, 45, tzinfo=timezone.utc)
        )
        self.assertEqual(0, quarter.weather_code)
        self.assertEqual(3, half.weather_code)
        self.assertEqual(15.0, quarter.temperature_c)
        self.assertEqual(50.0, half.cloud_cover_percent)
        self.assertAlmostEqual(355.0, quarter.wind_direction_degrees)
        self.assertAlmostEqual(0.0, half.wind_direction_degrees)
        self.assertAlmostEqual(5.0, three_quarters.wind_direction_degrees)

    def test_clear_cloud_and_fog_mappings_are_ordered(self):
        clear = WEATHER.derive_weather_state(sample())
        clouds = [
            WEATHER.derive_weather_state(sample(cloud_cover_percent=value))
            for value in (0.0, 25.0, 50.0, 75.0, 100.0)
        ]
        self.assertEqual(WEATHER.neutral_weather_state(), clear)
        self.assertEqual(sorted(state.cloud_intensity for state in clouds), [
            state.cloud_intensity for state in clouds
        ])
        self.assertEqual(0.0, clouds[0].cloud_intensity)
        self.assertEqual(1.0, clouds[-1].cloud_intensity)
        fog_45 = WEATHER.derive_weather_state(sample(weather_code=45, visibility_m=50000.0))
        fog_48 = WEATHER.derive_weather_state(sample(weather_code=48, visibility_m=50000.0))
        visibility = WEATHER.derive_weather_state(sample(visibility_m=1000.0))
        self.assertGreaterEqual(fog_45.fog_intensity, 0.65)
        self.assertEqual(1.0, fog_48.fog_intensity)
        self.assertGreater(visibility.fog_intensity, 0.0)

    def test_precipitation_priority_and_hail_is_never_inferred(self):
        rain = WEATHER.derive_weather_state(sample(weather_code=61, rain_mm=1.0))
        snow = WEATHER.derive_weather_state(
            sample(weather_code=71, rain_mm=3.0, snowfall_cm=1.0)
        )
        hail = WEATHER.derive_weather_state(
            sample(weather_code=96, rain_mm=3.0, snowfall_cm=2.0)
        )
        intense_rain = WEATHER.derive_weather_state(
            sample(weather_code=82, rain_mm=100.0, precipitation_mm=100.0)
        )
        self.assertEqual("rain", rain.precipitation_type)
        self.assertEqual("snow", snow.precipitation_type)
        self.assertEqual("hail", hail.precipitation_type)
        self.assertEqual("rain", intense_rain.precipitation_type)
        self.assertAlmostEqual(
            0.55,
            WEATHER.derive_weather_state(sample(weather_code=96)).precipitation_intensity,
        )
        self.assertEqual(
            1.0,
            WEATHER.derive_weather_state(sample(weather_code=99)).precipitation_intensity,
        )
        for code in (*range(51, 68), *range(80, 83), 95):
            with self.subTest(rain_code=code):
                self.assertEqual(
                    "rain",
                    WEATHER.derive_weather_state(sample(weather_code=code)).precipitation_type,
                )
        for code in (*range(71, 78), *range(85, 87)):
            with self.subTest(snow_code=code):
                self.assertEqual(
                    "snow",
                    WEATHER.derive_weather_state(sample(weather_code=code)).precipitation_type,
                )

    def test_light_heavy_presets_are_strictly_ordered_and_complete(self):
        self.assertEqual(
            {
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
            },
            set(WEATHER.WEATHER_PRESETS),
        )
        for kind in ("rain", "snow", "hail"):
            light = WEATHER.derive_weather_state(
                WEATHER.preset_weather_sample(f"{kind}-light", FIXED_NOW)
            )
            heavy = WEATHER.derive_weather_state(
                WEATHER.preset_weather_sample(f"{kind}-heavy", FIXED_NOW)
            )
            with self.subTest(kind=kind):
                self.assertEqual(kind, light.precipitation_type)
                self.assertEqual(kind, heavy.precipitation_type)
                self.assertLess(light.precipitation_intensity, heavy.precipitation_intensity)
        with self.assertRaises(ValueError):
            WEATHER.preset_weather_sample("thunder-lizards", FIXED_NOW)

    def test_wind_reverses_motion_and_gusts_increase_intensity(self):
        east = WEATHER.derive_weather_state(
            sample(wind_speed_ms=12.0, wind_direction_degrees=90.0), view_azimuth=0.0
        )
        west = WEATHER.derive_weather_state(
            sample(wind_speed_ms=12.0, wind_direction_degrees=270.0), view_azimuth=0.0
        )
        calm_gust = WEATHER.derive_weather_state(
            sample(wind_speed_ms=2.0, wind_gusts_ms=5.0)
        )
        strong_gust = WEATHER.derive_weather_state(
            sample(wind_speed_ms=2.0, wind_gusts_ms=30.0)
        )
        self.assertAlmostEqual(-east.wind_screen_direction, west.wind_screen_direction)
        self.assertNotEqual(0.0, east.wind_screen_direction)
        self.assertLess(calm_gust.wind_intensity, strong_gust.wind_intensity)

    def test_heat_threshold_suppression_snow_cover_and_bounds(self):
        at_30 = WEATHER.derive_weather_state(
            sample(temperature_c=30.0, apparent_temperature_c=30.0)
        )
        at_36 = WEATHER.derive_weather_state(
            sample(temperature_c=36.0, apparent_temperature_c=36.0)
        )
        at_42 = WEATHER.derive_weather_state(
            sample(temperature_c=42.0, apparent_temperature_c=42.0)
        )
        self.assertEqual(0.0, at_30.heat_intensity)
        self.assertAlmostEqual(0.5, at_36.heat_intensity)
        self.assertEqual(1.0, at_42.heat_intensity)
        for suppressor in (
            {"temperature_c": 42.0, "apparent_temperature_c": 42.0, "cloud_cover_percent": 100.0},
            {"temperature_c": 42.0, "apparent_temperature_c": 42.0, "weather_code": 48},
            {"temperature_c": 42.0, "apparent_temperature_c": 42.0, "weather_code": 82},
        ):
            with self.subTest(suppressor=suppressor):
                self.assertEqual(0.0, WEATHER.derive_weather_state(sample(**suppressor)).heat_intensity)
        self.assertEqual(0.5, WEATHER.derive_weather_state(sample(snow_depth_m=0.04)).snow_cover)
        self.assertEqual(1.0, WEATHER.derive_weather_state(sample(snow_depth_m=0.08)).snow_cover)
        extreme = WEATHER.derive_weather_state(
            sample(
                weather_code=9999,
                temperature_c=70.0,
                apparent_temperature_c=80.0,
                cloud_cover_percent=100.0,
                visibility_m=0.0,
                precipitation_mm=1000.0,
                rain_mm=1000.0,
                snowfall_cm=500.0,
                snow_depth_m=100.0,
                wind_speed_ms=150.0,
                wind_direction_degrees=360.0,
                wind_gusts_ms=200.0,
            )
        )
        for name in (
            "cloud_intensity",
            "fog_intensity",
            "precipitation_intensity",
            "wind_intensity",
            "heat_intensity",
            "snow_cover",
        ):
            self.assertTrue(0.0 <= getattr(extreme, name) <= 1.0, name)
        self.assertTrue(-1.0 <= extreme.wind_screen_direction <= 1.0)

    def test_precipitation_type_crossfade_is_continuous(self):
        left = WEATHER.preset_weather_sample("rain-heavy", FIXED_NOW)
        left = replace(left, timestamp=datetime(2026, 7, 12, 10, tzinfo=timezone.utc))
        right = WEATHER.preset_weather_sample("snow-heavy", FIXED_NOW)
        right = replace(right, timestamp=datetime(2026, 7, 12, 11, tzinfo=timezone.utc))
        timeline = timeline_with((left, right))
        before = WEATHER.weather_state_at(
            timeline, datetime(2026, 7, 12, 10, 29, 59, 999999, tzinfo=timezone.utc)
        )[1]
        after = WEATHER.weather_state_at(
            timeline, datetime(2026, 7, 12, 10, 30, 0, 1, tzinfo=timezone.utc)
        )[1]
        middle = WEATHER.weather_state_at(
            timeline, datetime(2026, 7, 12, 10, 30, tzinfo=timezone.utc)
        )[1]
        quarter = WEATHER.weather_state_at(
            timeline, datetime(2026, 7, 12, 10, 15, tzinfo=timezone.utc)
        )[1]
        three_quarters = WEATHER.weather_state_at(
            timeline, datetime(2026, 7, 12, 10, 45, tzinfo=timezone.utc)
        )[1]
        self.assertLess(
            abs(before.precipitation_intensity - after.precipitation_intensity),
            0.00001,
        )
        self.assertEqual("none", middle.precipitation_type)
        self.assertEqual(0.0, middle.precipitation_intensity)
        self.assertEqual("rain", quarter.precipitation_type)
        self.assertEqual("snow", three_quarters.precipitation_type)
        self.assertGreater(quarter.precipitation_intensity, middle.precipitation_intensity)
        self.assertGreater(three_quarters.precipitation_intensity, middle.precipitation_intensity)


class WeatherGifTests(unittest.TestCase):
    def require_api(self, name: str) -> Any:
        function = getattr(WEATHER, name, None)
        self.assertTrue(callable(function), f"weather API missing: {name}")
        return function

    def encode_small_gif(self) -> bytes:
        function = self.require_api("encode_palette_gif")
        frames = (bytes((0, 1, 1, 0)), bytes((1, 0, 0, 1)))
        result = _call_with_aliases(
            function,
            {
                "width": 2,
                "height": 2,
                "frames": frames,
                "palette": ((0, 0, 0), (255, 255, 255)),
                "delays": (5, 7),
                "delay": 5,
                "loop_count": 0,
                "transparent_index": 0,
            },
        )
        return _gif_bytes(result)

    def render_weather(self, kind: str, state: Any, depth: str = "front") -> bytes:
        function = self.require_api("render_weather_gif")
        if kind == "cloud":
            layer_name = "weather-cloud"
        elif kind == "fog":
            layer_name = f"weather-fog-{depth}"
        else:
            layer_name = f"weather-precip-{depth}"
        return _gif_bytes(
            _call_with_aliases(
                function,
                {
                    "kind": layer_name,
                    "state": state,
                    "depth": depth,
                    "width": WEATHER.PIXEL_WIDTH,
                    "height": WEATHER.PIXEL_HEIGHT,
                    "frame_count": WEATHER.GIF_FRAME_COUNT,
                    "seed": 0,
                },
            )
        )

    def decode(self, payload: bytes) -> Any:
        decoder = self.require_api("decode_palette_gif")
        return _call_with_aliases(
            decoder, {"source": payload, "payload": payload, "data": payload}
        )

    def validate(
        self,
        payload: bytes,
        width: int = WEATHER.PIXEL_WIDTH,
        height: int = WEATHER.PIXEL_HEIGHT,
    ) -> None:
        validator = self.require_api("validate_gif")
        _call_with_aliases(
            validator,
            {
                "source": payload,
                "payload": payload,
                "data": payload,
                "width": width,
                "height": height,
            },
        )

    def test_palette_encoder_decoder_and_validator_contract(self):
        payload = self.encode_small_gif()
        metadata = gif_metadata(payload)
        self.assertEqual((2, 2), (metadata["width"], metadata["height"]))
        self.assertEqual(2, metadata["frames"])
        self.assertEqual((5, 5), metadata["delays"])
        self.assertEqual(2, metadata["transparent_frames"])
        self.assertEqual(0, metadata["loop_count"])
        self.validate(payload, width=2, height=2)
        decoded = self.decode(payload)
        self.assertEqual(2, len(_decoded_frames(decoded)))
        for malformed in (payload[:-1], payload + b"\x00"):
            with self.subTest(length=len(malformed)), self.assertRaises(ValueError):
                self.decode(malformed)
        oversized = bytearray(b"GIF89a")
        oversized.extend(struct.pack("<HHBBB", 65535, 65535, 0, 0, 0))
        with self.assertRaises(ValueError):
            self.decode(bytes(oversized))

    def test_all_weather_gifs_are_deterministic_bounded_transparent_and_distinct(self):
        payloads: dict[str, bytes] = {}
        for preset, kind in (
            ("cloudy", "cloud"),
            ("fog", "fog"),
            ("rain-heavy", "rain"),
            ("snow-heavy", "snow"),
            ("hail-heavy", "hail"),
            ("wind", "debris"),
        ):
            state = WEATHER.derive_weather_state(
                WEATHER.preset_weather_sample(preset, FIXED_NOW)
            )
            first = self.render_weather(kind, state)
            second = self.render_weather(kind, state)
            with self.subTest(preset=preset):
                self.assertEqual(first, second)
                self.validate(first)
                metadata = gif_metadata(first)
                self.assertEqual(
                    (WEATHER.PIXEL_WIDTH, WEATHER.PIXEL_HEIGHT),
                    (metadata["width"], metadata["height"]),
                )
                self.assertTrue(1 <= metadata["frames"] <= WEATHER.MAX_GIF_FRAMES)
                self.assertTrue(all(delay > 0 for delay in metadata["delays"]))
                self.assertEqual(metadata["frames"], metadata["transparent_frames"])
                self.assertEqual(0, metadata["loop_count"])
                self.assertEqual(metadata["frames"], len(_decoded_frames(self.decode(first))))
            payloads[preset] = first
        self.assertEqual(len(payloads), len(set(payloads.values())))

    def test_light_heavy_density_and_precipitation_geometry_are_distinct(self):
        counts: dict[str, int] = {}
        representative: dict[str, bytes] = {}
        for kind in ("rain", "snow", "hail"):
            for weight in ("light", "heavy"):
                preset = f"{kind}-{weight}"
                state = WEATHER.derive_weather_state(
                    WEATHER.preset_weather_sample(preset, FIXED_NOW)
                )
                payload = self.render_weather(kind, state)
                frames = _decoded_frames(self.decode(payload))
                pixels = _frame_pixels(frames[0])
                counts[preset] = sum(1 for value in pixels if value != 0)
                representative[preset] = pixels
            self.assertLess(counts[f"{kind}-light"], counts[f"{kind}-heavy"])
        self.assertNotEqual(representative["rain-heavy"], representative["snow-heavy"])
        self.assertNotEqual(representative["snow-heavy"], representative["hail-heavy"])

        rain = representative["rain-heavy"]
        longest_rain_run = 0
        for x in range(WEATHER.PIXEL_WIDTH):
            run = 0
            for y in range(WEATHER.PIXEL_HEIGHT):
                if rain[y * WEATHER.PIXEL_WIDTH + x] == 1:
                    run += 1
                    longest_rain_run = max(longest_rain_run, run)
                else:
                    run = 0
        self.assertGreaterEqual(longest_rain_run, 6)

        def dense_centers(frame: bytes, value: int) -> int:
            total = 0
            for y in range(1, WEATHER.PIXEL_HEIGHT - 1):
                for x in range(1, WEATHER.PIXEL_WIDTH - 1):
                    indexes = (
                        (y + dy) * WEATHER.PIXEL_WIDTH + x + dx
                        for dy in (-1, 0, 1)
                        for dx in (-1, 0, 1)
                    )
                    if all(frame[index] == value for index in indexes):
                        total += 1
            return total

        self.assertEqual(0, dense_centers(representative["snow-heavy"], 2))
        self.assertGreater(dense_centers(representative["hail-heavy"], 3), 0)

    def test_particle_centroids_follow_opposite_wind_and_gust_displacement(self):
        def frame_centroid_x(frame: bytes) -> float:
            positions = [
                index % WEATHER.PIXEL_WIDTH
                for index, value in enumerate(frame)
                if value != 0
            ]
            self.assertTrue(positions)
            return sum(positions) / len(positions)

        def travel(state: Any) -> float:
            frames = tuple(
                _frame_pixels(frame)
                for frame in _decoded_frames(self.decode(self.render_weather("rain", state)))
            )
            return frame_centroid_x(frames[-1]) - frame_centroid_x(frames[0])

        base = WEATHER.preset_weather_sample("rain-heavy", FIXED_NOW)
        east_source = WEATHER.derive_weather_state(
            replace(base, wind_direction_degrees=90.0), view_azimuth=0.0
        )
        west_source = WEATHER.derive_weather_state(
            replace(base, wind_direction_degrees=270.0), view_azimuth=0.0
        )
        east_travel = travel(east_source)
        west_travel = travel(west_source)
        self.assertLess(east_travel, 0.0)
        self.assertGreater(west_travel, 0.0)

        light_gust = WEATHER.derive_weather_state(
            replace(
                base,
                wind_speed_ms=2.0,
                wind_gusts_ms=5.0,
                wind_direction_degrees=90.0,
            ),
            view_azimuth=0.0,
        )
        heavy_gust = WEATHER.derive_weather_state(
            replace(
                base,
                wind_speed_ms=2.0,
                wind_gusts_ms=30.0,
                wind_direction_degrees=90.0,
            ),
            view_azimuth=0.0,
        )
        self.assertLess(abs(travel(light_gust)), abs(travel(heavy_gust)))

    def test_heat_gif_distorts_source_and_changes_across_frames(self):
        function = self.require_api("render_heat_gif")
        source_path = ROOT / "examples" / "pixel-city-dynamic" / "1.png"
        scene_path = ROOT / "examples" / "pixel-city-dynamic" / "dynamic_scene.py"
        scene_spec = importlib.util.spec_from_file_location("pixel_city_heat_scene", scene_path)
        if scene_spec is None or scene_spec.loader is None:
            self.fail(f"could not load {scene_path}")
        scene = importlib.util.module_from_spec(scene_spec)
        sys.modules[scene_spec.name] = scene
        scene_spec.loader.exec_module(scene)
        width, height, pixels = scene.decode_png_rgba(source_path)
        heat = WEATHER.derive_weather_state(
            WEATHER.preset_weather_sample("heat", FIXED_NOW)
        )
        payload = _gif_bytes(
            _call_with_aliases(
                function,
                {
                    "source_path": source_path,
                    "pixels": pixels,
                    "width": width,
                    "height": height,
                    "state": heat,
                    "heat_intensity": heat.heat_intensity,
                    "frame_count": WEATHER.GIF_FRAME_COUNT,
                    "seed": 0,
                },
            )
        )
        self.validate(payload)
        metadata = gif_metadata(payload)
        self.assertEqual((width, height), (metadata["width"], metadata["height"]))
        frames = tuple(_frame_pixels(frame) for frame in _decoded_frames(self.decode(payload)))
        self.assertGreater(len(set(frames)), 1)
        source_indexes = WEATHER._rgba_to_palette_indexes(pixels, width, height)
        self.assertTrue(any(frame != source_indexes for frame in frames))
        changed_rows = 0
        content_rows = 0
        for y in range(height):
            start = y * width
            end = start + width
            if any(source_indexes[start:end]):
                content_rows += 1
            if frames[0][start:end] != source_indexes[start:end]:
                changed_rows += 1
        self.assertGreater(changed_rows, max(1, content_rows // 10))
        self.assertEqual(source_path.read_bytes(), (ROOT / "examples" / "pixel-city-dynamic" / "1.png").read_bytes())

    def test_quantized_signatures_ignore_tiny_noise_but_track_visible_changes(self):
        function = self.require_api("quantized_weather_signature")
        base = WEATHER.derive_weather_state(
            WEATHER.preset_weather_sample("rain-light", FIXED_NOW)
        )
        tiny = replace(
            base,
            cloud_intensity=min(1.0, base.cloud_intensity + 0.00001),
            precipitation_intensity=min(1.0, base.precipitation_intensity + 0.00001),
        )
        heavy = WEATHER.derive_weather_state(
            WEATHER.preset_weather_sample("rain-heavy", FIXED_NOW)
        )
        base_signature = function(base)
        self.assertEqual(base_signature, function(base))
        self.assertEqual(base_signature, function(tiny))
        self.assertNotEqual(base_signature, function(heavy))
        hash(base_signature)

    def test_adjacent_density_bucket_preserves_existing_cloud_geometry(self):
        low = WEATHER.WeatherState("cloudy", 0.44, 0.0, "none", 0.0, 0.0, 0.0, 0.0, 0.0)
        high = replace(low, cloud_intensity=0.56)
        self.assertNotEqual(
            WEATHER.quantized_weather_signature(low),
            WEATHER.quantized_weather_signature(high),
        )
        low_frame = _frame_pixels(
            _decoded_frames(self.decode(self.render_weather("cloud", low)))[0]
        )
        high_frame = _frame_pixels(
            _decoded_frames(self.decode(self.render_weather("cloud", high)))[0]
        )
        low_pixels = {index for index, value in enumerate(low_frame) if value != 0}
        high_pixels = {index for index, value in enumerate(high_frame) if value != 0}
        self.assertTrue(low_pixels)
        self.assertLessEqual(len(low_pixels - high_pixels), len(low_pixels) // 20)


def astronomy_for_scene():
    return SCENE.neutral_astronomy(
        datetime(2026, 7, 12, tzinfo=timezone.utc).date(), "UTC"
    )


def managed_rows(example: Path):
    rows = []
    for name in SCENE.MANAGED_NAMES:
        if name.endswith(".png"):
            path = example / name
        elif name in SCENE.WEATHER_LAYER_NAMES:
            path = example / "generated" / f"{name}-a.gif"
        else:
            path = example / "generated" / f"{name}-a.png"
        rows.append({"id": SCENE.ASSUMED_IDS[name], "path": str(path)})
    rows.append({"id": 99, "path": "/tmp/unrelated.png"})
    return rows


class FakeIPC:
    def __init__(self, example: Path):
        self.rows = managed_rows(example)
        self.commands = []

    def list_layers(self):
        return [dict(row) for row in self.rows]

    def modify(self, command):
        self.commands.append(command)
        if command.property == "path":
            for row in self.rows:
                if row["id"] == command.layer_id:
                    row["path"] = command.value
                    break


class FakeMonotonic:
    def __init__(self):
        self.value = 0.0

    def monotonic(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


class WeatherSceneIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.example = Path(self.temporary.name) / "pixel-city-dynamic"
        import shutil

        shutil.copytree(
            ROOT / "examples" / "pixel-city-dynamic", self.example
        )
        self.astronomy = astronomy_for_scene()
        self.astronomy_scene = SCENE.compute_scene_state(FIXED_NOW, self.astronomy)

    def scene_state(self, preset: str):
        sample_value = SCENE.weather_module.preset_weather_sample(preset, FIXED_NOW)
        return SCENE.weather_module.derive_weather_state(sample_value)

    def test_neutral_is_exact_identity_and_weather_composition_is_depth_aware(self):
        neutral = SCENE.compose_weather_scene(
            self.astronomy_scene, SCENE.weather_module.neutral_weather_state()
        )
        self.assertIs(self.astronomy_scene, neutral)
        cloud_values = []
        for cover in (0.0, 25.0, 50.0, 75.0, 100.0):
            state = SCENE.weather_module.derive_weather_state(
                replace(
                    SCENE.weather_module.neutral_weather_sample(FIXED_NOW),
                    cloud_cover_percent=cover,
                )
            )
            composed = SCENE.compose_weather_scene(self.astronomy_scene, state)
            cloud_values.append(
                (composed.sun_opacity, composed.stars_opacity, composed.ambient_brightness)
            )
        for column in range(3):
            values = [row[column] for row in cloud_values]
            self.assertEqual(sorted(values, reverse=True), values)
        fogged = SCENE.compose_weather_scene(self.astronomy_scene, self.scene_state("fog"))
        far_loss = (
            self.astronomy_scene.layer_looks["1.png"].opacity
            - fogged.layer_looks["1.png"].opacity
        )
        near_loss = (
            self.astronomy_scene.layer_looks["6.png"].opacity
            - fogged.layer_looks["6.png"].opacity
        )
        self.assertGreater(far_loss, near_loss)

    def test_polar_states_remain_deterministic_under_weather(self):
        day = datetime(2026, 12, 21, tzinfo=timezone.utc).date()
        polar = replace(
            SCENE.neutral_astronomy(day, "UTC"),
            sun_status="polar_night",
            civil_twilight_begin=None,
            sunrise=None,
            solar_noon=None,
            sunset=None,
            civil_twilight_end=None,
        )
        first = SCENE.compose_weather_scene(
            SCENE.compute_scene_state(FIXED_NOW, polar), self.scene_state("snow-heavy")
        )
        second = SCENE.compose_weather_scene(
            SCENE.compute_scene_state(FIXED_NOW, polar), self.scene_state("snow-heavy")
        )
        self.assertEqual(first, second)
        self.assertFalse(first.sun_visible)
        midnight_sun = replace(polar, sun_status="midnight_sun")
        midnight_first = SCENE.compose_weather_scene(
            SCENE.compute_scene_state(FIXED_NOW, midnight_sun), self.scene_state("wind")
        )
        midnight_second = SCENE.compose_weather_scene(
            SCENE.compute_scene_state(FIXED_NOW, midnight_sun), self.scene_state("wind")
        )
        self.assertEqual(midnight_first, midnight_second)
        self.assertTrue(midnight_first.sun_visible)

    def test_clouds_weaken_generated_shadow_and_wind_moves_cloud_and_fog(self):
        clear_shadow = SCENE.decode_png_rgba(
            SCENE.render_shadow_png(self.example / "6.png", self.astronomy_scene)
        )[2]
        cloudy_state = self.scene_state("cloudy")
        cloudy_scene = SCENE.compose_weather_scene(self.astronomy_scene, cloudy_state)
        cloudy_shadow = SCENE.decode_png_rgba(
            SCENE.render_shadow_png(self.example / "6.png", cloudy_scene)
        )[2]
        clear_alpha = sum(clear_shadow[3::4])
        cloudy_alpha = sum(cloudy_shadow[3::4])
        self.assertGreater(clear_alpha, cloudy_alpha)

        wind = self.scene_state("wind")
        cloudy_wind = replace(
            cloudy_state,
            wind_intensity=wind.wind_intensity,
            wind_screen_direction=1.0,
        )
        fog_wind = replace(
            self.scene_state("fog"),
            wind_intensity=wind.wind_intensity,
            wind_screen_direction=-1.0,
        )
        for layer, state in (
            ("weather-cloud", cloudy_wind),
            ("weather-fog-back", fog_wind),
            ("weather-fog-front", fog_wind),
        ):
            decoded = SCENE.weather_module.decode_palette_gif(
                SCENE.weather_module.render_weather_gif(layer, state)
            )
            with self.subTest(layer=layer):
                self.assertGreater(len(set(decoded.frames)), 1)

    def test_heat_paths_double_buffer_restore_and_do_not_churn(self):
        original_hashes = {
            index: (self.example / f"{index}.png").read_bytes()
            for index in range(1, 4)
        }
        ipc = FakeIPC(self.example)
        controller = SCENE.SceneController(ipc, self.example)
        heat = self.scene_state("heat")
        hot_scene = SCENE.compose_weather_scene(self.astronomy_scene, heat)
        controller.apply_once(hot_scene, heat)
        first_paths = {
            row["id"]: Path(row["path"])
            for row in ipc.rows
            if row["id"] in (1, 5, 6)
        }
        self.assertTrue(all(path.suffix == ".gif" for path in first_paths.values()))
        first_bytes = {path: path.read_bytes() for path in first_paths.values()}

        medium = replace(heat, heat_intensity=0.6)
        controller.apply_once(
            SCENE.compose_weather_scene(self.astronomy_scene, medium), medium
        )
        self.assertTrue(all(path.read_bytes() == data for path, data in first_bytes.items()))
        command_count = len(ipc.commands)
        controller.apply_once(
            SCENE.compose_weather_scene(self.astronomy_scene, medium), medium
        )
        self.assertEqual(command_count, len(ipc.commands))

        neutral = SCENE.weather_module.neutral_weather_state()
        controller.apply_once(self.astronomy_scene, neutral)
        for index, expected in original_hashes.items():
            self.assertEqual(expected, (self.example / f"{index}.png").read_bytes())
        base_rows = {row["id"]: Path(row["path"]) for row in ipc.rows}
        for index, layer_id in ((1, 1), (2, 5), (3, 6)):
            self.assertEqual(self.example / f"{index}.png", base_rows[layer_id])

    def test_heat_alternates_keep_canonical_layer_ownership(self):
        rows = managed_rows(self.example)
        for index in range(1, 4):
            name = f"{index}.png"
            row = next(item for item in rows if item["id"] == SCENE.ASSUMED_IDS[name])
            row["path"] = str(self.example / "generated" / f"heat-{index}-a.gif")
        managed = SCENE.discover_managed_layers(rows, self.example)
        self.assertEqual(set(SCENE.MANAGED_NAMES), set(managed.layers))
        self.assertNotIn(99, {item.layer_id for item in managed.layers.values()})

    def test_weather_overlay_double_buffer_preserves_active_and_clear_is_transparent(self):
        ipc = FakeIPC(self.example)
        controller = SCENE.SceneController(ipc, self.example)
        rain = self.scene_state("rain-heavy")
        controller.apply_once(
            SCENE.compose_weather_scene(self.astronomy_scene, rain), rain
        )
        layer_id = SCENE.ASSUMED_IDS["weather-precip-front"]
        first_path = Path(next(row["path"] for row in ipc.rows if row["id"] == layer_id))
        first_bytes = first_path.read_bytes()
        medium = replace(rain, precipitation_intensity=0.6)
        controller.apply_once(
            SCENE.compose_weather_scene(self.astronomy_scene, medium), medium
        )
        self.assertEqual(first_bytes, first_path.read_bytes())
        second_path = Path(next(row["path"] for row in ipc.rows if row["id"] == layer_id))
        second_bytes = second_path.read_bytes()
        self.assertNotEqual(first_path, second_path)

        neutral = SCENE.weather_module.neutral_weather_state()
        controller.apply_once(self.astronomy_scene, neutral)
        self.assertEqual(second_bytes, second_path.read_bytes())
        clear_path = Path(next(row["path"] for row in ipc.rows if row["id"] == layer_id))
        decoded = SCENE.weather_module.validate_gif(clear_path)
        transparent = decoded.transparent_index
        self.assertIsNotNone(transparent)
        self.assertTrue(
            all(all(value == transparent for value in frame) for frame in decoded.frames)
        )

    def test_demo_weather_visits_every_preset_without_client_factory(self):
        ipc = FakeIPC(self.example)
        clock = FakeMonotonic()
        output = __import__("io").StringIO()

        def forbidden_client():
            raise AssertionError("demo-weather attempted network client creation")

        with __import__("contextlib").redirect_stdout(output):
            result = SCENE.main(
                (
                    "--demo-weather",
                    "--demo-seconds", "11",
                    "--demo-step", "1",
                    "--at", "2026-07-12T12:00:00+00:00",
                    "--latitude", "47.4979",
                    "--longitude", "19.0402",
                    "--timezone", "UTC",
                    "--cache", str(Path(self.temporary.name) / "daily.json"),
                    "--example-dir", str(self.example),
                ),
                ipc_factory=lambda **kwargs: ipc,
                client_factory=forbidden_client,
                sleep=clock.sleep,
                monotonic=clock.monotonic,
            )
        payloads = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(0, result)
        self.assertEqual(set(SCENE.weather_module.WEATHER_PRESETS), {p["preset"] for p in payloads})
        self.assertTrue(all(item["network"] == "disabled" for item in payloads))

    def test_preset_bypasses_weather_network_and_status_reports_full_weather(self):
        astronomy_payload = {
            "date": "2026-07-12",
            "tzid": "Europe/Budapest",
            "sunrise": "2026-07-12T05:00:00+02:00",
            "sunset": "2026-07-12T20:40:00+02:00",
            "solar_noon": "2026-07-12T12:50:00+02:00",
            "civil_twilight_begin": "2026-07-12T04:25:00+02:00",
            "civil_twilight_end": "2026-07-12T21:15:00+02:00",
            "sun_status": "normal",
            "solar_position": {},
            "moonrise": None,
            "moonset": None,
            "moon_phase": "New Moon",
            "moon_illumination": 0.0,
        }
        client = StubClient(astronomy_payload)
        output = __import__("io").StringIO()
        with __import__("contextlib").redirect_stdout(output):
            result = SCENE.main(
                (
                    "--status",
                    "--weather-preset", "rain-heavy",
                    "--at", "2026-07-12T12:00:00+02:00",
                    "--latitude", "47.4979",
                    "--longitude", "19.0402",
                    "--timezone", "Europe/Budapest",
                    "--cache", str(Path(self.temporary.name) / "daily-status.json"),
                    "--weather-cache", str(Path(self.temporary.name) / "must-not-exist.json"),
                ),
                client_factory=lambda: client,
            )
        payload = json.loads(output.getvalue())
        self.assertEqual(0, result)
        self.assertEqual(1, len(client.calls))
        self.assertTrue(all("open-meteo" not in call for call in client.calls))
        self.assertEqual("preset:rain-heavy", payload["weather"]["source"])
        self.assertEqual("rain", payload["weather"]["state"]["precipitation_type"])
        self.assertIn("sample", payload["weather"])
        self.assertIn("age_seconds", payload["weather"])
        self.assertFalse((Path(self.temporary.name) / "must-not-exist.json").exists())

    def test_weather_provider_failure_keeps_astronomy_controller_running(self):
        astronomy_payload = {
            "date": "2026-07-12",
            "tzid": "Europe/Budapest",
            "sunrise": "2026-07-12T05:00:00+02:00",
            "sunset": "2026-07-12T20:40:00+02:00",
            "solar_noon": "2026-07-12T12:50:00+02:00",
            "civil_twilight_begin": "2026-07-12T04:25:00+02:00",
            "civil_twilight_end": "2026-07-12T21:15:00+02:00",
            "sun_status": "normal",
            "solar_position": {},
            "moonrise": None,
            "moonset": None,
            "moon_phase": "New Moon",
            "moon_illumination": 0.0,
        }
        client = StubClient(astronomy_payload, SCENE.ProviderError("DNS unavailable"))
        ipc = FakeIPC(self.example)
        output = __import__("io").StringIO()
        with __import__("contextlib").redirect_stdout(output):
            result = SCENE.main(
                (
                    "--once",
                    "--at", "2026-07-12T12:00:00+02:00",
                    "--latitude", "47.4979",
                    "--longitude", "19.0402",
                    "--timezone", "Europe/Budapest",
                    "--cache", str(Path(self.temporary.name) / "daily-failure.json"),
                    "--weather-cache", str(Path(self.temporary.name) / "weather-failure.json"),
                    "--example-dir", str(self.example),
                ),
                ipc_factory=lambda **kwargs: ipc,
                client_factory=lambda: client,
            )
        payload = json.loads(output.getvalue())
        self.assertEqual(0, result)
        self.assertEqual("neutral", payload["weather_source"])
        self.assertEqual("clear", payload["weather_condition"])
        self.assertTrue(any("DNS unavailable" in error for error in payload["errors"]))
        self.assertGreater(payload["commands_applied"], 0)

    def test_auto_status_uses_provider_current_while_demo_keeps_hourly_timeline(self):
        astronomy_payload = {
            "date": "2026-07-12",
            "tzid": "Europe/Budapest",
            "sunrise": "2026-07-12T05:00:00+02:00",
            "sunset": "2026-07-12T20:40:00+02:00",
            "solar_noon": "2026-07-12T12:50:00+02:00",
            "civil_twilight_begin": "2026-07-12T04:25:00+02:00",
            "civil_twilight_end": "2026-07-12T21:15:00+02:00",
            "sun_status": "normal",
            "solar_position": {},
            "moonrise": None,
            "moonset": None,
            "moon_phase": "New Moon",
            "moon_illumination": 0.0,
        }
        client = StubClient(astronomy_payload, fixture_payload())
        output = __import__("io").StringIO()
        with __import__("contextlib").redirect_stdout(output):
            result = SCENE.main(
                (
                    "--status",
                    "--at", "2026-07-12T10:30:00+02:00",
                    "--latitude", "47.4979",
                    "--longitude", "19.0402",
                    "--timezone", "Europe/Budapest",
                    "--cache", str(Path(self.temporary.name) / "daily-current.json"),
                    "--weather-cache", str(Path(self.temporary.name) / "weather-current.json"),
                ),
                client_factory=lambda: client,
            )
        payload = json.loads(output.getvalue())
        self.assertEqual(0, result)
        self.assertEqual("fresh", payload["weather"]["source"])
        self.assertEqual(1, payload["weather"]["sample"]["weather_code"])
        self.assertEqual(1, payload["weather"]["provider_current"]["weather_code"])
        self.assertEqual(3, WEATHER.interpolate_weather_sample(
            provider_timeline(),
            datetime(2026, 7, 12, 8, 30, tzinfo=timezone.utc),
        ).weather_code)


if __name__ == "__main__":
    unittest.main()
