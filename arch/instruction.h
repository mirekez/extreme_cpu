#pragma once
#include <cstdint>
namespace extreme {
enum class Opcode : uint8_t {
    Nop=0x00, Halt=0x01, Barrier=0x02, Copy=0x03,
    Return=0x08, Loop=0x04, End=0x05, NextSource=0x06, NextDestination=0x07,
    Immediate=0x10, Move=0x11,
    Add=0x20, Sub=0x21, And=0x22, Or=0x23, Xor=0x24,
    ShiftLeft=0x25, ShiftRight=0x26, LessUnsigned=0x27,
    ShiftArithmetic=0x28, Multiply=0x29, DivideUnsigned=0x2a, DivideSigned=0x2b,
    RemainderUnsigned=0x2c, RemainderSigned=0x2d, LessSigned=0x2e,
    Load=0x30, Store=0x31, LoadScalar=0x32, StoreScalar=0x33,
    Jump=0x40, BranchZero=0x41, Call=0x42, JumpRegister=0x43, CallRegister=0x44, StackLimit=0x45,
    TaskIssue=0x50, TaskDisarm=0x51, TaskRead=0x52, TaskFinish=0x53, TaskAbort=0x54, TaskStatus=0x55, TaskId=0x56
};
constexpr unsigned length(uint8_t op) {
    if (op<=8) return 1;
    if (op==0x50 || op==0x52) return 4;
    if (op==0x51 || op==0x55) return 3;
    if (op==0x53 || op==0x54 || op==0x56) return 2;
    if (op==0x10 || op==0x41) return 6;
    if (op==0x11 || op==0x30 || op==0x31) return 3;
    if (op>=0x20 && op<=0x2e) return 4;
    if (op==0x32 || op==0x33) return 4;
    if (op==0x43 || op==0x44) return 2;
    if (op==0x40 || op==0x42 || op==0x45) return 5;
    return 0;
}
// Encoding is byte-oriented; these describe fields, not host struct layout.
struct RegisterOperands { uint8_t destination, left, right; };
struct ImmediateOperands { uint8_t destination; uint32_t value; };
enum class MemoryFlag : uint8_t { Byte=0, Half=1, Word=2, ByteUnsigned=4, HalfUnsigned=5 };
enum class TaskState : uint32_t { Empty, Waiting, Reserved, Running, Complete, Failed, Cancelled };
enum class TaskStatus : uint32_t { Ok, Invalid, Locked, NotReady, NotOwner };
struct WordTarget { uint32_t word; };
}
#ifndef SYNTHESIS
#include <vector>
#include <stdexcept>
namespace extreme {
class Assembler {
    unsigned bytes;
public:
    std::vector<uint8_t> code;
    explicit Assembler(unsigned bus_bits) : bytes(bus_bits/8) {
        if (bus_bits<64 || bus_bits>512 || (bus_bits&(bus_bits-1))) throw std::invalid_argument("bus width");
    }
    void align() { while(code.size()%bytes) code.push_back(0); }
    void emit(Opcode op, uint8_t d=0, uint8_t a=0, uint8_t b=0, uint32_t imm=0) {
        unsigned n=length(uint8_t(op));
        if (!n) throw std::invalid_argument("opcode");
        if (code.size()%bytes+n>bytes) align();
        code.push_back(uint8_t(op));
        if (op==Opcode::Jump || op==Opcode::Call || op==Opcode::StackLimit) { for(unsigned i=0;i<4;++i) code.push_back(imm>>(8*i)); }
        else if(n==6) { code.push_back(d); for(unsigned i=0;i<4;++i) code.push_back(imm>>(8*i)); }
        else if(n==2) code.push_back(d);
        else { if(n>=3) {code.push_back(d);code.push_back(a);} if(n==4) code.push_back(b); }
    }
    void li(uint8_t d,uint32_t value) { emit(Opcode::Immediate,d,0,0,value); }
};
}
#endif
