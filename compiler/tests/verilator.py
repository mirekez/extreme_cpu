import subprocess
import sys
from pathlib import Path

root, build, cpphdl, cxx, bits, depth, cores, banks, simd = sys.argv[1:]
root = Path(root)
build = Path(build)
images = build / "compiler/images"
subprocess.run(
    [
        sys.executable,
        str(root / "scripts/test.py"),
        "--flow",
        "verilator",
        "--test",
        "Compiled",
        "--image",
        str(images / "hello.ecx"),
        "--expected",
        "42",
        "--bits",
        bits,
        "--depth",
        depth,
        "--cores",
        cores,
        "--banks",
        banks,
        "--build-dir",
        str(build / "rtl"),
        "--cxx",
        cxx,
        "--cpphdl",
        cpphdl,
        *(["--simd"] if simd == "ON" else []),
    ],
    check=True,
)
configuration = f"b{bits}-d{depth}-c{cores}-m{banks}" + ("-simd" if simd == "ON" else "")
exe = build / "rtl" / configuration / "Compiled/obj/regression"
cases = [
    ("scalars", 13630),
    ("calls", 96),
    ("libraries", 205),
    ("copy", 42),
    ("calls_copy", 37),
]
if simd == "ON":
    cases.append(("simd", 42))
for name, value in cases:
    subprocess.run(
        [str(exe), str(images / (name + ".ecx")), str(value), "5000000", "stalls"], check=True
    )
if int(banks) >= 2 and int(depth) >= 8:
    subprocess.run([str(exe), str(images / "copy_bandwidth.ecx"), "0", "1089", "copy"], check=True)
