# Instruction set

Instructions occupy 1–6 bytes and never cross a BUS_WIDTH word boundary. Pad
with `NOP` as needed. All immediate and word-target fields are little-endian.
[`arch/instruction.h`](../arch/instruction.h) contains opcode values, operand
descriptions, instruction lengths, and the assembler helper; host structure packing is not the encoding.

Registers `r0..r(N-1)` are BUS_WIDTH bits. All are writable. Scalar arithmetic
reads bits 31:0, computes modulo 2^32, and zero-extends its result. `MOV`, `LD`,
and `ST` transfer whole registers. Addresses use bits 31:0 of their register.
Shift counts use five low bits. There are no implicit flags or split immediates.

## Encoding conventions

`word_bytes = BUS_WIDTH/8` and `N = EC_REGS`. Operands `d`, `a`, and `b` are
one-byte register indices in `0..N-1`; they do not encode subregisters. In the
operation descriptions, scalar register operands mean their low 32-bit values.
All bytes of an immediate are present in the instruction; no separate upper/lower
immediate instructions are needed.

| Bytes | Encoding | Instructions |
|---:|---|---|
| 1 | op | NOP, HALT, BARRIER, COPY, LOOP, END, NEXTSRC, NEXTDST, RET |
| 2 | op,register | JMPR, CALLR, TFINISH, TABORT, TID |
| 3 | op,d,a | MOV, LD, ST, TDISARM, TSTATE, VSPLAT32 |
| 4 | op,d,a,b | Scalar and SIMD binary ALU operations, TISSUE, TREAD |
| 4 | op,d,a,flags | LDS, STS; the last byte is a memory flag, not a register |
| 5 | op,imm32 | JMP, CALL, STACKLIMIT |
| 6 | op,d,imm32 | LI, BZ |

Instruction words contain consecutive instruction bytes, with the first byte
in bits 7:0 of the fetched word. Insert NOP bytes before an instruction that
would cross a word boundary. JMP, BZ, and CALL immediates are absolute word
indices; JMPR, CALLR, and task entry registers contain byte addresses aligned
to `word_bytes`. LI and STACKLIMIT immediates are integer values.

`Assembler::emit(op, d, a, b, imm)` pads before an instruction when needed.
Supply a direct branch/call target or STACKLIMIT value in `imm`; supply a
JMPR/CALLR operand in `d`, despite its mnemonic spelling `a`. For LDS/STS,
supply the `MemoryFlag` value in `b`. The helper validates the opcode and bus
width; it does not validate register indices, memory flags, or runtime addresses.
Use `align()` before branch targets and after CALL/CALLR, whose return addresses
skip to the next word boundary.

## Base instructions

| Opcode | Mnemonic | Bytes / fields | Operation |
|---|---|---|---|
| 00 | NOP | 1 | No operation |
| 01 | HALT | 1 | Drain local memory operations, stop |
| 02 | BARRIER | 1 | Drain local operations; invalidate instruction buffers |
| 03 | COPY | 1 | Stream r2 words from address r0 to address r1 |
| 04 | LOOP | 1 | Save next byte PC as implicit loop anchor |
| 05 | END | 1 | Decrement r2; repeat at anchor if count remains |
| 06 | NEXTSRC | 1 | r0 = zero_extend(u32(r0) + word_bytes) |
| 07 | NEXTDST | 1 | r1 = zero_extend(u32(r1) + word_bytes) |
| 08 | RET | 1 | Pop a bus-aligned byte PC from register return lanes |
| 10 | LI d, imm32 | 6: op,d,imm32 | Zero-extend an arbitrary 32-bit immediate |
| 11 | MOV d,a | 3: op,d,a | Full-width copy r[a] to r[d] |
| 20 | ADD d,a,b | 4: op,d,a,b | Scalar addition |
| 21 | SUB d,a,b | 4: op,d,a,b | Scalar subtraction |
| 22 | AND d,a,b | 4: op,d,a,b | Scalar bitwise AND |
| 23 | OR d,a,b | 4: op,d,a,b | Scalar bitwise OR |
| 24 | XOR d,a,b | 4: op,d,a,b | Scalar bitwise XOR |
| 25 | SHL d,a,b | 4: op,d,a,b | Scalar logical left shift |
| 26 | SHR d,a,b | 4: op,d,a,b | Scalar logical right shift |
| 27 | SLTU d,a,b | 4: op,d,a,b | 1 if unsigned a < b, otherwise 0 |
| 28 | SAR d,a,b | 4: op,d,a,b | Arithmetic right shift of signed low 32 bits |
| 29 | MUL d,a,b | 4: op,d,a,b | Low 32 bits of product |
| 2A | DIVU d,a,b | 4: op,d,a,b | Unsigned quotient a / b |
| 2B | DIVS d,a,b | 4: op,d,a,b | Signed quotient a / b, truncated toward zero |
| 2C | REMU d,a,b | 4: op,d,a,b | Unsigned remainder a % b |
| 2D | REMS d,a,b | 4: op,d,a,b | Signed remainder; nonzero remainder has dividend's sign |
| 2E | SLT d,a,b | 4: op,d,a,b | Signed comparison |
| 30 | LD d,a | 3: op,d,a | Load one aligned word from address r[a] |
| 31 | ST d,a | 3: op,d,a | Enqueue full-width r[d] at address r[a] |
| 32 | LDS d,a,flags | 4: op,d,a,flags | Scalar load from byte address r[a] |
| 33 | STS d,a,flags | 4: op,d,a,flags | Masked store of low bits of r[d] |
| 40 | JMP word32 | 5: op,word32 | Absolute PC = word32 * word_bytes |
| 41 | BZ d,word32 | 6: op,d,word32 | Jump if low 32 bits of r[d] are zero |
| 42 | CALL word32 | 5: op,word32 | Save aligned continuation and branch |
| 43 | JMPR a | 2: op,a | Jump to aligned byte address r[a] |
| 44 | CALLR a | 2: op,a | Save continuation, call aligned byte address r[a] |
| 45 | STACKLIMIT imm32 | 5: op,imm32 | Set allowed return slots with SP = 0 |
| 50 | TISSUE d,a,b | 4: op,d,a,b | Issue ID r[d], entry r[a], mask r[b]; status to r[d] |
| 51 | TDISARM d,a | 3: op,d,a | Disarm ID r[a]; status to r[d] |
| 52 | TREAD d,a,b | 4: op,d,a,b | Result r[d] and status r[b] for task ID r[a] |
| 53 | TFINISH d | 2: op,d | Publish result r[d] and stop current task |
| 54 | TABORT d | 2: op,d | Publish error r[d], stop and cancel descendants |
| 55 | TSTATE d,a | 3: op,d,a | Task state for ID r[a] to r[d] |
| 56 | TID d | 2: op,d | Current task ID to r[d] |

Opcode numbers in the table are hexadecimal. Unknown opcodes and register
indices >= N fault. A malformed instruction extending beyond its containing
word faults. Target multiplication and address arithmetic wrap to 32 bits.

## Compact loops and copies

`LOOP` requires r2 > 0 and no active compact loop. `END` requires an active
compact loop. Nesting compact loops faults; implement nested/general loops
using scalar registers and branches. For an empty iteration range, branch
around LOOP or use COPY, which explicitly accepts r2 = 0.

```
LI r0, source_byte_address
LI r1, destination_byte_address
LI r2, word_count
COPY
BARRIER
HALT
```

COPY is executed by the core's three stages, not by an external DMA controller.
The count uses bits 31:0 of r2. For a nonzero count, on completion of enqueueing,
r0/r1 advance by the original count * word_bytes, zero-extending the resulting
32-bit addresses, and r2 becomes zero. It leaves other architectural registers unchanged. A zero
count produces no data transactions and leaves all registers unchanged. Source
and destination must be aligned and the ranges must not overlap.

The ordinary compact-loop equivalent, for nonzero counts, is:

```
LI r0, source_byte_address
LI r1, destination_byte_address
LI r2, word_count
LOOP
    LD r3, r0
    ST r3, r1
    NEXTSRC
    NEXTDST
END
HALT
```

The loop anchor is a byte PC saved in the core, so END has no encoded distance
and can return across instruction words. General branches use word targets;
align their destinations with NOP padding. Frequent loop-control and iteration
instructions remain exactly one byte independent of BUS_WIDTH.

## Scalar memory access and ordering

LDS/STS flags follow RISC-V funct3:

| Flag | `MemoryFlag` | LDS | STS |
|---:|---|---|---|
| 0 | Byte | Sign-extend one byte to 32 bits | Store low byte |
| 1 | Half | Sign-extend two bytes to 32 bits | Store low two bytes |
| 2 | Word | Load four bytes | Store low four bytes |
| 4 | ByteUnsigned | Zero-extend one byte | Invalid |
| 5 | HalfUnsigned | Zero-extend two bytes | Invalid |

Other flags fault. Halfwords/words require natural alignment. Loads extend to
32 bits, then zero all higher register bits; signed byte/halfword loads sign
extend only within those low 32 bits. Stores preserve unselected memory bytes
using controller byte enables. Every bus transaction remains BUS_WIDTH wide.

ST and STS retire after enqueueing their write; COPY also returns before its
store queue necessarily drains. A following load can therefore observe old
memory. There is no address hazard detection, store-to-load forwarding, or cache.
Use BARRIER before a dependent read. BARRIER drains the issuing core's memory
operations and invalidates its two instruction buffers; it does not wait for
other cores or provide mutual exclusion. HALT also drains local operations.

## Scalar arithmetic and register returns

All scalar ALU instructions replace r[d] with a zero-extended 32-bit result,
even for signed operations. ADD, SUB, and MUL wrap modulo 2^32. Signed quotient
rounds toward zero; a nonzero signed remainder has the dividend's sign. Divide
by zero returns 0xffffffff for the quotient and the dividend for the remainder.
Signed INT_MIN / -1 returns INT_MIN with remainder zero. Comparisons return
integer 0 or 1, and all shifts mask the right operand with 31. Scalar ALU
sources are read before the destination is written, so source/destination
aliasing is allowed.

SP is an occupied-slot count, reset to zero. Slot i is lane i % (BUS_WIDTH/32)
of register i / (BUS_WIDTH/32), starting at the least significant lane. CALL
writes only that lane, increments SP, and branches. RET decrements SP and reads
the corresponding lane. CALL saves the next instruction address rounded up to
a bus-word boundary; pad the skipped bytes. Direct targets use word addresses;
indirect targets and stored return addresses use aligned byte addresses.
Underflow, overflow, and misaligned targets fault. STACKLIMIT accepts
1..N*(BUS_WIDTH/32), only with SP zero; reset permits the full capacity.
There are no push/pop instructions, automatic register saves, or data stack.
Software must preserve occupied return lanes and any caller values it needs.
See [compiler ABI](../compiler/README.md) for the implemented convention.

## Optional SIMD32 extension

Configure with `cmake -S . -B build/simd -DEC_SIMD=ON`. It defines `EC_SIMD`
for every core, the assembler, compiler, and both simulation flows. The default
is OFF: the SIMD decode, arithmetic, opcode names, and compiler intrinsics are
excluded by `#ifdef EC_SIMD`. These opcodes fault in a disabled core. Scalar
programs have the same encoding and behavior in either configuration.

Each register contains BUS_WIDTH/32 independent lanes: lane i occupies bits
32*i+31:32*i, with lane zero in the least significant bits. Every SIMD
instruction computes all lanes in the execute stage and replaces the entire
destination register. Sources are read before the destination is changed;
`d == a`, `d == b`, and `d == a == b` are legal. No masks, saturation, lane
crossing, flags, or implicit memory accesses are introduced.

| Opcode (hex) | Mnemonic | Bytes / fields | Per-lane operation |
|---|---|---|---|
| 80 | VADD32 d,a,b | 4: op,d,a,b | a + b modulo 2^32 |
| 81 | VSUB32 d,a,b | 4: op,d,a,b | a - b modulo 2^32 |
| 82 | VAND32 d,a,b | 4: op,d,a,b | a AND b |
| 83 | VOR32 d,a,b | 4: op,d,a,b | a OR b |
| 84 | VXOR32 d,a,b | 4: op,d,a,b | a XOR b |
| 85 | VSHL32 d,a,b | 4: op,d,a,b | Logical left shift by b & 31 |
| 86 | VSHR32 d,a,b | 4: op,d,a,b | Logical right shift by b & 31 |
| 87 | VSLTU32 d,a,b | 4: op,d,a,b | Unsigned a < b, producing 0 or 1 |
| 88 | VSAR32 d,a,b | 4: op,d,a,b | Arithmetic right shift by b & 31 |
| 89 | VMUL32 d,a,b | 4: op,d,a,b | Low 32 bits of a * b |
| 8A | VSLT32 d,a,b | 4: op,d,a,b | Signed a < b, producing 0 or 1 |
| 8B | VSPLAT32 d,a | 3: op,d,a | Copy a[31:0] into every destination lane |

Comparisons produce integer 1, not an all-ones mask. Add/subtract/multiply
never carry into neighboring lanes. Shifts use each corresponding right-hand
lane, not a single scalar shift count. Signed values use two's complement.
Use full-width LD/ST for data and BARRIER when a read depends on queued stores.
Scalar arithmetic still clears all destination bits above bit 31, including
a destination previously written by SIMD. SIMD writes also overwrite return
address lanes if software selects a register occupied by the return stack.

C++ intrinsics are in [compiler/simd/simd.h](../compiler/simd/simd.h); see
[their contract](../compiler/simd/README.md). LLVM automatic vectorization is
still disabled. Opcodes 8C–BF remain reserved; floating point, other lane widths,
SIMD division, reductions, and saturation are not implemented.

For a nonzero word count, this adds the same 32-bit bias to every input lane:

```text
LI r0, source_byte_address
LI r1, destination_byte_address
LI r2, word_count
LI r5, bias
VSPLAT32 r5, r5
LOOP
    LD r3, r0
    VADD32 r4, r3, r5
    ST r4, r1
    NEXTSRC
    NEXTDST
END
BARRIER
HALT
```

Both buffers must be word-aligned and nonoverlapping. A zero count must branch
around the loop. This uses ordinary serial instruction execution: each SIMD
ALU instruction computes all lanes together, but the complete load/compute/store
loop does not have COPY's one-word-per-clock streaming behavior. SIMD does not
add a pipeline stage, forwarding, or implicit memory synchronization. The C++
intrinsics add barriers around their full-word memory accesses; the SIMD
instructions themselves operate only on registers.

## Task instructions

See [the task engine](tasks_engine.md) for exact state/status values, publication
barriers, cancellation, slot reuse, and operand aliasing rules. Task entries are
aligned byte addresses. TFINISH/TABORT terminate a dispatched task independently
of its local CALL/RET stack. Boot code uses HALT; TID outside a task returns
0xffffffff. Task operands use their low 32 bits; results, statuses, and states
written to registers are zero-extended to BUS_WIDTH.

TISSUE returns command status in its former task-ID register after reading all
three operands. TDISARM also returns status. TREAD is nonblocking: it returns
result in r[d] and status in r[b], requiring `d != b`; the task-ID register may
alias either output. On INVALID or NOT_READY, its result output is zero. Use
TSTATE to distinguish COMPLETE, FAILED, and CANCELLED results. An invalid ID
passed to TSTATE returns 0xffffffff.

Command statuses are OK=0, INVALID=1, LOCKED=2, NOT_READY=3, NOT_OWNER=4. Slot
states are EMPTY=0, WAITING=1, RESERVED=2, RUNNING=3, COMPLETE=4, FAILED=5,
CANCELLED=6. Task IDs are 0..31; predecessor mask bit i names task slot i.

TISSUE, TFINISH, and TABORT drain prior local memory operations and invalidate
instruction buffers before publishing. TFINISH is normal completion even when
its result represents an application error. TABORT explicitly prevents dependent
continuations by propagating cancellation. TFINISH/TABORT have no continuation;
a failed ownership check faults the core. HALT inside an owned task is an
abnormal task exit. RET returns from an ordinary function, not from a task.
