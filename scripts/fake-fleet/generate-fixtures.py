#!/usr/bin/env python3
"""Deterministic fixture generator for Data Collector fake-fleet tests.

Generates `.app/` (logs) and `screenshot/` directory trees for pc01..pc06 under
a base directory, with known mtimes spanning multiple time windows, special
filenames (spaces, UTF-8/Unicode, hidden directories, symlinks), multi-MB files
for progress/rate testing, optional bulk files up to 50,000/PC for soak tests,
and optional chmod 000 unreadable files to trigger rsync exit 23.

Writes `fixtures_manifest.json` in the base directory recording exact expected
file lists, counts, and byte totals per PC for standard test windows.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import stat
import sys
import time
from pathlib import Path
from typing import Dict, List, Set, Tuple

PC_IDS = [f"pc{i:02d}" for i in range(1, 7)]
BATCH_SIZE = 500


def parse_pc_list(values: List[str]) -> Set[str]:
    result: Set[str] = set()
    for val in values:
        for item in val.split(","):
            item = item.strip()
            if item:
                if item not in PC_IDS:
                    raise argparse.ArgumentTypeError(
                        f"Unknown PC id '{item}'. Expected one of: {', '.join(PC_IDS)}"
                    )
                result.add(item)
    return result


def make_deterministic_bytes(seed_str: str, size: int) -> bytes:
    """Generate deterministic bytes of exact `size` quickly."""
    if size <= 0:
        return b""
    digest = hashlib.sha256(seed_str.encode("utf-8")).digest()
    # Build a 4 KiB deterministic pattern block, then repeat/slice.
    block = (digest * 128)[:4096]
    if size <= len(block):
        return block[:size]
    repeats, rem = divmod(size, len(block))
    return (block * repeats) + block[:rem]


def write_file_with_mtime(path: Path, data: bytes, mtime: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() or path.is_symlink():
        try:
            if not path.is_symlink():
                os.chmod(path, 0o644)
        except OSError:
            pass
        path.unlink()
    with open(path, "wb") as f:
        f.write(data)
    os.utime(path, (mtime, mtime))


def make_symlink_with_mtime(link_path: Path, target: str) -> None:
    link_path.parent.mkdir(parents=True, exist_ok=True)
    if link_path.exists() or link_path.is_symlink():
        link_path.unlink()
    os.symlink(target, link_path)


def unlock_tree_permissions(root: Path) -> None:
    """Restore owner rwX permissions so re-generation is idempotent."""
    if not root.exists():
        return
    for dirpath, dirnames, filenames in os.walk(root):
        try:
            os.chmod(dirpath, 0o755)
        except OSError:
            pass
        for fname in filenames:
            fpath = os.path.join(dirpath, fname)
            if not os.path.islink(fpath):
                try:
                    st = os.lstat(fpath)
                    if (st.st_mode & 0o600) != 0o600:
                        os.chmod(fpath, 0o644)
                except OSError:
                    pass


def epoch_to_local_iso(epoch: int) -> str:
    return datetime.datetime.fromtimestamp(epoch).astimezone().isoformat()


def filter_files(
    entries: List[Tuple[str, int, int]],
    start_epoch: int,
    end_epoch: int,
    is_logs: bool,
    include_modified_after_end: bool,
) -> Dict[str, object]:
    selected: List[str] = []
    total_bytes = 0
    for rel_path, size, mtime in entries:
        if is_logs and include_modified_after_end:
            match = mtime >= start_epoch
        else:
            match = start_epoch <= mtime <= end_epoch
        if match:
            selected.append(rel_path)
            total_bytes += size
    selected.sort()
    return {
        "files": selected,
        "count": len(selected),
        "bytes": total_bytes,
    }


def generate_pc(
    base_dir: Path,
    pc_id: str,
    pc_index: int,
    now: int,
    count_per_pc: int,
    add_unreadable: bool,
    large_log_mb: int,
    large_shot_mb: int,
) -> Tuple[List[Tuple[str, int, int]], List[Tuple[str, int, int]], List[str]]:
    """Generate fixtures for a single PC.

    Returns (log_entries, shot_entries, unreadable_rel_paths) where each entry
    is (rel_path, size_bytes, mtime_epoch). Symlinks are created on disk but
    excluded from regular file entries.
    """
    pc_dir = base_dir / pc_id
    logs_dir = pc_dir / ".app"
    shots_dir = pc_dir / "screenshot"
    logs_dir.mkdir(parents=True, exist_ok=True)
    shots_dir.mkdir(parents=True, exist_ok=True)

    # Known mtime buckets (all exact integer seconds)
    t_10d = now - 10 * 86400
    t_3d = now - 3 * 86400
    t_6h = now - 6 * 3600
    t_1h = now - 1 * 3600
    t_future = now + 30 * 60
    buckets = [t_10d, t_3d, t_6h, t_1h, t_future]

    log_entries: List[Tuple[str, int, int]] = []
    shot_entries: List[Tuple[str, int, int]] = []
    unreadable_paths: List[str] = []

    # Fixed special log files covering all 5 time buckets + edge-case names + multi-MB
    special_logs: List[Tuple[str, int, int]] = [
        ("old_10d.log", 512 + pc_index * 17, t_10d),
        ("week_3d.log", 768 + pc_index * 23, t_3d),
        ("recent_6h.log", 1024 + pc_index * 31, t_6h),
        ("recent_1h.log", 1536 + pc_index * 37, t_1h),
        ("future_30m.log", 2048 + pc_index * 41, t_future),
        ("sub dir/my log file.log", 1280 + pc_index * 19, t_6h),
        ("café_日本語.log", 640 + pc_index * 13, t_1h),
        (".hidden/trace.log", 896 + pc_index * 29, t_1h),
        ("large_app.bin", large_log_mb * 1024 * 1024, t_1h),
    ]

    # Fixed special screenshot files covering all 5 time buckets + edge-case names + multi-MB
    special_shots: List[Tuple[str, int, int]] = [
        ("old_10d.png", 1024 + pc_index * 11, t_10d),
        ("week_3d.png", 1536 + pc_index * 17, t_3d),
        ("recent_6h.png", 2048 + pc_index * 23, t_6h),
        ("recent_1h.png", 2560 + pc_index * 29, t_1h),
        ("future_30m.png", 3072 + pc_index * 31, t_future),
        ("2026-11/big shot 1.png", large_shot_mb * 1024 * 1024, t_6h),
        ("écran_テスト.png", 1792 + pc_index * 37, t_1h),
    ]

    for rel, size, mtime in special_logs:
        data = make_deterministic_bytes(f"{pc_id}:log:{rel}", size)
        write_file_with_mtime(logs_dir / rel, data, mtime)
        log_entries.append((rel, size, mtime))

    for rel, size, mtime in special_shots:
        data = make_deterministic_bytes(f"{pc_id}:shot:{rel}", size)
        write_file_with_mtime(shots_dir / rel, data, mtime)
        shot_entries.append((rel, size, mtime))

    # Symlinks (must be ignored by RsyncLister; not added to manifest regular files)
    make_symlink_with_mtime(logs_dir / "link_to_recent.log", "recent_1h.log")
    make_symlink_with_mtime(shots_dir / "link_to_recent.png", "recent_1h.png")

    # Optional unreadable file (chmod 000) to trigger rsync exit 23 partial transfer
    if add_unreadable:
        unreadable_rel = "unreadable_secret.log"
        unreadable_size = 4096
        unreadable_mtime = t_1h
        unreadable_path = logs_dir / unreadable_rel
        data = make_deterministic_bytes(f"{pc_id}:unreadable:{unreadable_rel}", unreadable_size)
        write_file_with_mtime(unreadable_path, data, unreadable_mtime)
        os.chmod(unreadable_path, 0o000)
        # Note: rsync --list-only still sees chmod 000 files via lstat(),
        # so it is present in the listing / planned set and fails on copy (exit 23).
        log_entries.append((unreadable_rel, unreadable_size, unreadable_mtime))
        unreadable_paths.append(f".app/{unreadable_rel}")

    # Bulk deterministic files to reach `--count-per-pc` total regular files per PC
    existing_count = len(log_entries) + len(shot_entries)
    remaining = max(0, count_per_pc - existing_count)
    # Split remaining roughly 50/50 between logs and screenshots
    bulk_logs = (remaining + 1) // 2
    bulk_shots = remaining // 2

    # Pre-build a small deterministic payload table for fast soak-test generation
    payload_cache = [
        make_deterministic_bytes(f"{pc_id}:bulk:{k}", 64 + (k * 13) % 192)
        for k in range(32)
    ]

    for i in range(bulk_logs):
        batch = f"batch_{i // BATCH_SIZE:03d}"
        rel = f"{batch}/log_{i:05d}.log"
        mtime = buckets[i % len(buckets)]
        data = payload_cache[i % len(payload_cache)]
        write_file_with_mtime(logs_dir / rel, data, mtime)
        log_entries.append((rel, len(data), mtime))

    for i in range(bulk_shots):
        batch = f"batch_{i // BATCH_SIZE:03d}"
        rel = f"{batch}/shot_{i:05d}.png"
        mtime = buckets[i % len(buckets)]
        data = payload_cache[(i + 7) % len(payload_cache)]
        write_file_with_mtime(shots_dir / rel, data, mtime)
        shot_entries.append((rel, len(data), mtime))

    return log_entries, shot_entries, unreadable_paths


def build_manifest(
    now: int,
    count_per_pc: int,
    unreadable_pcs: Set[str],
    per_pc_data: Dict[str, Tuple[List[Tuple[str, int, int]], List[Tuple[str, int, int]], List[str]]],
) -> Dict[str, object]:
    window_specs = {
        "last_24h": {
            "startEpoch": now - 24 * 3600,
            "endEpoch": now,
            "includeModifiedAfterEnd": True,
        },
        "last_7d": {
            "startEpoch": now - 7 * 86400,
            "endEpoch": now,
            "includeModifiedAfterEnd": True,
        },
        "custom_window_logs_after_end_true": {
            "startEpoch": now - 12 * 3600,
            "endEpoch": now,
            "includeModifiedAfterEnd": True,
        },
        "custom_window_logs_after_end_false": {
            "startEpoch": now - 12 * 3600,
            "endEpoch": now,
            "includeModifiedAfterEnd": False,
        },
    }

    windows_out: Dict[str, object] = {}
    for wname, spec in window_specs.items():
        start_ep = int(spec["startEpoch"])
        end_ep = int(spec["endEpoch"])
        after_end = bool(spec["includeModifiedAfterEnd"])
        pcs_window: Dict[str, object] = {}
        for pc_id in PC_IDS:
            logs, shots, unreadable = per_pc_data[pc_id]
            logs_res = filter_files(logs, start_ep, end_ep, True, after_end)
            shots_res = filter_files(shots, start_ep, end_ep, False, after_end)
            pcs_window[pc_id] = {
                "logs": logs_res,
                "screenshots": shots_res,
                "totalFiles": logs_res["count"] + shots_res["count"],
                "totalBytes": logs_res["bytes"] + shots_res["bytes"],
                "unreadableFiles": unreadable,
            }
        windows_out[wname] = {
            "startEpoch": start_ep,
            "endEpoch": end_ep,
            "startIso": epoch_to_local_iso(start_ep),
            "endIso": epoch_to_local_iso(end_ep),
            "includeModifiedAfterEnd": after_end,
            "pcs": pcs_window,
        }

    all_files_summary: Dict[str, object] = {}
    for pc_id in PC_IDS:
        logs, shots, unreadable = per_pc_data[pc_id]
        all_files_summary[pc_id] = {
            "logsCount": len(logs),
            "logsBytes": sum(sz for _, sz, _ in logs),
            "screenshotsCount": len(shots),
            "screenshotsBytes": sum(sz for _, sz, _ in shots),
            "totalFiles": len(logs) + len(shots),
            "totalBytes": sum(sz for _, sz, _ in logs) + sum(sz for _, sz, _ in shots),
            "unreadableFiles": unreadable,
        }

    return {
        "schemaVersion": 1,
        "referenceTimeEpoch": now,
        "referenceTimeIso": epoch_to_local_iso(now),
        "pcs": PC_IDS,
        "countPerPc": count_per_pc,
        "unreadablePcs": sorted(unreadable_pcs),
        "allFiles": all_files_summary,
        "windows": windows_out,
    }


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Generate deterministic fake-fleet fixtures for pc01..pc06."
    )
    parser.add_argument(
        "base_dir",
        nargs="?",
        default=None,
        help="Base directory where pc01..pc06 and fixtures_manifest.json will be written.",
    )
    parser.add_argument(
        "--base-dir",
        dest="base_dir_opt",
        default=None,
        help="Base directory (alternative to positional argument).",
    )
    parser.add_argument(
        "--count-per-pc",
        type=int,
        default=50,
        help="Total regular files per PC across .app/ and screenshot/ (default: 50, supports up to 50000+).",
    )
    parser.add_argument(
        "--add-unreadable-on",
        action="append",
        default=[],
        help="PC id (or comma-separated list) on which to create a chmod 000 file (e.g. pc04).",
    )
    parser.add_argument(
        "--now",
        type=int,
        default=None,
        help="Reference UNIX epoch seconds for deterministic mtimes (default: current time).",
    )
    parser.add_argument(
        "--large-log-mb",
        type=int,
        default=2,
        help="Size in MiB of the large log file per PC (default: 2).",
    )
    parser.add_argument(
        "--large-shot-mb",
        type=int,
        default=3,
        help="Size in MiB of the large screenshot file per PC (default: 3).",
    )

    args = parser.parse_args(argv)
    base_dir_str = args.base_dir_opt or args.base_dir
    if not base_dir_str:
        parser.error("base_dir is required (positional or --base-dir).")

    if args.count_per_pc < 1:
        parser.error("--count-per-pc must be >= 1")
    if args.large_log_mb < 0 or args.large_shot_mb < 0:
        parser.error("--large-log-mb and --large-shot-mb must be >= 0")

    try:
        unreadable_pcs = parse_pc_list(args.add_unreadable_on)
    except argparse.ArgumentTypeError as exc:
        parser.error(str(exc))

    now = args.now if args.now is not None else int(time.time())
    base_dir = Path(base_dir_str).resolve()
    base_dir.mkdir(parents=True, exist_ok=True)

    # Ensure previous unreadable files don't block idempotent regeneration
    for pc_id in PC_IDS:
        unlock_tree_permissions(base_dir / pc_id)

    per_pc_data: Dict[
        str, Tuple[List[Tuple[str, int, int]], List[Tuple[str, int, int]], List[str]]
    ] = {}
    for idx, pc_id in enumerate(PC_IDS, start=1):
        per_pc_data[pc_id] = generate_pc(
            base_dir=base_dir,
            pc_id=pc_id,
            pc_index=idx,
            now=now,
            count_per_pc=args.count_per_pc,
            add_unreadable=(pc_id in unreadable_pcs),
            large_log_mb=args.large_log_mb,
            large_shot_mb=args.large_shot_mb,
        )

    manifest = build_manifest(
        now=now,
        count_per_pc=args.count_per_pc,
        unreadable_pcs=unreadable_pcs,
        per_pc_data=per_pc_data,
    )

    manifest_path = base_dir / "fixtures_manifest.json"
    tmp_path = base_dir / ".fixtures_manifest.json.tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp_path, manifest_path)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
