#!/usr/bin/env python3
"""Build the same Ethernet board simulator against generated RTL using Verilator."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--build-dir", type=Path, required=True)
p.add_argument("--bits", type=int, choices=[64, 128, 256, 512], default=128)
p.add_argument("--simd", action="store_true")
p.add_argument("--cores", type=int, default=1)
p.add_argument("--banks", type=int, default=2, help="RAM banks, plus one MMIO bank")
p.add_argument("--bank-words", type=int, default=16384)
p.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
p.add_argument("--depth", type=int, default=16)
p.add_argument("--cpphdl", type=Path, default=Path.home() / "cpphdl")
p.add_argument("--build-only", action="store_true")
p.add_argument("arguments", nargs=argparse.REMAINDER)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
folder = a.build_dir.resolve()
folder.mkdir(parents=True, exist_ok=True)
flags = [
    "-std=c++20",
    "-O2",
    "-fno-strict-aliasing",
    f"-I{a.cpphdl}/include",
    "-DEC_DEVICES",
    *(["-DEC_SIMD"] if a.simd else []),
    f"-DEC_BITS={a.bits}",
    f"-DEC_CORES={a.cores}",
    f"-DEC_BANKS={a.banks + 1}",
    f"-DEC_BANK_WORDS={a.bank_words}",
    f"-DEC_DEPTH={a.depth}",
]
source = root / "devices/board.cpp"
generated = folder / "generated"
subprocess.run(
    [str(a.cpphdl / "build/cpphdl"), "--generated-dir", str(generated), str(source), "--", *flags],
    check=True,
)
sv = sorted(generated.glob("*_pkg.sv")) + sorted(
    x for x in generated.glob("*.sv") if not x.name.endswith("_pkg.sv")
)
verilator = (
    os.environ.get("VERILATOR")
    or shutil.which("verilator")
    or str(a.cpphdl / ".conda/bin/verilator")
)
with (folder / "verilator-build.log").open("w") as log:
    try:
        subprocess.run(
            [
                verilator,
                "--cc",
                "--exe",
                "--build",
                "-j",
                "2",
                "-Wno-fatal",
                "--top-module",
                "System",
                "--Mdir",
                str(folder / "obj"),
                "-MAKEFLAGS",
                f"CXX={a.cxx} LINK={a.cxx}",
                "-CFLAGS",
                " ".join(flags + ["-DVERILATOR"]),
                *map(str, sv),
                str(source),
                "-o",
                "board",
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    except subprocess.CalledProcessError:
        print((folder / "verilator-build.log").read_text()[-12000:])
        raise
    finally:
        for cache in (folder / "obj").glob("*__pch.h.*.gch"):
            cache.unlink()
if not a.build_only:
    arguments = a.arguments[1:] if a.arguments[:1] == ["--"] else a.arguments
    subprocess.run([str(folder / "obj/board"), *arguments], check=True)
