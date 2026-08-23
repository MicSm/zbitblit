#!/usr/bin/env python3
"""Cross-platform zbb cases. Leaves TEST/test.tar and example.cmd untouched.

    python3 TEST/run-cases.py
    python3 TEST/run-cases.py --zbb /path/to/zbb
    python3 TEST/run-cases.py --jobs 2

Work products go to TEST/work/ (gitignored). Cases run in parallel on half the CPUs.
"""

from __future__ import annotations

import argparse
import filecmp
import os
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from functools import partial
from pathlib import Path
from typing import Callable, List, Optional, Sequence, Tuple

TEST_ROOT = Path(__file__).resolve().parent
WORK_ROOT = TEST_ROOT / "work"
BLOCK_UNIT = 100 * 1024
JobFn = Callable[[], Optional[str]]
_LOG_LOCK = threading.Lock()


def log(message: str) -> None:
    with _LOG_LOCK:
        print(message, flush=True)


def pattern_bytes(length: int, mul: int, add: int) -> bytes:
    return bytes(((i * mul + add) & 255) for i in range(length))


def fill_bytes(length: int, value: int) -> bytes:
    return bytes([value & 255]) * length


def cycle_bytes(length: int) -> bytes:
    return bytes(i & 255 for i in range(length))


def default_workers() -> int:
    # boffin: cap the pool at half the CPUs so the machine stays usable
    return max(1, (os.cpu_count() or 2) // 2)


def find_zbb(explicit: Optional[Path]) -> Path:
    if explicit is not None:
        path = explicit.expanduser().resolve()
        if not path.is_file():
            raise SystemExit("zbb not found: {0}".format(path))
        return path

    env = os.environ.get("ZBB")
    if env:
        path = Path(env).expanduser().resolve()
        if path.is_file():
            return path

    for name in ("zbb", "zbb.exe"):
        found = shutil.which(name)
        if found:
            return Path(found)

    repo = TEST_ROOT.parent
    for rel in (
        Path("out/build/x64-Release"),
        Path("out/build/x64-Release/release/x64"),
        Path("out/build/x64-Debug"),
        Path("out/build/x64-Debug/debug/x64"),
        Path("out/build"),
        Path("build"),
    ):
        for name in ("zbb", "zbb.exe"):
            path = repo / rel / name
            if path.is_file():
                return path

    build = repo / "out" / "build"
    if build.is_dir():
        matches = [p for p in build.rglob("zbb") if p.is_file()]
        matches += [p for p in build.rglob("zbb.exe") if p.is_file()]
        if matches:
            return max(matches, key=lambda p: p.stat().st_mtime)

    raise SystemExit("zbb not found. Build the project, pass --zbb, or set ZBB.")


def run_zbb(zbb: Path, args: Sequence[str], cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(zbb), *args],
        cwd=None if cwd is None else str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def combined_output(proc: subprocess.CompletedProcess) -> bytes:
    return (proc.stdout or b"") + (proc.stderr or b"")


def remove_tree(path: Path) -> None:
    if not path.exists():
        return
    last_error: Optional[OSError] = None
    for _ in range(10):
        try:
            shutil.rmtree(path)
            return
        except OSError as exc:
            last_error = exc
            time.sleep(0.2)
    if last_error is not None:
        raise last_error


def case_dir(case_id: str) -> Path:
    return WORK_ROOT / "cases" / case_id


def prepare_src(case_id: str, filename: str, payload: Optional[bytes], source: Optional[Path]) -> Path:
    # boffin: gave each case its own directory so parallel zbb runs do not share a cwd
    root = case_dir(case_id)
    if root.exists():
        remove_tree(root)
    src = root / "src"
    src.mkdir(parents=True)
    dest = src / filename
    if source is not None:
        shutil.copy2(source, dest)
    else:
        dest.write_bytes(payload or b"")
    return src


def test_roundtrip(
    zbb: Path,
    case_id: str,
    filename: str,
    zbb_args: Sequence[str],
    payload: Optional[bytes] = None,
    source: Optional[Path] = None,
) -> Optional[str]:
    src_dir = prepare_src(case_id, filename, payload, source)
    src = src_dir / filename
    archive = src_dir / (filename + ".zbb")

    proc = run_zbb(zbb, list(zbb_args) + [filename], cwd=src_dir)
    if proc.returncode != 0:
        return "compress exit {0}".format(proc.returncode)
    if not archive.is_file():
        return "archive missing"

    dec_dir = case_dir(case_id) / "dec"
    dec_dir.mkdir(parents=True)
    shutil.copy2(archive, dec_dir / (filename + ".zbb"))
    restored = dec_dir / filename

    proc = run_zbb(zbb, ["-d", filename + ".zbb"], cwd=dec_dir)
    if proc.returncode != 0:
        return "decompress exit {0}".format(proc.returncode)
    if not restored.is_file() or not filecmp.cmp(src, restored, shallow=False):
        return "restored bytes differ"
    return None


def test_cli(zbb: Path, args: Sequence[str], expected_exit: int, needle: bytes) -> Optional[str]:
    proc = run_zbb(zbb, list(args))
    if proc.returncode != expected_exit:
        return "exit {0} (expected {1})".format(proc.returncode, expected_exit)
    if needle not in combined_output(proc):
        return "missing text: {0}".format(needle.decode("ascii"))
    return None


def test_empty(zbb: Path) -> Optional[str]:
    src_dir = prepare_src("empty", "empty.bin", b"", None)
    archive = src_dir / "empty.bin.zbb"
    proc = run_zbb(zbb, ["empty.bin"], cwd=src_dir)
    if proc.returncode != 1:
        return "exit {0} (expected 1)".format(proc.returncode)
    if b"_File size is zero_" not in combined_output(proc):
        return "missing zero-size message"
    if archive.exists():
        return "failed compress left an archive"
    return None


def test_missing_tar() -> Optional[str]:
    return "TEST/test.tar is missing"


def test_bad_magic(zbb: Path) -> Optional[str]:
    src_dir = prepare_src("bad-magic", "bad.zbb", b"not-a-zbb-archive!!!!", None)
    proc = run_zbb(zbb, ["-d", "bad.zbb"], cwd=src_dir)
    if proc.returncode != 1:
        return "exit {0} (expected 1)".format(proc.returncode)
    text = combined_output(proc)
    if b"_Can't process archive_" not in text and b"_I/O error_" not in text:
        return "unexpected message"
    return None


def test_truncated_archive(zbb: Path) -> Optional[str]:
    src_dir = prepare_src("trunc-archive", "tiny.bin", bytes((1, 2, 3)), None)
    proc = run_zbb(zbb, ["tiny.bin"], cwd=src_dir)
    if proc.returncode != 0:
        return "setup compress exit {0}".format(proc.returncode)
    archive = src_dir / "tiny.bin.zbb"
    blob = archive.read_bytes()
    archive.write_bytes(blob[: min(16, max(1, len(blob) - 1))])
    dec_dir = case_dir("trunc-archive") / "dec"
    dec_dir.mkdir(parents=True)
    shutil.copy2(archive, dec_dir / "tiny.bin.zbb")
    proc = run_zbb(zbb, ["-d", "tiny.bin.zbb"], cwd=dec_dir)
    if proc.returncode != 1:
        return "trunc decompress exit {0}".format(proc.returncode)
    return None


def test_missing_archive(zbb: Path) -> Optional[str]:
    proc = run_zbb(zbb, ["-d", "no-such-file.zbb"])
    if proc.returncode != 1:
        return "exit {0} (expected 1)".format(proc.returncode)
    if b"_Can't open file_" not in combined_output(proc):
        return "missing open-file message"
    return None


def rt(
    zbb: Path,
    case_id: str,
    filename: str,
    zbb_args: Sequence[str],
    payload: bytes,
) -> Tuple[str, JobFn]:
    return (case_id, partial(test_roundtrip, zbb, case_id, filename, zbb_args, payload))


def run_job(name: str, fn: JobFn) -> Optional[str]:
    log("START {0}".format(name))
    try:
        error = fn()
    except Exception as exc:
        error = "{0}: {1}".format(type(exc).__name__, exc)
    if error:
        log("FAIL  {0}  {1}".format(name, error))
    else:
        log("PASS  {0}".format(name))
    return error


def build_jobs(zbb: Path) -> List[Tuple[str, JobFn]]:
    heavy: List[Tuple[str, JobFn]] = [
        ("tail7-b1", partial(test_roundtrip, zbb, "tail7-b1", "tail7.bin", ["-b1"], cycle_bytes(BLOCK_UNIT + 7))),
        ("big-b1", partial(test_roundtrip, zbb, "big-b1", "big.bin", ["-b1"], cycle_bytes(250000))),
    ]
    tar = TEST_ROOT / "test.tar"
    if tar.is_file():
        heavy.append(
            (
                "test.tar-p-b61",
                partial(test_roundtrip, zbb, "test.tar-p-b61", "test.tar", ["-p", "-b61"], None, tar),
            )
        )
    else:
        heavy.append(("test.tar-p-b61", test_missing_tar))

    midnul = bytearray(pattern_bytes(64, 37, 11))
    midnul[16:24] = b"\x00" * 8

    heavy.extend(
        [
            rt(zbb, "oneblock-b1", "oneblock.bin", ["-b1"], cycle_bytes(BLOCK_UNIT)),
            rt(zbb, "twoblock-b1", "twoblock.bin", ["-b1"], cycle_bytes(BLOCK_UNIT * 2)),
            rt(zbb, "underblock-b1", "underblock.bin", ["-b1"], cycle_bytes(BLOCK_UNIT - 1)),
            rt(zbb, "tail7-p-b1", "tail7.bin", ["-p", "-b1"], cycle_bytes(BLOCK_UNIT + 7)),
            rt(zbb, "big-p-b1", "big.bin", ["-p", "-b1"], cycle_bytes(250000)),
        ]
    )

    light: List[Tuple[str, JobFn]] = [
        ("usage", partial(test_cli, zbb, [], 0, b"Experimental compression program")),
        ("unknown-key", partial(test_cli, zbb, ["-x"], 1, b"_Unknown key in command line_")),
        ("no-file", partial(test_cli, zbb, ["-d"], 1, b"_No file to process_")),
        ("p-no-file", partial(test_cli, zbb, ["-p"], 1, b"_No file to process_")),
        ("bad-block", partial(test_cli, zbb, ["-b0", "x.bin"], 1, b"_Uncorrect buffer size_")),
        ("b-only", partial(test_cli, zbb, ["-b", "x.bin"], 1, b"_Uncorrect buffer size_")),
        ("b128", partial(test_cli, zbb, ["-b128", "x.bin"], 1, b"_Uncorrect buffer size_")),
        ("lone-dash", partial(test_cli, zbb, ["-"], 1, b"_Unknown action requested_")),
        ("two-files", partial(test_cli, zbb, ["a.bin", "b.bin"], 1, b"_Unknown action requested_")),
        ("d-with-p", partial(test_cli, zbb, ["-d", "-p", "x.zbb"], 1, b"_Unknown action requested_")),
        ("dp-token", partial(test_cli, zbb, ["-dp", "x.zbb"], 1, b"_Unknown action requested_")),
        ("empty", partial(test_empty, zbb)),
        ("bad-magic", partial(test_bad_magic, zbb)),
        ("trunc-archive", partial(test_truncated_archive, zbb)),
        ("missing-archive", partial(test_missing_archive, zbb)),
        rt(zbb, "raw1", "raw1.bin", [], bytes((0x7F,))),
        rt(zbb, "tiny", "tiny.bin", [], bytes((0x00, 0x01, 0x02))),
        rt(zbb, "nuls7", "nuls7.bin", [], bytes(7)),
        rt(zbb, "raw7", "raw7.bin", [], bytes(range(1, 8))),
        rt(zbb, "bwt8", "bwt8.bin", [], bytes(range(1, 9))),
        rt(zbb, "bwt9", "bwt9.bin", [], bytes(range(1, 10))),
        rt(zbb, "nuls8", "nuls8.bin", [], bytes(8)),
        rt(zbb, "lzp15-p", "lzp15.bin", ["-p"], pattern_bytes(15, 17, 3)),
        rt(zbb, "lzp16-p", "lzp16.bin", ["-p"], fill_bytes(16, 0x42)),
        rt(zbb, "lzp16", "lzp16.bin", [], fill_bytes(16, 0x42)),
        rt(zbb, "shrink20-p", "shrink20.bin", ["-p"], fill_bytes(20, 0x41)),
        rt(zbb, "ff32-p", "ff32.bin", ["-p"], fill_bytes(32, 0xFF)),
        rt(zbb, "mix64", "mix64.bin", [], pattern_bytes(64, 37, 11)),
        rt(zbb, "mix64-p", "mix64.bin", ["-p"], pattern_bytes(64, 37, 11)),
        rt(zbb, "rep64", "rep64.bin", [], fill_bytes(64, 0x41)),
        rt(zbb, "rep64-p", "rep64.bin", ["-p"], fill_bytes(64, 0x41)),
        rt(zbb, "midnul64", "midnul64.bin", [], bytes(midnul)),
        rt(zbb, "all256", "all256.bin", [], bytes(range(256))),
        rt(zbb, "odd255", "odd255.bin", [], pattern_bytes(255, 13, 7)),
        rt(zbb, "space-name", "a b.bin", ["-p"], pattern_bytes(32, 5, 9)),
        rt(zbb, "tiny-b1", "tiny.bin", ["-b1"], bytes((0x00, 0x01, 0x02))),
        rt(zbb, "combo-pb1", "rep64.bin", ["-pb1"], fill_bytes(64, 0x41)),
        rt(zbb, "upper-PB", "mix64.bin", ["-P", "-B1"], pattern_bytes(64, 37, 11)),
    ]
    return heavy + light


def main() -> int:
    parser = argparse.ArgumentParser(description="Cross-platform zbb case runner")
    parser.add_argument("--zbb", type=Path, default=None, help="path to the zbb binary")
    parser.add_argument(
        "--jobs",
        type=int,
        default=None,
        help="parallel workers (default: half the CPUs, at least 1)",
    )
    args = parser.parse_args()
    workers = default_workers() if args.jobs is None else max(1, args.jobs)

    zbb = find_zbb(args.zbb)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    log("zbb: {0}".format(zbb))
    log("workers: {0} (cpus {1})".format(workers, os.cpu_count() or 1))
    log("long jobs start first; short CLI/raw cases finish in milliseconds")

    if WORK_ROOT.exists():
        remove_tree(WORK_ROOT)
    WORK_ROOT.mkdir(parents=True)

    jobs = build_jobs(zbb)
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=workers) as pool:
        pending = [(name, pool.submit(run_job, name, fn)) for name, fn in jobs]
        results = [(name, future.result()) for name, future in pending]
    elapsed = time.monotonic() - started

    failed = [name for name, error in results if error]
    print("==== summary ====")
    print("elapsed {0:.1f}s".format(elapsed))
    if not failed:
        print("ALL PASSED")
        return 0
    print("FAILED={0}".format(len(failed)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
