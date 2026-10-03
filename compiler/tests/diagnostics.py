import subprocess
import sys
import tempfile
from pathlib import Path
from textwrap import dedent

cases = [
    (
        "recursive",
        dedent("""\
            volatile unsigned n = 4;

            __attribute__((noinline, optnone)) unsigned f(unsigned x) {
                return x ? f(x - 1) + x : 0;
            }

            extern "C" unsigned kernel() {
                return f(n);
            }
            """),
        "recursive",
    ),
    (
        "double",
        dedent("""\
            volatile double x = 2;

            extern "C" unsigned kernel() {
                return unsigned(x * 3);
            }
            """),
        "unsupported",
    ),
    (
        "unresolved",
        dedent("""\
            extern unsigned missing(unsigned);

            extern "C" unsigned kernel() {
                return missing(7);
            }
            """),
        "unresolved",
    ),
    (
        "constructor",
        dedent("""\
            extern unsigned init();
            unsigned value = init();

            extern "C" unsigned kernel() {
                return value;
            }
            """),
        "constructors",
    ),
    (
        "task_dynamic",
        dedent("""\
            #include <tasks/tasks.h>

            void task() {}

            extreme::tasks::Entry volatile entry = task;

            extern "C" unsigned kernel() {
                return extreme::tasks::issue(0, entry);
            }
            """),
        "constant task entry",
    ),
    (
        "task_signature",
        dedent("""\
            #include <tasks/tasks.h>

            unsigned task() {
                return 7;
            }

            extern "C" unsigned kernel() {
                return extreme::tasks::issue(0, reinterpret_cast<extreme::tasks::Entry>(task));
            }
            """),
        "task entry must be void",
    ),
    (
        "task_ordinary",
        dedent("""\
            #include <tasks/tasks.h>
            volatile unsigned x;

            __attribute__((noinline, optnone)) void task() {
                x = x + 1;
            }

            extern "C" unsigned kernel() {
                task();
                return extreme::tasks::issue(0, task);
            }
            """),
        "ordinary functions",
    ),
    (
        "task_unknown",
        dedent("""\
            extern "C" unsigned __extreme_task_unknown(unsigned);

            extern "C" unsigned kernel() {
                return __extreme_task_unknown(0);
            }
            """),
        "unknown task intrinsic",
    ),
    (
        "task_bad_abi",
        dedent("""\
            extern "C" unsigned __extreme_task_finish(unsigned);

            extern "C" unsigned kernel() {
                return __extreme_task_finish(0);
            }
            """),
        "invalid task intrinsic signature",
    ),
    (
        "exceptions",
        dedent("""\
            extern "C" unsigned kernel() {
                throw 42;
            }
            """),
        "exceptions disabled",
    ),
]

simd_enabled = len(sys.argv) > 2 and sys.argv[2] == "ON"
cases.extend(
    [
        (
            "simd_signature",
            dedent("""\
            extern "C" void __extreme_simd_add32(unsigned*, unsigned*);
            unsigned data[16];

            extern "C" unsigned kernel() {
                __extreme_simd_add32(data, data);
                return 0;
            }
            """),
            "invalid SIMD intrinsic signature" if simd_enabled else "unresolved",
        ),
        (
            "simd_result",
            dedent("""\
            extern "C" unsigned __extreme_simd_splat32(unsigned*, unsigned);
            unsigned data[16];

            extern "C" unsigned kernel() {
                return __extreme_simd_splat32(data, 42);
            }
            """),
            "invalid SIMD intrinsic signature" if simd_enabled else "unresolved",
        ),
        (
            "simd_unknown",
            dedent("""\
            extern "C" void __extreme_simd_unknown(unsigned*, unsigned*, unsigned*);
            unsigned data[16];

            extern "C" unsigned kernel() {
                __extreme_simd_unknown(data, data, data);
                return 0;
            }
            """),
            "unknown SIMD intrinsic" if simd_enabled else "unresolved",
        ),
    ]
)

with tempfile.TemporaryDirectory(prefix="extreme-negative-") as temp:
    for name, source, diagnostic in cases:
        src = Path(temp) / (name + ".cpp")
        src.write_text(source)
        result = subprocess.run(
            [sys.executable, sys.argv[1], str(src), "-o", str(Path(temp) / (name + ".ecx"))],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if result.returncode == 0 or diagnostic not in result.stdout:
            raise RuntimeError(
                f"{name}: expected rejection containing {diagnostic}:\n{result.stdout}"
            )
        print(name, "correctly rejected")
