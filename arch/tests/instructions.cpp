#include "../instruction.h"
#include <iostream>
#include <stdexcept>
using namespace extreme;

void check(bool ok) {
    if (!ok) {
        throw std::runtime_error("encoding regression");
    }
}

int main() {
    for (unsigned bits : {64u, 128u, 256u, 512u}) {
        Assembler p(bits);
        p.li(0, 0x12345678);
        p.li(1, 0x87654321);
        p.li(2, 17);
        p.emit(Opcode::Loop);
        p.emit(Opcode::Load, 3, 0);
        p.emit(Opcode::Store, 3, 1);
        p.emit(Opcode::NextSource);
        p.emit(Opcode::NextDestination);
        p.emit(Opcode::End);
        p.emit(Opcode::Copy);
        p.emit(Opcode::Add, 3, 1, 2);
        p.emit(Opcode::Barrier);
        p.emit(Opcode::Halt);
        p.align();
        unsigned decoded = 0;
        for (unsigned pos = 0; pos < p.code.size();) {
            unsigned n = length(p.code[pos]);
            check(n && pos % (bits / 8) + n <= bits / 8);
            pos += n;
            ++decoded;
        }
        check(decoded >= 13);
        check(p.code[0] == 0x10 && p.code[1] == 0 && p.code[2] == 0x78 && p.code[5] == 0x12);
        for (unsigned op = 0; op <= 8; ++op) {
            check(length(op) == 1);
        }
        check(length(0xff) == 0);
        Assembler extended(bits);
        extended.emit(Opcode::LoadScalar, 3, 4, uint8_t(MemoryFlag::HalfUnsigned));
        check(extended.code == std::vector<uint8_t>({0x32, 3, 4, 5}));
        extended.align();
        auto offset = extended.code.size();
        extended.emit(Opcode::Call, 0, 0, 0, 0x12345678);
        check(extended.code[offset] == 0x42 && extended.code[offset + 1] == 0x78 &&
              extended.code[offset + 4] == 0x12);
        extended.align();
        offset = extended.code.size();
        extended.emit(Opcode::CallRegister, 7);
        check(extended.code[offset] == 0x44 && extended.code[offset + 1] == 7);
        for (auto opcode : {Opcode::ReadStackPointer, Opcode::WriteStackPointer}) {
            Assembler context(bits);
            context.emit(opcode, 3);
            check(length(uint8_t(opcode)) == 2);
            check(context.code == std::vector<uint8_t>({uint8_t(opcode), 3}));
        }
        Assembler task(bits);
        task.emit(Opcode::TaskIssue, 2, 3, 4);
        check(task.code == std::vector<uint8_t>({0x50, 2, 3, 4}));
        task.align();
        offset = task.code.size();
        task.emit(Opcode::TaskRead, 2, 3, 4);
        check(task.code[offset] == 0x52 && task.code[offset + 3] == 4);
        check(length(0x51) == 3 && length(0x53) == 2 && length(0x54) == 2 && length(0x55) == 3 &&
              length(0x56) == 2);
#ifdef EC_SIMD
        for (unsigned op = 0x80; op <= 0x8f; ++op) {
            Assembler simd(bits);
            // Exercise padding when the instruction cannot fit at the end of a word.
            simd.code.resize(bits / 8 - 1, 0);
            simd.emit(Opcode(op), 2, 3, 4);
            unsigned start = bits / 8;
            check(simd.code[start] == op && simd.code[start + 1] == 2 && simd.code[start + 2] == 3);
            check(length(op) == (op == 0x8b || op >= 0x8d ? 3 : 4));
            if (op != 0x8b && op < 0x8d) {
                check(simd.code[start + 3] == 4);
            }
        }
#else
        for (unsigned op = 0x80; op <= 0x8f; ++op) {
            check(length(op) == 0);
        }
#endif
        check(length(0x90) == 0);
        Assembler branch(bits);
        branch.emit(Opcode::Jump, 0, 0, 0, 0x12345678);
        check(branch.code[1] == 0x78 && branch.code[4] == 0x12);
    }
    std::cout << "Instruction encodings PASS\n";
}
