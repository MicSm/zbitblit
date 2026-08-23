#!/usr/bin/env python3
"""Cross-platform zbb cases. Leaves TEST/test.tar and example.cmd untouched.

    python3 TEST/run-cases.py
    python3 TEST/run-cases.py --zbb /path/to/zbb

Work products go to TEST/work/ (gitignored).
"""

from __future__ import annotations

import argparse
import filecmp
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional

TEST_ROOT = Path(__file__).resolve().parent
WORK_ROOT = TEST_ROOT / "work"
SRC_DIR = WORK_ROOT / "src"
BLOCK_UNIT = 100 * 1024


def pattern_bytes(length: int, mul: int, add: int) -> bytes:
    return bytes(((i * mul + add) & 255) for i in range(length))


def fill_bytes(length: int, value: int) -> bytes:
    return bytes([value & 255]) * length


def cycle_bytes(length: int) -> bytes:
    return bytes(i & 255 for i in range(length))


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


def run_zbb(zbb: Path, args: List[str], cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
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


def install_fixtures() -> None:
    SRC_DIR.mkdir(parents=True, exist_ok=True)
    (SRC_DIR / "tiny.bin").write_bytes(bytes((0x00, 0x01, 0x02)))
    (SRC_DIR / "raw7.bin").write_bytes(bytes(range(1, 8)))
    (SRC_DIR / "bwt8.bin").write_bytes(bytes(range(1, 9)))
    # boffin: kept a 15-byte -p case so preprocessing stays off below the length gate
    (SRC_DIR / "lzp15.bin").write_bytes(pattern_bytes(15, 17, 3))
    (SRC_DIR / "lzp16.bin").write_bytes(fill_bytes(16, 0x42))
    (SRC_DIR / "mix64.bin").write_bytes(pattern_bytes(64, 37, 11))
    (SRC_DIR / "rep64.bin").write_bytes(fill_bytes(64, 0x41))
    # boffin: kept a full block plus a 7-byte tail so the last chunk still takes the raw path
    (SRC_DIR / "tail7.bin").write_bytes(cycle_bytes(BLOCK_UNIT + 7))
    (SRC_DIR / "big.bin").write_bytes(cycle_bytes(250000))
    (SRC_DIR / "empty.bin").write_bytes(b"")

    tar = TEST_ROOT / "test.tar"
    if tar.is_file():
        shutil.copy2(tar, SRC_DIR / "test.tar")


def test_roundtrip(zbb: Path, name: str, zbb_args: List[str]) -> Optional[str]:
    src = SRC_DIR / name
    archive = SRC_DIR / (name + ".zbb")
    if archive.exists():
        archive.unlink()

    proc = run_zbb(zbb, zbb_args + [name], cwd=SRC_DIR)
    if proc.returncode != 0:
        return "compress exit {0}".format(proc.returncode)
    if not archive.is_file():
        return "archive missing"

    dec_dir = WORK_ROOT / "dec" / name
    if dec_dir.exists():
        remove_tree(dec_dir)
    dec_dir.mkdir(parents=True)
    dec_archive = dec_dir / (name + ".zbb")
    restored = dec_dir / name
    shutil.copy2(archive, dec_archive)

    proc = run_zbb(zbb, ["-d", name + ".zbb"], cwd=dec_dir)
    if proc.returncode != 0:
        return "decompress exit {0}".format(proc.returncode)
    if not restored.is_file() or not filecmp.cmp(src, restored, shallow=False):
        return "restored bytes differ"
    return None


def test_cli(zbb: Path, args: List[str], expected_exit: int, needle: bytes) -> Optional[str]:
    proc = run_zbb(zbb, args)
    if proc.returncode != expected_exit:
        return "exit {0} (expected {1})".format(proc.returncode, expected_exit)
    if needle not in combined_output(proc):
        return "missing text: {0}".format(needle.decode("ascii"))
    return None


def test_empty(zbb: Path) -> Optional[str]:
    archive = SRC_DIR / "empty.bin.zbb"
    if archive.exists():
        archive.unlink()
    proc = run_zbb(zbb, ["empty.bin"], cwd=SRC_DIR)
    if proc.returncode != 1:
        return "exit {0} (expected 1)".format(proc.returncode)
    if b"_File size is zero_" not in combined_output(proc):
        return "missing zero-size message"
    if archive.exists():
        return "failed compress left an archive"
    return None


def record(failed: List[str], name: str, error: Optional[str]) -> None:
    if error:
        print("FAIL  {0}  {1}".format(name, error))
        failed.append(name)
    else:
        print("PASS  {0}".format(name))


def main() -> int:
    parser = argparse.ArgumentParser(description="Cross-platform zbb case runner")
    parser.add_argument("--zbb", type=Path, default=None, help="path to the zbb binary")
    args = parser.parse_args()

    zbb = find_zbb(args.zbb)
    print("zbb: {0}".format(zbb))

    if WORK_ROOT.exists():
        remove_tree(WORK_ROOT)
    SRC_DIR.mkdir(parents=True)
    install_fixtures()

    failed: List[str] = []
    print("==== CLI ====")
    record(failed, "usage", test_cli(zbb, [], 0, b"Experimental compression program"))
    record(failed, "unknown-key", test_cli(zbb, ["-x"], 1, b"_Unknown key in command line_"))
    record(failed, "no-file", test_cli(zbb, ["-d"], 1, b"_No file to process_"))
    record(failed, "bad-block", test_cli(zbb, ["-b0", "x.bin"], 1, b"_Uncorrect buffer size_"))
    record(failed, "d-with-p", test_cli(zbb, ["-d", "-p", "x.zbb"], 1, b"_Unknown action requested_"))

    print("==== empty ====")
    record(failed, "empty", test_empty(zbb))

    print("==== roundtrip ====")
    record(failed, "tiny", test_roundtrip(zbb, "tiny.bin", []))
    record(failed, "raw7", test_roundtrip(zbb, "raw7.bin", []))
    record(failed, "bwt8", test_roundtrip(zbb, "bwt8.bin", []))
    record(failed, "lzp15-p", test_roundtrip(zbb, "lzp15.bin", ["-p"]))
    record(failed, "lzp16-p", test_roundtrip(zbb, "lzp16.bin", ["-p"]))
    record(failed, "mix64-p", test_roundtrip(zbb, "mix64.bin", ["-p"]))
    record(failed, "rep64-p", test_roundtrip(zbb, "rep64.bin", ["-p"]))
    record(failed, "tail7-b1", test_roundtrip(zbb, "tail7.bin", ["-b1"]))
    record(failed, "big-b1", test_roundtrip(zbb, "big.bin", ["-b1"]))

    if (SRC_DIR / "test.tar").is_file():
        record(failed, "test.tar-p-b61", test_roundtrip(zbb, "test.tar", ["-p", "-b61"]))
    else:
        record(failed, "test.tar-p-b61", "TEST/test.tar is missing")

    print("==== summary ====")
    if not failed:
        print("ALL PASSED")
        return 0
    print("FAILED={0}".format(len(failed)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
