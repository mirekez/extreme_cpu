#pragma once
#include <cstdint>

namespace extreme {
// Byte-oriented ISA; see doc/instructions.md for the complete software contract.
// d/a/b are one-byte indices into N = EC_REGS registers. Immediates are little-endian.
// Instructions must fit inside one BUS_WIDTH word; byte/word addresses are distinguished below.
enum class Opcode : uint8_t {
    // NOP (1 byte): advance to the next instruction without changing registers.
    Nop = 0x00,
    // HALT (1 byte): drain local memory operations, invalidate fetch buffers, and stop.
    // HALT inside an owned task is an abnormal exit; use TFINISH/TABORT to publish a result.
    Halt = 0x01,
    // BARRIER (1 byte): drain local memory operations and invalidate fetch buffers; resume.
    Barrier = 0x02,
    // COPY (1 byte): stream r2 full words from r0 to r1; advance r0/r1 and clear r2.
    // A zero low32(r2) performs no memory operation and preserves every register.
    Copy = 0x03,
    // RET (1 byte): decrement SP and jump to the aligned byte PC in that return lane.
    Return = 0x08,
    // LOOP (1 byte): save the next byte PC; requires low32(r2) > 0 and no active loop.
    Loop = 0x04,
    // END (1 byte): decrement low32(r2); repeat the active loop until it reaches zero.
    End = 0x05,
    // NEXTSRC (1 byte): r0 = zero_extend(low32(r0) + BUS_WIDTH/8).
    NextSource = 0x06,
    // NEXTDST (1 byte): r1 = zero_extend(low32(r1) + BUS_WIDTH/8).
    NextDestination = 0x07,
    // LI d,imm32 (6 bytes): zero-extend the little-endian 32-bit immediate into r[d].
    Immediate = 0x10,
    // MOV d,a (3 bytes): copy all BUS_WIDTH bits of r[a] to r[d].
    Move = 0x11,
    // Scalar ALU instructions read low 32-bit operands and zero-extend results to BUS_WIDTH.
    // ADD d,a,b (4 bytes): add low 32-bit operands modulo 2^32.
    Add = 0x20,
    // SUB d,a,b (4 bytes): subtract low32(r[b]) from low32(r[a]) modulo 2^32.
    Sub = 0x21,
    // AND d,a,b (4 bytes): bitwise AND of the low 32-bit operands.
    And = 0x22,
    // OR d,a,b (4 bytes): bitwise OR of the low 32-bit operands.
    Or = 0x23,
    // XOR d,a,b (4 bytes): bitwise XOR of the low 32-bit operands.
    Xor = 0x24,
    // SHL d,a,b (4 bytes): logical left shift of low32(r[a]) by low32(r[b]) & 31.
    ShiftLeft = 0x25,
    // SHR d,a,b (4 bytes): logical right shift of low32(r[a]) by low32(r[b]) & 31.
    ShiftRight = 0x26,
    // SLTU d,a,b (4 bytes): 1 if unsigned low32(r[a]) < low32(r[b]), otherwise 0.
    LessUnsigned = 0x27,
    // SAR d,a,b (4 bytes): signed right shift of low32(r[a]) by low32(r[b]) & 31.
    ShiftArithmetic = 0x28,
    // MUL d,a,b (4 bytes): low 32 bits of the product of the low 32-bit operands.
    Multiply = 0x29,
    // DIVU d,a,b (4 bytes): unsigned 32-bit quotient; zero divisor returns 0xffffffff.
    DivideUnsigned = 0x2a,
    // DIVS d,a,b (4 bytes): signed quotient toward zero; zero gives -1, INT_MIN/-1 wraps.
    DivideSigned = 0x2b,
    // REMU d,a,b (4 bytes): unsigned 32-bit remainder; zero divisor returns the dividend.
    RemainderUnsigned = 0x2c,
    // REMS d,a,b (4 bytes): signed remainder; zero divisor returns a, INT_MIN%-1 gives 0.
    RemainderSigned = 0x2d,
    // SLT d,a,b (4 bytes): 1 if signed low32(r[a]) < low32(r[b]), otherwise 0.
    LessSigned = 0x2e,
    // LD d,a (3 bytes): load one full aligned bus word at byte address low32(r[a]).
    Load = 0x30,
    // ST d,a (3 bytes): enqueue all of r[d] at aligned byte address low32(r[a]).
    Store = 0x31,
    // LDS d,a,flags (4 bytes): load/extend a byte, halfword, or word; see MemoryFlag.
    LoadScalar = 0x32,
    // STS d,a,flags (4 bytes): enqueue low bytes of r[d] with byte enables; flags 0..2.
    StoreScalar = 0x33,
    // JMP word32 (5 bytes): jump to byte PC = word32 * (BUS_WIDTH/8), modulo 2^32.
    Jump = 0x40,
    // BZ d,word32 (6 bytes): jump to the word target when low32(r[d]) == 0.
    BranchZero = 0x41,
    // CALL word32 (5 bytes): save bus-aligned continuation in a return lane; jump.
    Call = 0x42,
    // JMPR a (2 bytes): jump to the aligned byte PC in low32(r[a]); encoded as op,a.
    JumpRegister = 0x43,
    // CALLR a (2 bytes): save aligned continuation; jump to aligned low32(r[a]).
    CallRegister = 0x44,
    // STACKLIMIT imm32 (5 bytes): with SP zero, set return capacity to 1..N*(BUS_WIDTH/32).
    StackLimit = 0x45,
    // GETSP d (2 bytes): zero-extend the occupied return-lane count into r[d].
    ReadStackPointer = 0x46,
    // SETSP a (2 bytes): set SP from low32(r[a]); values beyond STACKLIMIT fault.
    // Does not change return-lane contents or memory. Software restores them explicitly.
    WriteStackPointer = 0x47,
    // Task operands use low 32 bits; result/status/state writes clear higher register bits.
    // TISSUE d,a,b (4 bytes): drain, issue ID r[d]/entry r[a]/mask r[b]; status to r[d].
    TaskIssue = 0x50,
    // TDISARM d,a (3 bytes): cancel or clear task ID r[a]; command status to r[d].
    TaskDisarm = 0x51,
    // TREAD d,a,b (4 bytes): nonblocking result to r[d], status to r[b], ID r[a]; d != b.
    TaskRead = 0x52,
    // TFINISH d (2 bytes): drain, publish r[d], and stop; release eligible dependents.
    TaskFinish = 0x53,
    // TABORT d (2 bytes): drain, publish error r[d], stop, and cancel dependent work.
    TaskAbort = 0x54,
    // TSTATE d,a (3 bytes): task ID r[a] state to r[d]; invalid ID returns 0xffffffff.
    TaskStatus = 0x55,
    // TID d (2 bytes): current task ID to r[d]; boot code receives 0xffffffff.
    TaskId = 0x56,
// Disabled builds exclude these opcode names and fault if their encodings are executed.
#ifdef EC_SIMD
    // SIMD32 replaces the entire destination with BUS_WIDTH/32 independent 32-bit lanes.
    // Lane i occupies bits [32*i+31:32*i]. Carry masks and pair moves are explicit.
    // Sources may alias d; ordinary lane arithmetic never crosses a lane boundary.
    // VADD32 d,a,b (4 bytes): independent lane additions modulo 2^32.
    SimdAdd32 = 0x80,
    // VSUB32 d,a,b (4 bytes): independent lane subtractions a - b modulo 2^32.
    SimdSub32 = 0x81,
    // VAND32 d,a,b (4 bytes): bitwise AND in every lane.
    SimdAnd32 = 0x82,
    // VOR32 d,a,b (4 bytes): bitwise OR in every lane.
    SimdOr32 = 0x83,
    // VXOR32 d,a,b (4 bytes): bitwise XOR in every lane.
    SimdXor32 = 0x84,
    // VSHL32 d,a,b (4 bytes): each lane a shifts left by its corresponding b & 31.
    SimdShiftLeft32 = 0x85,
    // VSHR32 d,a,b (4 bytes): each lane a shifts right logically by its b & 31.
    SimdShiftRight32 = 0x86,
    // VSLTU32 d,a,b (4 bytes): each lane becomes unsigned a < b ? 1 : 0.
    SimdLessUnsigned32 = 0x87,
    // VSAR32 d,a,b (4 bytes): each signed lane a shifts right by its b & 31.
    SimdShiftArithmetic32 = 0x88,
    // VMUL32 d,a,b (4 bytes): each lane gets the low 32 bits of its product.
    SimdMultiply32 = 0x89,
    // VSLT32 d,a,b (4 bytes): each lane becomes signed a < b ? 1 : 0.
    SimdLessSigned32 = 0x8a,
    // VSPLAT32 d,a (3 bytes): replicate low32(r[a]) into every destination lane.
    SimdSplat32 = 0x8b,
    // VADDC32 d,a,b (4 bytes): each lane is the unsigned carry-out of a + b (0 or 1).
    // Does not write the sum or any hidden flags. Use VPAIRUP32 to move low-word carries.
    SimdAddCarry32 = 0x8c,
    // VPAIRSWAP32 d,a (3 bytes): swap the two 32-bit lanes within every adjacent pair.
    SimdPairSwap32 = 0x8d,
    // VPAIRUP32 d,a (3 bytes): each [low,high] pair becomes [0,low], a fixed 32-bit move.
    SimdPairUp32 = 0x8e,
    // VPAIRDOWN32 d,a (3 bytes): each [low,high] pair becomes [high,0].
    SimdPairDown32 = 0x8f,
#endif
};

// Encoded byte count, or zero for an unknown/disabled opcode. Does not validate operands.
constexpr unsigned length(uint8_t op) {
    if (op <= 8) {
        return 1;
    }
    if (op == 0x50 || op == 0x52) {
        return 4;
    }
    if (op == 0x51 || op == 0x55) {
        return 3;
    }
    if (op == 0x53 || op == 0x54 || op == 0x56) {
        return 2;
    }
    if (op == 0x10 || op == 0x41) {
        return 6;
    }
    if (op == 0x11 || op == 0x30 || op == 0x31) {
        return 3;
    }
    if (op >= 0x20 && op <= 0x2e) {
        return 4;
    }
    if (op == 0x32 || op == 0x33) {
        return 4;
    }
    if (op == 0x43 || op == 0x44 || op == 0x46 || op == 0x47) {
        return 2;
    }
    if (op == 0x40 || op == 0x42 || op == 0x45) {
        return 5;
    }
#ifdef EC_SIMD
    if ((op >= 0x80 && op <= 0x8a) || op == 0x8c) {
        return 4;
    }
    if (op == 0x8b || (op >= 0x8d && op <= 0x8f)) {
        return 3;
    }
#endif
    return 0;
}

// Encoding is byte-oriented; these describe fields, not host struct layout.
struct RegisterOperands {
    uint8_t destination, left, right;
};

struct ImmediateOperands {
    uint8_t destination;
    uint32_t value;
};
// RISC-V-style scalar access flags. Halfwords/words require natural alignment.
// Loads extend to 32 bits and then clear higher register bits; stores accept only 0, 1, 2.
enum class MemoryFlag : uint8_t {
    Byte = 0,         // Load a signed byte, or store the low byte.
    Half = 1,         // Load a signed halfword, or store the low two bytes.
    Word = 2,         // Load/store the low four bytes.
    ByteUnsigned = 4, // Load a zero-extended byte.
    HalfUnsigned = 5  // Load a zero-extended halfword.
};
enum class TaskState : uint32_t { Empty, Waiting, Reserved, Running, Complete, Failed, Cancelled };
enum class TaskStatus : uint32_t { Ok, Invalid, Locked, NotReady, NotOwner };

// Absolute instruction-word index; byte PC = word * (BUS_WIDTH/8), modulo 2^32.
struct WordTarget {
    uint32_t word;
};
} // namespace extreme
#ifndef SYNTHESIS
#include <vector>
#include <stdexcept>

namespace extreme {
class Assembler {
    unsigned bytes;

public:
    std::vector<uint8_t> code;

    explicit Assembler(unsigned bus_bits) : bytes(bus_bits / 8) {
        if (bus_bits < 64 || bus_bits > 512 || (bus_bits & (bus_bits - 1))) {
            throw std::invalid_argument("bus width");
        }
    }

    // Append NOP bytes up to the next instruction-word boundary.
    void align() {
        while (code.size() % bytes) {
            code.push_back(0);
        }
    }

    // Pad automatically if the instruction cannot fit in the current word.
    // Call align() after CALL/CALLR so the following instruction is at the saved return PC.
    // JMP/CALL/STACKLIMIT use imm; LI/BZ use d and imm. Two-byte instructions use d.
    // LDS/STS use b for MemoryFlag. Register ranges and target alignment are checked by hardware.
    void emit(Opcode op, uint8_t d = 0, uint8_t a = 0, uint8_t b = 0, uint32_t imm = 0) {
        unsigned n = length(uint8_t(op));
        if (!n) {
            throw std::invalid_argument("opcode");
        }
        if (code.size() % bytes + n > bytes) {
            align();
        }
        code.push_back(uint8_t(op));
        if (op == Opcode::Jump || op == Opcode::Call || op == Opcode::StackLimit) {
            for (unsigned i = 0; i < 4; ++i) {
                code.push_back(imm >> (8 * i));
            }
        } else if (n == 6) {
            code.push_back(d);
            for (unsigned i = 0; i < 4; ++i) {
                code.push_back(imm >> (8 * i));
            }
        } else if (n == 2) {
            code.push_back(d);
        } else {
            if (n >= 3) {
                code.push_back(d);
                code.push_back(a);
            }
            if (n == 4) {
                code.push_back(b);
            }
        }
    }

    void li(uint8_t d, uint32_t value) {
        emit(Opcode::Immediate, d, 0, 0, value);
    }
};
} // namespace extreme
#endif
