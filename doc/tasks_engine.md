# Hardware task engine

The task engine dispatches short, nonpreemptive work units to available CPU
cores without an OS scheduler, processes, or context switching. A running core
can issue more tasks. A task can publish a 32-bit result to any number of
successors; a successor runs only when all its predecessors have completed.

`rtl/TasksControl.h` implements the shared controller. `rtl/System.h` connects
its command/response and launch interfaces to every core. The controller is
separate from memory arbitration and never executes task instructions or copies
data itself. Task instructions are part of `arch/instruction.h` and `rtl/Core.h`.

## Shared task table

There are exactly 32 slots, addressed by task IDs 0–31. Each contains:

| Field | Bits | Meaning |
|---|---:|---|
| Call address | 32 | BUS_WIDTH-aligned byte address of the task entry |
| Predecessor mask | 32 | Bit i requires successful completion of slot i |
| Result | 32 | Published value, including application-defined error values |
| State | 3 | Empty, waiting, reserved, running, complete, failed, cancelled |

Thus the requested payload is 32 registers of 96 bits, with separate lifecycle
metadata. The engine also stores one reservation, arbitration pointers, per-core
ownership, and registered launch/response fields. The table itself is the pending
list; it is not a memory-linked queue. Issued fields are immutable until the
slot is terminal and safe to reuse. A result is meaningful only in a terminal
state; zero is a valid result, not an indication of readiness.

| State | Value | Meaning |
|---|---:|---|
| EMPTY | 0 | Available; no result |
| WAITING | 1 | Issued, including ready work not yet selected |
| RESERVED | 2 | Selected by the first dispatch stage; immutable |
| RUNNING | 3 | Launch committed to an owning core; immutable |
| COMPLETE | 4 | Normal completion; dependents may run |
| FAILED | 5 | Abort or abnormal exit; result remains readable |
| CANCELLED | 6 | Disarmed pending task or a descendant of failed work |

All fields and ownership reset together. Slot 31 and its mask bit are fully
supported; masks are unsigned.

## Two-stage dispatch and sequencing

Every cycle a parallel comparison across waiting slots computes:

```
ready[i] = state[i] == WAITING &&
           (predecessors[i] & completed_mask) == predecessors[i]
```

A rotating priority encoder selects one ready slot and registers its reservation.
On the next clock the controller searches free cores in round-robin order,
records ownership, and registers a launch containing the address and task ID.
The core consumes that pulse on the following edge. If no core is free, the
reservation stays locked until a core becomes available. There is one selection
reservation, so the maximum dispatch rate is one task every two clocks.

A core is available only after it has started and halted, its memory queues and
pending fetch have drained, and it has no fault. The controller's ownership and
launch bits additionally prevent assigning the same core twice while its old
halted signal is still visible. Launch clears the new task's registers, SP,
loop state, and instruction buffers; it preserves shared memory. Tasks start
at their entry, rather than returning through a caller's register stack.

Commands from cores are serialized by a separate round-robin arbiter. A core
holds its request until acceptance, then waits for its registered response.
Only one command is accepted per cycle. Address, mask, value, command and ID
remain stable through acceptance. Responses are one-cycle pulses one edge after
acceptance; the core is already waiting and needs no response backpressure.

This serialization defines issue/disarm races. If a command targets a waiting
slot on the same edge as selection, the command takes precedence and selection
skips that slot. Already reserved or running slots reject both issue and disarm.
Completion can only affect the task owned by the requesting core; it does not
accept an arbitrary task ID. A nonowner completion is rejected; a simultaneous core fault takes precedence
over completion. New completion
and cancellation states participate in dependency matching on subsequent clocks,
not combinationally through the command input.

## Instructions

All register operands below refer to low 32-bit values. Written results are
zero-extended to BUS_WIDTH. Instructions never cross a bus-word boundary.
The assembler inserts NOP padding when necessary.

| Opcode | Instruction | Encoding | Behavior |
|---|---|---|---|
| 50 | TISSUE d,a,b | op,d,a,b | Issue ID from r[d], entry from r[a], predecessor mask from r[b]; replace r[d] with command status |
| 51 | TDISARM d,a | op,d,a | Disarm ID in r[a]; status to r[d] |
| 52 | TREAD d,a,b | op,d,a,b | Read result for ID in r[a] into r[d], status into r[b]; d and b must differ |
| 53 | TFINISH d | op,d | Publish r[d] as the current task's result and stop this task |
| 54 | TABORT d | op,d | Publish r[d] as an error, stop, and cancel descendants |
| 55 | TSTATE d,a | op,d,a | Return slot state for ID in r[a]; invalid ID returns 0xffffffff |
| 56 | TID d | op,d | Return current task ID; boot code outside a task gets 0xffffffff |

Opcode numbers are hexadecimal. TISSUE reads all operands before overwriting
its ID/status register. Invalid register encodings fault the core; controller
validation errors return status without changing the descriptor.

| Command status | Value | Meaning |
|---|---:|---|
| OK | 0 | Accepted operation / available result |
| INVALID | 1 | Invalid ID, unaligned entry, self-dependency, or unknown command |
| LOCKED | 2 | Live task, reserved task, or result protected by active dependents |
| NOT_READY | 3 | TREAD of an empty, waiting, reserved, or running slot |
| NOT_OWNER | 4 | Completion attempted outside a healthy owned task |

TREAD is nonblocking. On NOT_READY or INVALID its result output is zero and its
status distinguishes that from a published zero result. Read TSTATE to distinguish
normal completion from failure/cancellation. TFINISH/TABORT have no continuation;
if their ownership check fails the core faults. HALT parks boot cores, but HALT
inside an owned task is an abnormal exit, not a successful task return. Ordinary
RET still serves local function calls and does not complete a task.

Controller wire commands 1–6 are issue, disarm, read, finish, abort, and state,
respectively. TID is served locally. The debug table outputs are read-only and
are not needed by software. System launch outputs provide test/debug observability.

## Publication and shared memory

TISSUE drains the issuing core's previous stores and loads before publishing the
descriptor. TFINISH and TABORT also drain before publishing their result. This
ensures that initialized task data and completed task writes are visible before
successors execute. Instruction buffers are invalidated during that drain.
Controller completion is never inferred merely from store FIFO acceptance.

The predecessor mask establishes ordering, not memory ownership. Tasks that
write the same locations concurrently still need an application protocol.
Pass small outputs through the 32-bit result; pass larger outputs through shared
memory, for example by publishing a pointer. TID can index per-task argument
records in shared memory. There are no implicit argument registers, data-stack
allocation, or automatic context saves.

TFINISH with an error-valued result is still normal completion: dependents run
and can inspect it. TABORT is the explicit request for **no continuation**.
The aborted slot becomes FAILED. Each waiting slot with a FAILED/CANCELLED
predecessor becomes CANCELLED, retaining an error result from its lowest-numbered
failed predecessor visible on that clock. Cancellation propagates one dependency
level per clock. Once cancelled, that result is stable even if another predecessor
fails later. Unrelated tasks and sibling predecessors continue running.

TDISARM of WAITING work cancels it with result `0xfffffffd`; cancellation also
propagates to its descendants. TDISARM of an unreferenced terminal slot clears
it to EMPTY. Disarming an unreferenced EMPTY slot is a successful no-op. It cannot
interrupt running work or revoke a dispatch reservation. A fault or HALT without
TFINISH/TABORT marks the owned slot FAILED with `0xfffffffe`. Faulted cores are
not reused until reset; accepted memory operations may still complete after a
fault, as specified by the core's existing fault contract. An abnormal failure
is not a successful memory-publication fence.

## Graph construction and slot reuse

Masks may refer to EMPTY slots. This permits building a graph backward: issue
its join first, then its leaves. Empty predecessors do not satisfy dependencies;
issuing a previously empty referenced slot is allowed. A zero-mask task can run
immediately after issue, so publish its inputs first. Direct self-dependencies
are rejected. Longer cycles and never-issued predecessors remain waiting;
software must submit a DAG or disarm blocked work to cancel it. This revision
does not implement cycle detection or automatic deadlock recovery.

Do not overwrite terminal results while consumers may still read them. A
WAITING, RESERVED, or RUNNING task protects every slot named in its original
predecessor mask. Such a terminal slot cannot be reissued or cleared. The
original mask is retained throughout execution, even after dependencies become
ready. Once all those consumers are terminal, software may directly reissue the
slot or clear it first. Unreferenced completed tasks are not automatically erased.
Issuing into any live slot returns LOCKED; there is no silent replacement.

IDs are slot indices, not generation handles. A new graph must recycle terminal
slots deliberately. A predecessor that is still COMPLETE satisfies newly issued
masks immediately. To wait for a *new* execution of that ID, rearm it before
issuing consumers or clear the old graph and build the new graph backward.
Readers outside the declared dependency graph must coordinate reuse themselves.

The producer does not block waiting for a core or a free slot: issue returns a
status, and dispatch runs independently. Software should express waits as
continuation tasks. If all cores spin waiting for children, no free core remains
to run those children. A task should issue children, issue a join, and finish.
Boot code can park with HALT and be reused as a worker. A single core supports
the same model by executing tasks sequentially.

## Map/reduce example

Suppose task IDs 0–7 perform independent map operations, ID 8 reduces their
results, and ID 9 consumes the reduction:

```
issue(9, consume_entry, 1 << 8)
issue(8, reduce_entry, 0x000000ff)
for i in 0..7:
    issue(i, map_entry[i], 0)
halt_boot_core()

map_entry[i]:
    finish(map(shared_input[task_id()]))

reduce_entry:
    sum = 0
    for i in 0..7:
        result, status = read_result(i)
        // All eight results are available due to the predecessor mask.
        if is_application_error(result):
            abort(result) // ID 9 is cancelled
        sum += result
    finish(sum)

consume_entry:
    result, status = read_result(8)
    finish(result)
```

These are pseudocode operations corresponding directly to the task instructions.
The executable assembled examples are in `tests/tasks.cpp`. A mapper can also
TABORT directly, preventing the reducer and consumer from ever launching.
The test suite demonstrates a running task issuing children that depend on its
own eventual result, then finishing to release them.

The freestanding compiler exposes these operations through
[`compiler/tasks/tasks.h`](../compiler/tasks/tasks.h). It gives every task ID and
boot code separate static spill/local/mailbox storage, so task instances can call
shared helpers concurrently. See the [C++ task ABI](../compiler/tasks/README.md)
for entry-function restrictions, examples, and the compiled dual-memcpy test.

## Verification

`rtl/tests/TasksControl.cpp` uses the same source for native C++HDL and generated
SystemVerilog/Verilator. It checks reset, slot 31, empty/zero results, malformed
commands, ownership, reservation/running locks, simultaneous selection/disarm,
concurrent issue arbitration, result retention, reuse, disarm/abort propagation,
missing/cyclic dependencies, unexpected HALT/fault, disabled scheduling, and
80 randomized full 32-slot DAGs with independently calculated expected results.

`tests/tasks.cpp` assembles the complete CPU and runs ten scenarios: parallel
map/reduce, hardware cancellation, consumer-inspected errors, dynamic child
graphs, and long chains, each with and without controller stalls. It checks
exact launch counts, real parallel execution, task identity, result values,
shared-memory publication, cancellation side effects, and eventual quiescence.
Both flows use identical programs and checks. Existing scalar/compiler and
memcpy bandwidth regressions remain part of CTest.

`tasks_idle_out` means no waiting/reserved/running slots. Terminal results can
remain in the table while idle. To declare the system quiescent, also require
all cores halted and memory queues drained; a boot producer could still issue
work while the table is idle. Host programming mode requires that quiescent
condition, or reset, and must not interrupt a live task graph.

Validated configurations for both the controller and complete task-system tests:

| Bus bits | Cores | FIFO depth | Memory banks |
|---:|---:|---:|---:|
| 64 | 1 | 2 | 1 |
| 128 | 2 | 16 | 2 |
| 512 | 4 | 16 | 2 |

The default CTest suite, including C++ task intrinsics and parallel memcpy,
passes all 28 tests. Native and Verilator system task
runs produce matching cycle counts. Full-system Yosys synthesis and
`check -assert` pass for the 64-bit, one-core smoke configuration. Existing
compiled 1,024-word memcpy remains at 1,084 clocks at 128 bits. These are
simulation and logic-synthesis checks, not post-placement timing measurements.
