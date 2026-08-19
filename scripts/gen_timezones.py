#!/usr/bin/env python3
"""Generate firmware timezone catalog from IANA zone.tab + host tzdata.

Output: lib/hal/TimeZoneData.cpp
"""

from __future__ import annotations

import argparse
import calendar
import sys
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

REGIONS = (
    "UTC",
    "Africa",
    "America",
    "Antarctica",
    "Arctic",
    "Asia",
    "Atlantic",
    "Australia",
    "Europe",
    "Indian",
    "Pacific",
)

ZONE_TAB_CANDIDATES = (
    Path("/usr/share/zoneinfo/zone1970.tab"),
    Path("/usr/share/zoneinfo/zone.tab"),
    Path("/var/db/timezone/zoneinfo/zone1970.tab"),
    Path("/var/db/timezone/zoneinfo/zone.tab"),
)

# Representative year for recurring DST rules shipped in firmware.
RULE_YEAR = 2026


def load_zone_ids(zone_tab: Path) -> list[str]:
    ids: list[str] = []
    seen: set[str] = set()
    for line in zone_tab.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        zone_id = parts[2].strip()
        if zone_id and zone_id not in seen:
            seen.add(zone_id)
            ids.append(zone_id)
    if "UTC" not in seen:
        ids.insert(0, "UTC")
    return ids


def posix_offset(utcoffset: timedelta) -> str:
    total = -int(utcoffset.total_seconds())
    sign = "-" if total < 0 else ""
    total = abs(total)
    hours, rem = divmod(total, 3600)
    minutes, seconds = divmod(rem, 60)
    if seconds:
        return f"{sign}{hours}:{minutes:02d}:{seconds:02d}"
    if minutes:
        return f"{sign}{hours}:{minutes:02d}"
    return f"{sign}{hours}"


def posix_abbrev(dt: datetime) -> str:
    name = dt.tzname() or ""
    if name.isascii() and name.isalpha() and 3 <= len(name) <= 6:
        return name
    off = dt.utcoffset() or timedelta(0)
    secs = int(off.total_seconds())
    sign = "+" if secs >= 0 else "-"
    secs = abs(secs)
    hours, rem = divmod(secs, 3600)
    minutes, seconds = divmod(rem, 60)
    if seconds:
        inner = f"{sign}{hours}:{minutes:02d}:{seconds:02d}"
    elif minutes:
        inner = f"{sign}{hours}:{minutes:02d}"
    else:
        inner = f"{sign}{hours}"
    return f"<{inner}>"


def refine_transition(tz: ZoneInfo, start: datetime, end: datetime) -> datetime:
    lo = start
    hi = end
    prev_off = lo.astimezone(tz).utcoffset()
    while (hi - lo) > timedelta(seconds=1):
        mid = lo + (hi - lo) / 2
        mid = mid.replace(microsecond=0)
        if mid <= lo:
            mid = lo + timedelta(seconds=1)
        if mid.astimezone(tz).utcoffset() == prev_off:
            lo = mid
        else:
            hi = mid
    return hi.replace(microsecond=0)


def find_transitions(tz: ZoneInfo, year: int) -> list[datetime]:
    t = datetime(year, 1, 1, tzinfo=timezone.utc)
    end = datetime(year + 1, 1, 1, tzinfo=timezone.utc)
    prev_off = t.astimezone(tz).utcoffset()
    found: list[datetime] = []
    step = timedelta(hours=1)
    while t < end:
        nxt = min(t + step, end)
        off = nxt.astimezone(tz).utcoffset()
        if off != prev_off:
            found.append(refine_transition(tz, t, nxt))
            prev_off = off
        t = nxt
    return found


def posix_datetime(tz: ZoneInfo, utc_transition: datetime) -> datetime:
    pre = (utc_transition - timedelta(seconds=1)).astimezone(tz).replace(tzinfo=None)
    return pre + timedelta(seconds=1)


def posix_rule_date(dt: datetime) -> str:
    month = dt.month
    dow = dt.isoweekday() % 7
    last_day = calendar.monthrange(dt.year, month)[1]
    week = 5 if dt.day + 7 > last_day else (dt.day - 1) // 7 + 1
    time_secs = dt.hour * 3600 + dt.minute * 60 + dt.second
    if time_secs == 2 * 3600:
        return f"M{month}.{week}.{dow}"
    if dt.minute == 0 and dt.second == 0:
        return f"M{month}.{week}.{dow}/{dt.hour}"
    if dt.second == 0:
        return f"M{month}.{week}.{dow}/{dt.hour}:{dt.minute:02d}"
    return f"M{month}.{week}.{dow}/{dt.hour}:{dt.minute:02d}:{dt.second:02d}"


def posix_tz(zone_id: str, year: int = RULE_YEAR) -> str:
    tz = ZoneInfo(zone_id)
    jan = datetime(year, 1, 1, 12, 0, tzinfo=tz)
    jul = datetime(year, 7, 1, 12, 0, tzinfo=tz)
    jan_off = jan.utcoffset() or timedelta(0)
    jul_off = jul.utcoffset() or timedelta(0)

    if jan_off == jul_off:
        return f"{posix_abbrev(jan)}{posix_offset(jan_off)}"

    transitions = find_transitions(tz, year)
    if len(transitions) < 2:
        # Unusual / cancelled DST: keep the January offset.
        return f"{posix_abbrev(jan)}{posix_offset(jan_off)}"

    # Standard time is the more-west / smaller IANA offset (winter).
    if jan_off <= jul_off:
        std_dt, dst_dt = jan, jul
        start_utc, end_utc = transitions[0], transitions[1]
    else:
        std_dt, dst_dt = jul, jan
        start_utc, end_utc = transitions[1], transitions[0]

    std_off = std_dt.utcoffset() or timedelta(0)
    dst_off = dst_dt.utcoffset() or timedelta(0)
    std = f"{posix_abbrev(std_dt)}{posix_offset(std_off)}"
    dst = posix_abbrev(dst_dt)
    expected_dst_off = std_off + timedelta(hours=1)
    if dst_off != expected_dst_off:
        dst += posix_offset(dst_off)

    start = posix_rule_date(posix_datetime(tz, start_utc))
    end = posix_rule_date(posix_datetime(tz, end_utc))
    return f"{std}{dst},{start},{end}"


def region_of(zone_id: str) -> str:
    if zone_id in ("UTC", "Etc/UTC", "Etc/GMT"):
        return "UTC"
    return zone_id.split("/", 1)[0]


def c_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def generate_cpp(entries: list[tuple[str, str, str]]) -> str:
    lines = [
        "// THIS FILE IS AUTOGENERATED by scripts/gen_timezones.py, DO NOT EDIT MANUALLY",
        "",
        '#include "TimeZoneCatalog.h"',
        "",
        "namespace TimeZoneCatalog {",
        "namespace {",
        "",
        "constexpr TimeZoneEntry kEntries[] = {",
    ]
    for zone_id, posix, region in entries:
        lines.append(
            f'    {{"{c_escape(zone_id)}", "{c_escape(posix)}", TimeZoneRegion::{region}}},'
        )
    lines.extend(
        [
            "};",
            "",
            "}  // namespace",
            "",
            "const TimeZoneEntry* data() { return kEntries; }",
            "",
            "uint16_t count() { return static_cast<uint16_t>(sizeof(kEntries) / sizeof(kEntries[0])); }",
            "",
            "}  // namespace TimeZoneCatalog",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--zone-tab",
        type=Path,
        default=None,
        help="Path to IANA zone.tab / zone1970.tab",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("lib/hal/TimeZoneData.cpp"),
    )
    args = parser.parse_args()

    zone_tab = args.zone_tab
    if zone_tab is None:
        zone_tab = next((path for path in ZONE_TAB_CANDIDATES if path.exists()), None)
    if zone_tab is None or not zone_tab.exists():
        print("Error: IANA zone.tab not found", file=sys.stderr)
        return 1

    zone_ids = load_zone_ids(zone_tab)
    entries: list[tuple[str, str, str]] = []
    skipped: list[str] = []
    region_counts: dict[str, int] = defaultdict(int)

    for zone_id in zone_ids:
        region = region_of(zone_id)
        if region not in REGIONS:
            skipped.append(zone_id)
            continue
        try:
            posix = posix_tz(zone_id)
        except (ZoneInfoNotFoundError, OSError, ValueError) as exc:  # pragma: no cover - host tzdata gaps
            print(f"Warning: skipping {zone_id}: {exc}", file=sys.stderr)
            skipped.append(zone_id)
            continue
        entries.append((zone_id, posix, region))
        region_counts[region] += 1

    # Keep UTC first, then stable IANA order within the rest.
    utc = [item for item in entries if item[0] == "UTC"]
    rest = [item for item in entries if item[0] != "UTC"]
    rest.sort(key=lambda item: item[0])
    entries = utc + rest

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate_cpp(entries), encoding="utf-8")

    print(f"Wrote {len(entries)} timezones to {args.output}")
    for region in REGIONS:
        print(f"  {region}: {region_counts[region]}")
    if skipped:
        print(f"Skipped {len(skipped)}: {', '.join(skipped)}")

    checks = {
        "UTC": "UTC0",
        "Asia/Shanghai": "CST-8",
        "America/Phoenix": "MST7",
        "Pacific/Honolulu": "HST10",
        "Europe/Moscow": "MSK-3",
        "Asia/Kolkata": "IST-5:30",
        "Europe/London": "GMT0BST,M3.5.0/1,M10.5.0/2",
        "Europe/Berlin": "CET-1CEST,M3.5.0/2,M10.5.0/3",
        "Europe/Helsinki": "EET-2EEST,M3.5.0/3,M10.5.0/4",
        "America/New_York": "EST5EDT,M3.2.0,M11.1.0",
        "Pacific/Auckland": "NZST-12NZDT,M9.5.0/2,M4.1.0/3",
    }
    by_id = {zone_id: posix for zone_id, posix, _ in entries}
    for zone_id, expected in checks.items():
        actual = by_id.get(zone_id)
        if actual != expected:
            print(f"Check {zone_id}: expected {expected}, got {actual}")
        else:
            print(f"Check {zone_id}: {actual}")
    for zone_id in (
        "Europe/London",
        "Europe/Berlin",
        "Europe/Helsinki",
        "America/New_York",
        "Pacific/Auckland",
    ):
        print(f"Sample {zone_id}: {by_id.get(zone_id)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
