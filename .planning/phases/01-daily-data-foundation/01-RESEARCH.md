# Phase 1 Research: Daily Data Foundation

**Researched:** 2026-07-12
**Confidence:** High

## Concrete Module Shape

Implement `examples/pixel-city-dynamic/dynamic_scene.py` as an importable standard-library module
with these Phase 1 boundaries:

- `DailyCache`: lock, load, schema migration/rejection, attempt reservation, success commit.
- `HttpJsonClient`: URL encoding, identified User-Agent, timeout, 256 KiB bounded read, JSON decode.
- `LocationProvider`: manual override or validated ip-api response.
- `AstronomyProvider`: validated Sunrise-Sunset.org v2 daily response.
- `DailyFacts`: normalized location/date/solar/lunar record plus source/staleness metadata.
- `resolve_daily_facts(...)`: injected clock/fetch/cache orchestration used by later CLI phases.

Keep the functions small and pure after network/cache boundaries. Use `dataclasses`, `datetime`,
`zoneinfo`, `urllib`, `json`, `math.isfinite`, `pathlib`, `tempfile`, `fcntl`, and `os.replace`.

## Cache Schema v1

```json
{
  "schema_version": 1,
  "providers": {
    "ip-api": {
      "last_attempt_date": "2026-07-12",
      "last_attempt_at": "2026-07-12T08:00:00+00:00",
      "last_error": null,
      "last_success": {
        "fetched_at": "2026-07-12T08:00:01+00:00",
        "data": {"lat": 47.4979, "lon": 19.0402, "timezone": "Europe/Budapest", "city": "Budapest"}
      }
    },
    "sunrise-sunset-v2": {
      "last_attempt_date": "2026-07-12",
      "last_attempt_at": "2026-07-12T08:00:02+00:00",
      "last_error": null,
      "location_key": "47.4979,19.0402,Europe/Budapest",
      "last_success": {"fetched_at": "...", "date": "2026-07-12", "data": {}}
    }
  }
}
```

The date decision is evaluated in the resolved IANA timezone. Location attempts initially use the
system timezone date because the remote timezone is not known yet; after a last-good location
exists, use its timezone. Manual overrides use their supplied timezone and are never written as an
ip-api attempt.

## Attempt Reservation Algorithm

1. Open/create `daily-cache.lock` and take `fcntl.flock(LOCK_EX)`.
2. Load/validate cache; quarantine malformed cache by treating it as empty without executing it.
3. Calculate provider-local date and the data identity/location key.
4. If `last_attempt_date` matches, do not fetch regardless of location-key changes. Reuse
   `last_success` only when its location key matches; otherwise return neutral fallback.
5. Write `last_attempt_date`, `last_attempt_at`, and cleared error atomically while the lock is held.
6. Keep the lock through the single request so a second process cannot observe-and-race. Daily
   requests are rare, so bounded 10-second lock contention is preferable to a stampede.
7. On success, validate then update `last_success`; on failure, record a bounded error string.
8. Atomically write, unlock, and return last-good/fallback. Never loop or retry inside this method.

For atomic write: create a temp file in the cache directory, `json.dump`, flush, `os.fsync`,
`os.replace`, then best-effort fsync the parent directory. Set file mode `0600`.

## Provider Validation

### ip-api

- Object only; `status` must equal `success`.
- `lat`/`lon` must be finite numeric non-booleans in [-90, 90]/[-180, 180].
- `timezone` must be nonempty, length-bounded, and constructible via `ZoneInfo`.
- locality fields are strings with bounded length and control characters removed.
- Failure message is diagnostic only and length-bounded.

### Sunrise-Sunset v2

- Object only and no top-level `error`.
- `date` must equal requested date; `tzid` must construct via `ZoneInfo`.
- ISO fields parse via `datetime.fromisoformat`, retain offsets, and may be null where documented.
- `sun_status` must be one of normal/midnight_sun/polar_night.
- `moon_phase` must be one of the documented eight names; illumination finite in [0,100].
- Solar-position numbers finite and bounded to physical azimuth/altitude ranges.
- Preserve provider data needed by Phase 2 but normalize aliases and nulls once.

## Neutral Fallback

With no usable cache, synthesize a record for the requested local date with sunrise 06:00, solar
noon 12:00, sunset 18:00, civil dawn/dusk at 05:30/18:30, no moonrise/set, `New Moon`, 0%
illumination, `source="fallback"`, and `stale=true`. It is explicitly non-astronomical and only
keeps the scene operational.

## Test Matrix

Use `tests/test_pixel_city_dynamic.py` with `unittest`, temporary XDG cache, injected clock, and
call-counting fetcher. No test contacts a provider.

| Case | Required assertion |
|------|--------------------|
| first success | one attempt, success cached, normalized record returned |
| same-day restart | zero new fetches, cached record returned |
| first failure then restart | one total attempt, bounded error cached, no second same-day call |
| stale success then next-day failure | stale success retained and marked stale |
| two processes | shared counter/fixture proves exactly one fetch |
| manual coordinates | zero ip-api calls and supplied timezone/date used |
| moved manual location | zero second same-day astronomy calls; wrong-place cache is not reused |
| malformed/oversized JSON | rejected, error bounded, fallback/stale used |
| invalid coordinates/timezone | rejected before astronomy call |
| polar/null response | normalized without manufacturing missing events |
| provider error object/HTTP exception | no retry loop; deterministic fallback |
| copied config | `tomllib` parses and six base PNG paths exist |

## Analog Files and Repo Constraints

- `examples/cherry-blossom/time_overlay.py`: useful script placement and path reload precedent, but
  not a provider/cache design.
- `src/ipc.c`: later phases must target live lowercase IPC parser, not the old uppercase test helper.
- `tests/test_*.sh` run only under `make test-scripts`; Python unittest must be wired into that
  target or a discovered wrapper so `make test` alone is not misreported as full proof.
- The copied source TOML's literal `0.0VV` must be corrected before parsing.

## Plan Recommendation

- Plan 01-01: copied/corrected config, Python cache/schema/client/provider implementation, basic fixtures.
- Plan 01-02: unit test matrix including process contention, failure ledger, manual bypass, polar/null,
  and integration with script-test tooling.

## RESEARCH COMPLETE
