# C++ task intrinsics

Include `<tasks/tasks.h>` when compiling with `extreme-cxx`. The compiler lowers
these calls directly to task instructions; no runtime library or process
scheduler is involved. The wrappers are always inlined so even terminal calls
reach the backend directly.

| API | Behavior |
|---|---|
| `issue(id, entry, predecessors = 0)` | TISSUE; return command status |
| `disarm(id)` | TDISARM; return command status |
| `read(id, uint32_t& result)` | Nonblocking TREAD; return status and write the result |
| `state(id)` | TSTATE; return lifecycle state |
| `id()` | TID; return current slot, or 0xffffffff for boot code |
| `finish(result = 0)` | TFINISH; publish and stop, never returns |
| `abort(result)` | TABORT; publish failure and cancel descendants, never returns |

All names are in `extreme::tasks`. Status/state enums match
[the task engine](../../doc/tasks_engine.md). `read` writes zero when no result
is available; check its returned status. Its result reference must designate
writable, naturally aligned storage. `issue` requires a compile-time-known
`void (*)()` entry with no arguments. The task ID and predecessor mask may be
computed at runtime. A task entry that returns normally publishes result zero.
Use shared argument records indexed by `id()` for other inputs.

```cpp
#include <tasks/tasks.h>
using namespace extreme::tasks;

void map() { finish(id() + 10); }
void reduce() {
    uint32_t a, b;
    if (read(0, a) != ok || read(1, b) != ok) abort(0xbad);
    finish(a + b);
}
extern "C" unsigned kernel() {
    auto join_status = issue(2, reduce, 3);
    auto first = issue(0, map);
    auto second = issue(1, map);
    return unsigned(join_status) | unsigned(first) | unsigned(second);
}
```

CPU0 can return to HALT immediately after issuing work. Other initially parked
cores are then available, and CPU0 can also become a worker. A task should issue
children and finish, with its continuation expressed as another task. Spinning
for children can deadlock if no free core remains. Task entries are dispatch
entry points; use ordinary helper functions for shared callable code instead
of calling a task entry directly. The backend rejects such ordinary calls when
they survive frontend optimization, and rejects dynamic entry pointers.

## Concurrent compiler storage

For a module that issues tasks, the compiler allocates **33 independent static
frames**: one for each task ID and one for boot code. Each frame contains the
SSA spills, local objects, PHI temporaries, and argument/return mailbox for the
entire reachable ordinary call graph. Globals remain shared. Each context can
call the same helper while another context is executing it; their local storage
and mailboxes do not alias. A slot is reused only under the task engine's normal
lifetime rules. A reused frame is not zeroed: C++ object initialization still
comes from the generated program.

Register r7 holds the frame base, calculated from TID at task entry; boot uses
context 32. Frame-free leaf tasks omit this setup. r0/r1 retain the register-lane
return stack. COPY in an ordinary helper saves/restores those whole registers
through two private frame words while preserving r7. Calls and spills use r2–r6.
Each frame has a power-of-two stride, recorded with its base in the image map.
The compiler checks total storage against configured memory and diagnoses
insufficient space. This storage is static; no memory stack or push/pop is added.
Recursion and indirect ordinary calls remain unsupported.

Only one boot producer may use an image's boot frame at once. Start other cores
at the image's idle entry. Multiple live tasks may execute the same task entry,
but each must own a distinct task ID. Private frames do not protect shared globals
from application data races. Task issue/finish/abort retain the hardware memory
publication guarantees.

Modules without task entries retain the original scalar ABI and performance.

## Tests and parallel memcpy bandwidth

`tests/intrinsics.cpp` checks issue, state, ID, result reads, disarm, abort,
cancellation, joins, normal void return, and two instances of one entry. It
exercises a shared helper concurrently with boot code, nested calls, and COPY
inside an ordinary helper, including controller stalls.

`../../tests/memcpy2.cpp` is both the target C++ program (with `EXTREME_KERNEL`)
and the common native/Verilator testbench. CPU0 issues two independent copy
tasks, and the test requires them to execute on two distinct other cores.
The dedicated configuration has three cores, 16-entry FIFOs, and four single-port
memory banks: source/destination for copy A, source/destination for copy B.
Two copies at one word per cycle require two reads plus two writes per cycle;
two of the existing single-port banks would not provide that bandwidth.

The test checks every destination word, unchanged sources, boundary guards,
issue statuses, published results, exact COPY word counts, and simultaneous
transfers. It counts startup and completion separately from the streaming window.
Running both 2048-word copies must cost **exactly 1024 more clocks** than running
both 1024-word copies, proving aggregate throughput of two copied words/clock.
The absolute limits are `N + 256` clocks end to end, `N + 96` for the
combined streaming window, and at least `N - 96` cycles transferring both words
together. These also reject serialization or long stalls.
A separate stalled run checks correctness without imposing the unstalled bound.
These are modeled cycles, not physical clock-frequency measurements.

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake -j2
ctest --test-dir build/cmake --output-on-failure -R 'compiler.(memcpy2|task_intrinsics)'
```

Validated native and Verilator timings (matching in both flows):

| Bus bits | Two 1024-word copies | Two 2048-word copies | Additional clocks |
|---:|---:|---:|---:|
| 64 | 1213 | 2237 | 1024 |
| 128 | 1177 | 2201 | 1024 |
| 512 | 1137 | 2161 | 1024 |

At 128 bits the combined streaming windows are 1076 and 2100 clocks; both
copies transfer a word in the same cycle 979 and 2003 times, respectively.
The fixed setup/drain cost is counted, not hidden by the throughput assertion.
The default suite passes 28 tests, and the new task compiler/dual-copy cases also
pass at 64 and 512 bits. The full-system synthesis smoke check passes.
