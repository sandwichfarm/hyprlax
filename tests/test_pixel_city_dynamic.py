#!/usr/bin/env python3
"""Deterministic tests for the dynamic Pixel City daily-data foundation."""

from __future__ import annotations

from datetime import date, datetime, timezone
import importlib.util
import json
import multiprocessing
from pathlib import Path
import stat
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "examples" / "pixel-city-dynamic" / "dynamic_scene.py"
SPEC = importlib.util.spec_from_file_location("pixel_city_dynamic_scene", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"could not load {MODULE_PATH}")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)

FIXED_NOW = datetime(2026, 7, 12, 10, 0, tzinfo=timezone.utc)


def location_payload(
    latitude: float = 47.4979,
    longitude: float = 19.0402,
    timezone_name: str = "Europe/Budapest",
):
    return {
        "status": "success",
        "lat": latitude,
        "lon": longitude,
        "timezone": timezone_name,
        "city": "Budapest",
        "regionName": "Budapest",
        "country": "Hungary",
        "countryCode": "HU",
    }


def astronomy_payload(
    requested_day: date = date(2026, 7, 12),
    timezone_name: str = "Europe/Budapest",
):
    day = requested_day.isoformat()
    return {
        "date": day,
        "tzid": timezone_name,
        "sunrise": f"{day}T05:00:00+02:00",
        "sunset": f"{day}T20:40:00+02:00",
        "solar_noon": f"{day}T12:50:00+02:00",
        "civil_twilight_begin": f"{day}T04:25:00+02:00",
        "civil_twilight_end": f"{day}T21:15:00+02:00",
        "sun_status": "normal",
        "solar_position": {
            "sunrise_azimuth": 54.1,
            "sunset_azimuth": 305.9,
            "solar_noon_azimuth": 180.0,
            "solar_noon_altitude": 63.2,
        },
        "moonrise": f"{day}T23:10:00+02:00",
        "moonset": f"{day}T09:20:00+02:00",
        "moon_phase": "Waxing Gibbous",
        "moon_illumination": 73.5,
    }


class StubClient:
    def __init__(self, *responses):
        self.responses = list(responses)
        self.calls = []

    def get_json(self, url):
        self.calls.append(url)
        if not self.responses:
            raise AssertionError(f"unexpected network request: {url}")
        response = self.responses.pop(0)
        if isinstance(response, BaseException):
            raise response
        return response


class GuardOpener:
    def __call__(self, request, timeout):
        raise AssertionError(f"real network access forbidden: {request.full_url} timeout={timeout}")


class FakeResponse:
    def __init__(self, payload: bytes):
        self.payload = payload

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False

    def read(self, amount: int):
        return self.payload[:amount]


def race_worker(cache_path, counter_path, start_event, result_queue, fail):
    cache = SCENE.DailyCache(
        Path(cache_path), clock=lambda: datetime(2026, 7, 12, tzinfo=timezone.utc)
    )
    start_event.wait(5)

    def fetch():
        with Path(counter_path).open("a", encoding="utf-8") as handle:
            handle.write("fetch\n")
            handle.flush()
        time.sleep(0.15)
        if fail:
            raise SCENE.ProviderError("offline")
        return {"value": 1}

    result = cache.run_daily("race-provider", date(2026, 7, 12), fetch)
    result_queue.put((result.source, result.attempted, result.error))


class DailyCacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.cache_path = Path(self.temporary.name) / "daily.json"
        self.calls = []
        self.cache = SCENE.DailyCache(self.cache_path, clock=lambda: FIXED_NOW)

    def fetch(self, value=None):
        self.calls.append("fetch")
        return value if value is not None else {"value": 1}

    def test_same_day_success_is_not_fetched_again(self):
        first = self.cache.run_daily("provider", date(2026, 7, 12), self.fetch)
        second = self.cache.run_daily("provider", date(2026, 7, 12), self.fetch)
        self.assertEqual(["fetch"], self.calls)
        self.assertEqual("fresh", first.source)
        self.assertEqual("cache", second.source)
        self.assertFalse(second.attempted)

    def test_failed_attempt_is_not_retried_same_day(self):
        def fail():
            self.calls.append("fail")
            raise SCENE.ProviderError("offline")

        first = self.cache.run_daily("provider", date(2026, 7, 12), fail)
        second = self.cache.run_daily("provider", date(2026, 7, 12), fail)
        self.assertEqual(["fail"], self.calls)
        self.assertEqual("missing", first.source)
        self.assertEqual("missing", second.source)
        self.assertIn("offline", second.error)

    def test_stale_success_survives_next_day_failure(self):
        self.cache.run_daily("provider", date(2026, 7, 11), self.fetch)

        def fail():
            self.calls.append("fail")
            raise SCENE.ProviderError("offline")

        result = self.cache.run_daily("provider", date(2026, 7, 12), fail)
        self.assertEqual(["fetch", "fail"], self.calls)
        self.assertEqual("stale", result.source)
        self.assertTrue(result.stale)
        self.assertEqual({"value": 1}, result.data)

    def test_changed_location_identity_cannot_bypass_same_day_gate(self):
        self.cache.run_daily(
            "astronomy", date(2026, 7, 12), self.fetch, identity="location-a"
        )
        result = self.cache.run_daily(
            "astronomy", date(2026, 7, 12), self.fetch, identity="location-b"
        )
        self.assertEqual(["fetch"], self.calls)
        self.assertEqual("missing", result.source)
        self.assertIsNone(result.data)

    def test_canonical_and_input_dates_both_suppress_duplicate(self):
        canonical = lambda value: date(2026, 7, 13)
        self.cache.run_daily(
            "provider", date(2026, 7, 12), self.fetch, canonical_date=canonical
        )
        self.cache.run_daily("provider", date(2026, 7, 12), self.fetch)
        self.cache.run_daily("provider", date(2026, 7, 13), self.fetch)
        self.assertEqual(["fetch"], self.calls)

    def test_cache_is_private_valid_json(self):
        self.cache.run_daily("provider", date(2026, 7, 12), self.fetch)
        parsed = json.loads(self.cache_path.read_text(encoding="utf-8"))
        self.assertEqual(1, parsed["schema_version"])
        mode = stat.S_IMODE(self.cache_path.stat().st_mode)
        self.assertEqual(0o600, mode)


class DailyCacheConcurrencyTests(unittest.TestCase):
    def run_race(self, fail: bool):
        context = multiprocessing.get_context("fork")
        with tempfile.TemporaryDirectory() as temporary:
            cache_path = str(Path(temporary) / "daily.json")
            counter_path = str(Path(temporary) / "counter.txt")
            start_event = context.Event()
            result_queue = context.Queue()
            processes = [
                context.Process(
                    target=race_worker,
                    args=(cache_path, counter_path, start_event, result_queue, fail),
                )
                for _ in range(2)
            ]
            for process in processes:
                process.start()
            start_event.set()
            for process in processes:
                process.join(5)
                if process.is_alive():
                    process.terminate()
                    process.join(2)
                    self.fail("cache race child did not exit within five seconds")
                self.assertEqual(0, process.exitcode)
            results = [result_queue.get(timeout=2) for _ in processes]
            lines = Path(counter_path).read_text(encoding="utf-8").splitlines()
            parsed = json.loads(Path(cache_path).read_text(encoding="utf-8"))
            self.assertEqual(["fetch"], lines)
            self.assertEqual(1, parsed["schema_version"])
            return results

    def test_concurrent_success_performs_exactly_one_fetch(self):
        results = self.run_race(False)
        self.assertEqual(1, sum(1 for _, attempted, _ in results if attempted))

    def test_concurrent_failed_attempt_performs_exactly_one_fetch(self):
        results = self.run_race(True)
        self.assertEqual(1, sum(1 for _, attempted, _ in results if attempted))
        self.assertTrue(all(source == "missing" for source, _, _ in results))


class ProviderValidationTests(unittest.TestCase):
    def test_manual_location_rejects_invalid_ranges_and_timezone(self):
        for values in (
            (True, 19.0, "Europe/Budapest"),
            (91.0, 19.0, "Europe/Budapest"),
            (47.0, 181.0, "Europe/Budapest"),
            (47.0, 19.0, "Not/AZone"),
        ):
            with self.subTest(values=values), self.assertRaises(SCENE.ProviderError):
                SCENE.manual_location(*values)

    def test_ip_api_status_and_schema_validation(self):
        client = StubClient({"status": "fail", "message": "reserved range"})
        with self.assertRaises(SCENE.ProviderError):
            SCENE.LocationProvider(client).fetch()
        client = StubClient(location_payload())
        result = SCENE.LocationProvider(client).fetch()
        self.assertEqual("Europe/Budapest", result.timezone)
        self.assertEqual("HU", result.country_code)
        self.assertIn("fields=status,message,lat,lon,timezone", client.calls[0])

    def test_astronomy_wrong_date_timezone_phase_and_ranges_are_rejected(self):
        location = SCENE.manual_location(47.4979, 19.0402, "Europe/Budapest")
        mutations = (
            ("wrong date", {"date": "2026-07-13"}),
            ("wrong timezone", {"tzid": "UTC"}),
            ("bad phase", {"moon_phase": "Mostly cheese"}),
            ("bad illumination", {"moon_illumination": 101}),
            ("bad altitude", {"solar_position": {"solar_noon_altitude": 100}}),
        )
        for name, change in mutations:
            payload = astronomy_payload()
            payload.update(change)
            client = StubClient(payload)
            with self.subTest(name=name), self.assertRaises(SCENE.ProviderError):
                SCENE.AstronomyProvider(client).fetch(location, date(2026, 7, 12))

    def test_polar_and_null_events_are_accepted_without_manufacturing(self):
        payload = astronomy_payload()
        payload.update(
            {
                "sun_status": "polar_night",
                "sunrise": None,
                "sunset": None,
                "solar_noon": None,
                "civil_twilight_begin": None,
                "civil_twilight_end": None,
                "moonrise": None,
                "moonset": None,
                "solar_position": None,
            }
        )
        location = SCENE.manual_location(69.6492, 18.9553, "Europe/Budapest")
        result = SCENE.AstronomyProvider(StubClient(payload)).fetch(
            location, date(2026, 7, 12)
        )
        self.assertEqual("polar_night", result.sun_status)
        self.assertIsNone(result.sunrise)
        self.assertIsNone(result.moonrise)

    def test_oversized_response_is_rejected(self):
        payload = b"x" * (SCENE.MAX_RESPONSE_BYTES + 1)
        client = SCENE.HttpJsonClient(opener=lambda request, timeout: FakeResponse(payload))
        with self.assertRaisesRegex(SCENE.ProviderError, "exceeds"):
            client.get_json("https://example.invalid")

    def test_network_guard_fails_closed(self):
        client = SCENE.HttpJsonClient(opener=GuardOpener())
        with self.assertRaises(AssertionError):
            client.get_json("https://example.invalid")


class DailyFactsTests(unittest.TestCase):
    def make_cache(self, directory: str):
        return SCENE.DailyCache(Path(directory) / "daily.json", clock=lambda: FIXED_NOW)

    def test_manual_override_bypasses_ip_api(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = StubClient(astronomy_payload())
            facts = SCENE.resolve_daily_facts(
                self.make_cache(temporary),
                client,
                now=FIXED_NOW,
                latitude=47.4979,
                longitude=19.0402,
                timezone_name="Europe/Budapest",
            )
            self.assertEqual("manual", facts.location_source)
            self.assertEqual(1, len(client.calls))
            self.assertTrue(client.calls[0].startswith(SCENE.ASTRONOMY_API_URL))

    def test_same_day_restart_uses_location_and_astronomy_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache = self.make_cache(temporary)
            first_client = StubClient(location_payload(), astronomy_payload())
            first = SCENE.resolve_daily_facts(cache, first_client, now=FIXED_NOW)
            guard = StubClient()
            second = SCENE.resolve_daily_facts(cache, guard, now=FIXED_NOW)
            self.assertEqual(2, len(first_client.calls))
            self.assertEqual([], guard.calls)
            self.assertEqual("cache", second.location_source)
            self.assertEqual("cache", second.astronomy_source)
            self.assertFalse(first.stale)

    def test_location_failure_skips_astronomy_and_uses_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = StubClient(SCENE.ProviderError("offline"))
            facts = SCENE.resolve_daily_facts(
                self.make_cache(temporary), client, now=FIXED_NOW
            )
            self.assertEqual(1, len(client.calls))
            self.assertEqual("fallback", facts.location_source)
            self.assertEqual("fallback", facts.astronomy_source)
            self.assertIn("skipped because no valid location", " ".join(facts.errors))

    def test_changed_manual_location_same_day_uses_neutral_not_wrong_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache = self.make_cache(temporary)
            first = SCENE.resolve_daily_facts(
                cache,
                StubClient(astronomy_payload()),
                now=FIXED_NOW,
                latitude=47.4979,
                longitude=19.0402,
                timezone_name="Europe/Budapest",
            )
            guard = StubClient()
            second = SCENE.resolve_daily_facts(
                cache,
                guard,
                now=FIXED_NOW,
                latitude=48.2082,
                longitude=16.3738,
                timezone_name="Europe/Budapest",
            )
            self.assertEqual("fresh", first.astronomy_source)
            self.assertEqual([], guard.calls)
            self.assertEqual("fallback", second.astronomy_source)
            self.assertEqual("New Moon", second.astronomy.moon_phase)

    def test_neutral_fallback_exact_constants(self):
        result = SCENE.neutral_astronomy(date(2026, 7, 12), "Europe/Budapest")
        self.assertEqual((5, 30), (result.civil_twilight_begin.hour, result.civil_twilight_begin.minute))
        self.assertEqual((6, 0), (result.sunrise.hour, result.sunrise.minute))
        self.assertEqual((12, 0), (result.solar_noon.hour, result.solar_noon.minute))
        self.assertEqual((18, 0), (result.sunset.hour, result.sunset.minute))
        self.assertEqual((18, 30), (result.civil_twilight_end.hour, result.civil_twilight_end.minute))
        self.assertEqual("New Moon", result.moon_phase)
        self.assertEqual(0.0, result.moon_illumination)


class CopiedExampleTests(unittest.TestCase):
    def test_copied_config_parses_with_six_existing_layers(self):
        try:
            import tomllib
        except ImportError as error:  # pragma: no cover - Python 3.9/3.10 operator message
            self.skipTest(f"tomllib requires Python 3.11+: {error}")
        config_path = ROOT / "examples" / "pixel-city-dynamic" / "parallax.toml"
        with config_path.open("rb") as handle:
            parsed = tomllib.load(handle)
        layers = parsed["global"]["layers"]
        self.assertEqual(6, len(layers))
        self.assertTrue(all((config_path.parent / layer["path"]).is_file() for layer in layers))
        self.assertNotIn("0.0VV", config_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()

