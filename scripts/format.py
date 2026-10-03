#!/usr/bin/env python3
"""Format handwritten C++, Python, and CMake files, or check their formatting."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report changes without editing files")
    args = parser.parse_args()

    # Include new source files, while honoring .gitignore and excluding generated output.
    paths = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        text=True,
    ).split("\0")
    files = sorted({path for path in paths if path and (ROOT / path).is_file()})
    cpp = [path for path in files if Path(path).suffix in {".c", ".cpp", ".h"}]
    python = [path for path in files if path.endswith((".py", ".py.in"))]
    cmake = [path for path in files if Path(path).name == "CMakeLists.txt"]

    cpphdl = Path(os.environ.get("CPPHDL_HOME", str(Path.home() / "cpphdl")))
    clang_format = (
        os.environ.get("CLANG_FORMAT")
        or shutil.which("clang-format-21")
        or shutil.which("clang-format")
        or str(cpphdl / ".conda/bin/clang-format")
    )
    version = subprocess.check_output([clang_format, "--version"], text=True)
    if "version 21." not in version:
        raise RuntimeError("Use clang-format 21; set CLANG_FORMAT to its executable path.")

    commands = [
        [clang_format, *(["--dry-run", "--Werror"] if args.check else ["-i"]), *cpp],
        [sys.executable, "-m", "black", *(["--check"] if args.check else []), *python],
        [sys.executable, "-m", "cmakelang.format", "--check" if args.check else "-i", *cmake],
    ]
    for command in commands:
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Formatting failed: {error}", file=sys.stderr)
        print(
            "Install Python formatters with: python3 -m pip install -r requirements-format.txt",
            file=sys.stderr,
        )
        raise SystemExit(1)
