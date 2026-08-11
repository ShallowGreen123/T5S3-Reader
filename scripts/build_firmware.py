#!/usr/bin/env python3
"""Build board-specific release firmware into the repository firmware directory."""

from __future__ import annotations

import argparse
import configparser
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class BoardBuild:
    name: str
    environment: str
    board_id: str


BOARD_BUILDS = {
    "t5s3": BoardBuild("t5s3", "gh_release", "t5s3-pro"),
    "epd47": BoardBuild("epd47", "lilygo-epd47-s3-release", "lilygo-epd47-s3"),
}

FIRMWARE_PREFIX = "corsspoint_lilygo"
VERSION_PATTERN = re.compile(r"^[0-9A-Za-z][0-9A-Za-z._+-]*$")


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build release firmware for all supported boards."
    )
    parser.add_argument(
        "boards",
        nargs="*",
        choices=BOARD_BUILDS,
        metavar="BOARD",
        help="boards to build (default: t5s3 epd47)",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=positive_int,
        default=4,
        help="number of parallel PlatformIO build jobs (default: 4)",
    )
    return parser.parse_args()


def load_version(platformio_ini: Path) -> str:
    config = configparser.ConfigParser(interpolation=None)
    if not config.read(platformio_ini, encoding="utf-8"):
        raise RuntimeError(f"cannot read {platformio_ini}")
    if not config.has_option("crosspoint", "version"):
        raise RuntimeError(f"[crosspoint] version is missing from {platformio_ini}")

    version = config.get("crosspoint", "version").strip()
    if not VERSION_PATTERN.fullmatch(version):
        raise RuntimeError(f"invalid [crosspoint] version for a file name: {version!r}")
    return version


def platformio_command() -> list[str]:
    executable = shutil.which("pio")
    if executable:
        return [executable]
    return [sys.executable, "-m", "platformio"]


def contains_board_marker(firmware: Path, board_id: str) -> bool:
    marker = f"CROSSPOINT_BOARD_ID:{board_id}".encode("ascii")
    return marker in firmware.read_bytes()


def build_board(repo_root: Path, pio: list[str], board: BoardBuild, jobs: int) -> Path:
    print(f"\n==> Building {board.name} with environment {board.environment}", flush=True)
    subprocess.run(
        [*pio, "run", "-e", board.environment, "-j", str(jobs)],
        cwd=repo_root,
        check=True,
    )

    firmware = repo_root / ".pio" / "build" / board.environment / "firmware.bin"
    if not firmware.is_file():
        raise RuntimeError(f"PlatformIO did not create {firmware}")
    if not contains_board_marker(firmware, board.board_id):
        raise RuntimeError(
            f"{firmware} does not contain the expected board marker for {board.board_id}"
        )
    return firmware


def publish_firmware(
    output_dir: Path, version: str, artifacts: list[tuple[BoardBuild, Path]]
) -> list[Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    staged: list[tuple[Path, Path]] = []

    try:
        for board, source in artifacts:
            output = output_dir / f"{FIRMWARE_PREFIX}_{board.name}_{version}.bin"
            temporary = output.with_name(f".{output.name}.tmp")
            shutil.copy2(source, temporary)
            staged.append((temporary, output))

        for temporary, output in staged:
            os.replace(temporary, output)
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)

    return [output for _, output in staged]


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent

    try:
        version = load_version(repo_root / "platformio.ini")
        selected = list(dict.fromkeys(args.boards or BOARD_BUILDS))
        pio = platformio_command()
        artifacts = [
            (BOARD_BUILDS[name], build_board(repo_root, pio, BOARD_BUILDS[name], args.jobs))
            for name in selected
        ]
        outputs = publish_firmware(repo_root / "firmware", version, artifacts)
    except subprocess.CalledProcessError as error:
        print(f"\nBuild failed with exit code {error.returncode}.", file=sys.stderr)
        return error.returncode or 1
    except (OSError, RuntimeError, configparser.Error) as error:
        print(f"\nError: {error}", file=sys.stderr)
        return 1

    print(f"\nBuilt firmware version {version}:")
    for output in outputs:
        print(f"  {output.relative_to(repo_root)} ({output.stat().st_size:,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
