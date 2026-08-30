#!/usr/bin/env python3
"""Validate the data-driven content catalogs (P3-08).

Runs the same checks the engine loaders and the future content_smoke_test
(P3-09) do, but as a standalone pass over assets/data/*.cfg — no build required,
so a duplicate id or a dangling weapon reference is caught in seconds instead of
after a full compile + headless run.

Usage:
    python3 tools/validate_catalogs.py [--data DIR] [-q]

Exit code 0 = all catalogs valid, 1 = at least one error, 2 = usage/IO problem.
Warnings never fail the run on their own.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Mirrors the compile-time caps in the engine headers. Keep in sync with:
#   include/game/flight/ship_catalog.hpp   (kMaxShipDefs, kMaxWeaponMounts,
#                                            kMaxTurretHardpoints)
#   include/game/flight/weapon_catalog.hpp (kMaxWeaponDefs, kDefault*WeaponId)
#   include/game/economy/economy.hpp       (kMaxCommodities, kMaxMarkets,
#                                            kMaxMissionTemplates, kNumFactions)
MAX_SHIP_DEFS = 16
MAX_WEAPON_MOUNTS = 2
MAX_TURRET_HARDPOINTS = 4
MAX_WEAPON_DEFS = 24
MAX_COMMODITIES = 16
MAX_MARKETS = 8
MAX_MISSION_TEMPLATES = 16
NUM_FACTIONS = 4
MAX_LOCATION_DEFS = 12
MAX_LOCATION_NPCS = 16
LOCATION_NPC_KINDS = {"trader", "mission_giver", "turn_in", "travel_pad"}
MAX_SUIT_DEFS = 16
DEFAULT_PLAYER_SUIT_ID = "suit.flight.standard"

DEFAULT_SHIP_WEAPON_ID = "weapon.fixed.repeater"
DEFAULT_TURRET_WEAPON_ID = "weapon.turret.repeater"
# Ids the engine spawns by name when a caller passes none.
REQUIRED_SHIP_IDS = ("ship.player.default", "ship.npc.skiff")
MISSION_NO_REQUIREMENT = 0xFFFFFFFF
MISSION_TYPES = {"delivery", "visit", "combat", "escort"}


class Entry:
    """One [[section]] block: its fields plus the line it started on."""

    __slots__ = ("kind", "line", "fields")

    def __init__(self, kind: str, line: int) -> None:
        self.kind = kind
        self.line = line
        self.fields: dict[str, str] = {}


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, where: str, msg: str) -> None:
        self.errors.append(f"{where}: {msg}")

    def warn(self, where: str, msg: str) -> None:
        self.warnings.append(f"{where}: {msg}")


def parse_cfg(path: Path, report: Report) -> list[Entry]:
    """Parse the `[[section]] then key=value` format used by every *.cfg."""
    entries: list[Entry] = []
    current: Entry | None = None
    if not path.is_file():
        report.error(path.name, "file not found")
        return entries
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line[0] in "#;":
            continue
        if line.startswith("[[") and line.endswith("]]"):
            current = Entry(line[2:-2].strip(), lineno)
            entries.append(current)
            continue
        if "=" not in line:
            report.warn(f"{path.name}:{lineno}", f"line without '=' ignored: {line!r}")
            continue
        key, _, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if current is None:
            report.warn(f"{path.name}:{lineno}", f"key {key!r} outside any [[section]]")
            continue
        if key in current.fields:
            report.warn(
                f"{path.name}:{lineno}", f"duplicate key {key!r} in this block (last wins)"
            )
        current.fields[key] = value
    return entries


def as_float(entry: Entry, key: str, where: str, report: Report):
    if key not in entry.fields:
        return None
    try:
        return float(entry.fields[key])
    except ValueError:
        report.error(where, f"{key}={entry.fields[key]!r} is not a number")
        return None


def as_int(entry: Entry, key: str, where: str, report: Report):
    if key not in entry.fields:
        return None
    try:
        return int(entry.fields[key], 0)
    except ValueError:
        report.error(where, f"{key}={entry.fields[key]!r} is not an integer")
        return None


def as_int_silent(entry: Entry, key: str):
    """int field or None — no reporting (used for chain walks)."""
    try:
        return int(entry.fields.get(key, ""), 0)
    except ValueError:
        return None


def check_unique_ids(entries, kind, where_prefix, report, *, key="id", cap=None):
    """Return {id: entry} keeping the first of any duplicate."""
    seen: dict[str, Entry] = {}
    for e in entries:
        if e.kind != kind:
            continue
        where = f"{where_prefix}:{e.line}"
        ident = e.fields.get(key, "").strip()
        if not ident:
            report.error(where, f"[[{kind}]] entry without a {key}")
            continue
        if ident in seen:
            report.error(where, f"duplicate {kind} {key} {ident!r} "
                                f"(first at line {seen[ident].line})")
            continue
        seen[ident] = e
    if cap is not None and len(seen) > cap:
        report.error(where_prefix, f"{len(seen)} {kind} entries exceeds cap {cap}")
    return seen


def validate_weapons(path: Path, report: Report) -> dict[str, Entry]:
    entries = parse_cfg(path, report)
    weapons = check_unique_ids(entries, "weapon", path.name, report, cap=MAX_WEAPON_DEFS)
    for wid, e in weapons.items():
        where = f"{path.name}:{e.line}"
        if not wid.startswith("weapon."):
            report.warn(where, f"id {wid!r} does not follow 'weapon.<mount>.<name>'")
        for k in ("damage", "cooldown", "energy_cost", "heat_max", "range"):
            as_float(e, k, where, report)
        if "hitscan" in e.fields and e.fields["hitscan"] not in (
            "true", "false", "1", "0", "yes", "no", "on", "off"
        ):
            report.error(where, f"hitscan={e.fields['hitscan']!r} is not a bool")
        if e.fields.get("mount") not in (None, "fixed", "turret"):
            report.warn(where, f"mount={e.fields['mount']!r} (expected fixed|turret)")
    for need in (DEFAULT_SHIP_WEAPON_ID, DEFAULT_TURRET_WEAPON_ID):
        if need not in weapons:
            report.error(path.name, f"missing built-in default weapon {need!r}")
    return weapons


def validate_ships(path: Path, weapons: dict[str, Entry], report: Report) -> dict[str, Entry]:
    entries = parse_cfg(path, report)
    ships = check_unique_ids(entries, "ship", path.name, report, cap=MAX_SHIP_DEFS)
    for sid, e in ships.items():
        where = f"{path.name}:{e.line}"
        if not sid.startswith("ship."):
            report.warn(where, f"id {sid!r} does not follow 'ship.<role>.<name>'")
        for k in ("mass_kg", "hull_radius", "render_scale", "main_thrust_n",
                  "maneuver_thrust_n", "retro_thrust_n", "max_torque_nm", "hull_hp",
                  "power_output", "power_capacity", "shield_capacity", "shield_regen",
                  "shield_power_draw", "cargo_volume", "cargo_mass"):
            as_float(e, k, where, report)
        if "inertia" in e.fields and len(e.fields["inertia"].split(",")) != 3:
            report.error(where, "inertia must be 'Ixx,Iyy,Izz' (3 values)")
        if "subsystem_hp" in e.fields and len(e.fields["subsystem_hp"].split(",")) != 4:
            report.error(where, "subsystem_hp must be 'ENG,SHD,WPN,SEN' (4 values)")

        mounts = as_int(e, "weapon_mounts", where, report)
        if mounts is not None and not 0 <= mounts <= MAX_WEAPON_MOUNTS:
            report.error(where, f"weapon_mounts={mounts} out of [0,{MAX_WEAPON_MOUNTS}]")
        turrets = as_int(e, "turret_hardpoints", where, report)
        if turrets is not None and not 0 <= turrets <= MAX_TURRET_HARDPOINTS:
            report.error(where, f"turret_hardpoints={turrets} out of [0,{MAX_TURRET_HARDPOINTS}]")

        for idx in range(MAX_WEAPON_MOUNTS + 2):  # look past the cap to warn
            key = f"weapon_id_{idx}"
            if key not in e.fields:
                continue
            if idx >= MAX_WEAPON_MOUNTS:
                report.error(where, f"{key}: mount index >= cap {MAX_WEAPON_MOUNTS}")
                continue
            if mounts is not None and idx >= mounts:
                report.warn(where, f"{key} set but weapon_mounts={mounts}")
            ref = e.fields[key]
            if ref not in weapons:
                report.error(where, f"{key} references unknown weapon {ref!r}")

    for need in REQUIRED_SHIP_IDS:
        if need not in ships:
            report.error(path.name, f"missing engine-required ship id {need!r}")
    return ships


def validate_commodities(path: Path, report: Report) -> dict[int, Entry]:
    entries = parse_cfg(path, report)
    by_id: dict[int, Entry] = {}
    for e in (x for x in entries if x.kind == "commodity"):
        where = f"{path.name}:{e.line}"
        cid = as_int(e, "id", where, report)
        if cid is None:
            report.error(where, "[[commodity]] without an id")
            continue
        if cid in by_id:
            report.error(where, f"duplicate commodity id {cid}")
            continue
        if not e.fields.get("name"):
            report.error(where, f"commodity {cid} has no name")
        by_id[cid] = e
    if by_id:
        expected = set(range(len(by_id)))
        if set(by_id) != expected:
            report.error(path.name,
                         f"commodity ids must be contiguous from 0 (got {sorted(by_id)})")
        if len(by_id) > MAX_COMMODITIES:
            report.error(path.name, f"{len(by_id)} commodities exceeds cap {MAX_COMMODITIES}")
    return by_id


def validate_markets(path: Path, commodities: dict[int, Entry], report: Report) -> dict[int, Entry]:
    entries = parse_cfg(path, report)
    by_id: dict[int, Entry] = {}
    ncomm = len(commodities)
    for e in (x for x in entries if x.kind == "market"):
        where = f"{path.name}:{e.line}"
        mid = as_int(e, "id", where, report)
        if mid is None:
            report.error(where, "[[market]] without an id")
            continue
        if mid in by_id:
            report.error(where, f"duplicate market id {mid}")
            continue
        by_id[mid] = e
        if "location_id" not in e.fields:
            report.warn(where, f"market {mid} has no location_id")
        for key in e.fields:
            for prefix in ("stock_", "price_mod_", "rate_"):
                if key.startswith(prefix):
                    try:
                        idx = int(key[len(prefix):])
                    except ValueError:
                        report.error(where, f"{key}: bad commodity index")
                        break
                    if ncomm and idx >= ncomm:
                        report.error(where, f"{key} indexes commodity {idx} "
                                            f"but only {ncomm} exist")
                    break
    if len(by_id) > MAX_MARKETS:
        report.error(path.name, f"{len(by_id)} markets exceeds cap {MAX_MARKETS}")
    return by_id


def validate_missions(path: Path, commodities, markets, report: Report) -> dict[int, Entry]:
    entries = parse_cfg(path, report)
    by_id: dict[int, Entry] = {}
    for e in (x for x in entries if x.kind == "template"):
        where = f"{path.name}:{e.line}"
        tid = as_int(e, "id", where, report)
        if tid is None:
            report.error(where, "[[template]] without an id")
            continue
        if tid in by_id:
            report.error(where, f"duplicate template id {tid}")
            continue
        by_id[tid] = e
    if len(by_id) > MAX_MISSION_TEMPLATES:
        report.error(path.name, f"{len(by_id)} templates exceeds cap {MAX_MISSION_TEMPLATES}")

    for tid, e in by_id.items():
        where = f"{path.name}:{e.line}"
        mtype = e.fields.get("type", "").lower()
        if mtype and mtype not in MISSION_TYPES:
            report.error(where, f"type={mtype!r} not in {sorted(MISSION_TYPES)}")
        cid = as_int(e, "commodity_id", where, report)
        if cid is not None and commodities and cid not in commodities:
            report.error(where, f"commodity_id {cid} does not exist")
        for key in ("from_market", "to_market"):
            mk = as_int(e, key, where, report)
            if mk is not None and markets and mk not in markets:
                report.error(where, f"{key} {mk} does not exist")
        req = as_int(e, "requires_completed_id", where, report)
        if req is not None and req != MISSION_NO_REQUIREMENT and req not in by_id:
            report.error(where, f"requires_completed_id {req} is not a template id")
        for key in ("faction_id", "target_faction_id"):
            fac = as_int(e, key, where, report)
            if fac is not None and not 0 <= fac < NUM_FACTIONS:
                report.error(where, f"{key}={fac} out of [0,{NUM_FACTIONS})")

    # Cycle detection on requires_completed_id chains (mirrors P3-09 engine check).
    def prereq(tid: int):
        v = as_int_silent(by_id[tid], "requires_completed_id")
        return v if (v is not None and v != MISSION_NO_REQUIREMENT and v in by_id) else None

    for tid in by_id:
        seen = {tid}
        cur = prereq(tid)
        while cur is not None:
            if cur in seen:
                report.error(f"{path.name}:{by_id[tid].line}",
                             f"requires_completed_id chain from template {tid} forms a cycle")
                break
            seen.add(cur)
            cur = prereq(cur)
    return by_id


def validate_locations(path: Path, markets, commodities, missions, report: Report) -> dict[str, Entry]:
    entries = parse_cfg(path, report)
    locs = check_unique_ids(entries, "location", path.name, report, cap=MAX_LOCATION_DEFS)
    for lid, e in locs.items():
        where = f"{path.name}:{e.line}"
        if not lid.startswith("loc."):
            report.warn(where, f"id {lid!r} does not follow 'loc.<name>'")
        as_int(e, "location_id", where, report)
        # Gather npc.N.* fields into per-index dicts.
        npcs: dict[int, dict[str, str]] = {}
        for key, val in e.fields.items():
            if not key.startswith("npc."):
                continue
            rest = key[4:]
            num, _, field = rest.partition(".")
            try:
                idx = int(num)
            except ValueError:
                report.error(where, f"bad npc key {key!r}")
                continue
            if idx >= MAX_LOCATION_NPCS:
                report.error(where, f"npc index {idx} >= cap {MAX_LOCATION_NPCS}")
                continue
            npcs.setdefault(idx, {})[field] = val
        for idx, fields in sorted(npcs.items()):
            nwhere = f"{where} npc.{idx}"
            kind = fields.get("kind", "")
            if kind not in LOCATION_NPC_KINDS:
                report.error(nwhere, f"kind={kind!r} not in {sorted(LOCATION_NPC_KINDS)}")
            if "offset" in fields and len(fields["offset"].split(",")) != 3:
                report.error(nwhere, "offset must be 'x,y,z' (3 values)")
            if kind == "trader":
                try:
                    mk = int(fields.get("market", ""))
                    if markets and mk not in markets:
                        report.error(nwhere, f"market {mk} does not exist")
                except ValueError:
                    report.error(nwhere, "trader needs a numeric market")
                try:
                    cm = int(fields.get("commodity", ""))
                    if commodities and cm not in commodities:
                        report.error(nwhere, f"commodity {cm} does not exist")
                except ValueError:
                    report.error(nwhere, "trader needs a numeric commodity")
            elif kind in ("mission_giver", "turn_in"):
                try:
                    tp = int(fields.get("template", ""))
                    if missions and tp not in missions:
                        report.error(nwhere, f"template {tp} does not exist")
                except ValueError:
                    report.error(nwhere, f"{kind} needs a numeric template")
            elif kind == "travel_pad":
                dest = fields.get("dest", "")
                if dest not in locs:
                    report.error(nwhere, f"dest {dest!r} is not a location id")
    return locs


def validate_suits(path: Path, report: Report) -> dict[str, Entry]:
    entries = parse_cfg(path, report)
    suits = check_unique_ids(entries, "suit", path.name, report, cap=MAX_SUIT_DEFS)
    for sid, e in suits.items():
        where = f"{path.name}:{e.line}"
        if not sid.startswith("suit."):
            report.warn(where, f"id {sid!r} does not follow 'suit.<class>.<name>'")
        dr = as_float(e, "damage_reduction", where, report)
        if dr is not None and not 0.0 <= dr <= 0.95:
            report.error(where, f"damage_reduction={dr} out of [0, 0.95]")
        for k in ("eva_capacity", "eva_drain_per_sec", "eva_recharge_per_sec"):
            v = as_float(e, k, where, report)
            if v is not None and v < 0.0:
                report.error(where, f"{k}={v} must be >= 0")
        msm = as_float(e, "move_speed_mult", where, report)
        if msm is not None and msm <= 0.0:
            report.error(where, f"move_speed_mult={msm} must be > 0")
    if DEFAULT_PLAYER_SUIT_ID not in suits:
        report.error(path.name, f"missing engine default suit {DEFAULT_PLAYER_SUIT_ID!r}")
    return suits


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, default=None,
                    help="path to the data dir (default: <repo>/assets/data)")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="only print the final verdict + errors")
    args = ap.parse_args()

    data_dir = args.data or (Path(__file__).resolve().parent.parent / "assets" / "data")
    if not data_dir.is_dir():
        print(f"error: data dir not found: {data_dir}", file=sys.stderr)
        return 2

    report = Report()
    weapons = validate_weapons(data_dir / "weapons.cfg", report)
    ships = validate_ships(data_dir / "ships.cfg", weapons, report)
    commodities = validate_commodities(data_dir / "commodities.cfg", report)
    markets = validate_markets(data_dir / "markets.cfg", commodities, report)
    missions = validate_missions(data_dir / "mission_templates.cfg",
                                 commodities, markets, report)
    locations = validate_locations(data_dir / "locations.cfg",
                                   markets, commodities, missions, report)
    suits = validate_suits(data_dir / "suits.cfg", report)

    if not args.quiet:
        print(f"catalogs in {data_dir}:")
        print(f"  weapons ...... {len(weapons):3d}")
        print(f"  ships ........ {len(ships):3d}")
        print(f"  commodities .. {len(commodities):3d}")
        print(f"  markets ...... {len(markets):3d}")
        print(f"  missions ..... {len(missions):3d}")
        print(f"  locations .... {len(locations):3d}")
        print(f"  suits ........ {len(suits):3d}")
        for w in report.warnings:
            print(f"WARN  {w}")

    for err in report.errors:
        print(f"ERROR {err}")

    if report.errors:
        print(f"FAILED — {len(report.errors)} error(s), {len(report.warnings)} warning(s)")
        return 1
    print(f"OK — 0 errors, {len(report.warnings)} warning(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
