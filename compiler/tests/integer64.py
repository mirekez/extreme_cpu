"""Run both call conventions against one generated SystemVerilog model."""

from pathlib import Path
import subprocess
import sys

root, build, cpphdl, cxx, bits, simd = sys.argv[1:]
root, build = Path(root), Path(build)
images = build / "compiler/images"
rtl = build / "rtl-integer64"
subprocess.run(
    [
        sys.executable,
        str(root / "scripts/test.py"),
        "--test",
        "Compiled",
        "--flow",
        "verilator",
        "--bits",
        bits,
        "--cores",
        "3",
        "--banks",
        "4",
        "--words",
        "16384",
        "--image",
        str(images / "integer64_reentrant.ecx"),
        "--expected",
        "42",
        "--mode",
        "stalls",
        "--limit",
        "20000000",
        "--build-dir",
        str(rtl),
        "--cpphdl",
        cpphdl,
        "--cxx",
        cxx,
        *(["--simd"] if simd == "ON" else []),
    ],
    check=True,
)
configuration = f"b{bits}-d16-c3-m4-w16384" + ("-simd" if simd == "ON" else "")
executable = rtl / configuration / "Compiled/obj/regression"
subprocess.run(
    [str(executable), str(images / "integer64_static.ecx"), "42", "20000000", "stalls"],
    check=True,
)
