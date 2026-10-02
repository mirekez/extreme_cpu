# Instruction set, revision 0

Instructions occupy 1–6 bytes and never cross a BUS_WIDTH word boundary. Pad
with `NOP` as needed. All immediate and word-target fields are little-endian.
`arch/instruction.h` contains opcode values, operand descriptions, instruction
lengths, and the assembler helper; host structure packing is not the encoding.

Registers `r0..r(N-1)` are BUS_WIDTH bits. All are writable. Scalar arithmetic
reads bits 31:0, computes modulo 2^32, and zero-extends its result. `MOV`, `LD`,
and `ST` transfer whole registers. Addresses use bits 31:0 of their register.
Shift counts use five low bits. There are no implicit flags or split immediates.

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
| 2A–2D | DIVU, DIVS, REMU, REMS | 4: op,d,a,b | Unsigned/signed quotient or remainder |
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
The count uses bits 31:0 of r2. For a nonzero count, on completion of enqueueing, r0/r1 advance by the original count * word_bytes
and r2 becomes zero. It leaves other architectural registers unchanged. A zero
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

## Scalar access flags and calls

LDS flags follow RISC-V funct3: 0 = signed byte, 1 = signed halfword,
2 = word, 4 = unsigned byte, 5 = unsigned halfword. STS accepts 0, 1, 2.
Other flags fault. Halfwords/words require natural alignment. Loads extend to
32 bits, then zero all higher register bits; signed byte/halfword loads sign
extend only within those low 32 bits. Stores preserve unselected memory bytes
using controller byte enables. Every bus transaction remains BUS_WIDTH wide.
Divide by zero returns all ones for quotient and the dividend for remainder;
signed INT_MIN / -1 returns INT_MIN with remainder zero.

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

## Generic programming and future SIMD

The freestanding compiler supports an initial C++ subset using this ISA.
Existing machine-code binaries are not compatible.

Opcodes 80–BF are reserved for future explicit SIMD operations over full-width
registers. No SIMD opcode is currently executable; it faults like other unknown
encodings. Lane size, saturation, masking, and reduction semantics remain to be
specified. Full-width register storage and copy-stage payloads are already in
place for that extension.

## Task instructions

See [the task engine](tasks_engine.md) for exact state/status values, publication
barriers, cancellation, slot reuse, and operand aliasing rules. Task entries are
aligned byte addresses. TFINISH/TABORT terminate a dispatched task independently
of its local CALL/RET stack. Boot code uses HALT; TID outside a task returns
0xffffffff. TISSUE and completion drain prior memory operations before publishing.
