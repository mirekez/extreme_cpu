// LLVM IR -> Extreme bytecode. The frontend supplies an explicit ILP32 ABI.
// SSA values use compiler-managed storage; return addresses remain in registers.
#include "../arch/instruction.h"
#include "Integer64.h"
#include "Libc.h"
#include "Overflow.h"
#include "SoftFloat.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace llvm;
using extreme::Assembler;
using extreme::Opcode;

struct Backend {
    Module& module;
    const DataLayout& dl;
    unsigned bits, bytes, regs, banks, words;
    uint32_t dataBase, cursor, mailbox, resultAddress, idleAddress;
    unsigned mailboxWords = 8, mailboxBytes = 64;
    uint32_t userDataBegin = 0, userDataEnd = 0, userHeapEnd = 0;
    std::map<Function*, uint32_t> varargSlots;
    static constexpr unsigned dynamicLocalBytes = 65536;
    std::map<Function*, uint32_t> dynamicAreas, dynamicCursors;
    Assembler out;
    std::vector<uint8_t> image;
    std::map<const Value*, uint32_t> slots, objects, phiTemps;
    std::map<const BasicBlock*, std::string> labels;
    std::map<std::string, uint32_t> positions;

    struct Fixup {
        unsigned offset;
        std::string label;
        bool byteAddress = false;
    };

    std::vector<Fixup> fixups;
    std::vector<Fixup> dataFixups;
    std::vector<Function*> functions;
    std::set<Function*> visiting, visited;
    std::map<Function*, unsigned> depths;
    std::map<Function*, std::set<Function*>> callGraph;
    std::map<Function*, uint32_t> activationBytes;
    std::map<std::pair<Function*, unsigned>, uint64_t> activationBounds;
    Function* entryFunction = nullptr;
    std::set<Function*> taskRoots, ordinaryCallees;
    std::vector<Function*> taskEntries;
    bool taskMode = false, frameLayout = false;
    bool reentrant = false;
    uint32_t frameCursor = 0, frameBase = 0, frameStride = 0, returnSave = 0;
    uint32_t contextStride = 0, bootFrameBase = 0, frameAlignment = 0;
    unsigned serial = 0, copyCount = 0;

    Backend(Module& m, unsigned b, unsigned r, unsigned n, unsigned w)
        : module(m), dl(m.getDataLayout()), bits(b), bytes(b / 8), regs(r), banks(n), words(w),
          out(b) {
        if (n < 1 || n > 16 || w < 1 || uint64_t(bytes) * w * n > 0xffffffffULL) {
            fail("invalid memory configuration");
        }
        if (r < 8 || r > 16) {
            fail("compiler ABI requires 8..16 registers");
        }
        if (!dl.isLittleEndian() || dl.getPointerSizeInBits() != 32) {
            fail("input must use a little-endian ILP32 target, not host LLVM IR");
        }
        image.resize(uint64_t(bytes) * words * banks);
        dataBase = uint32_t(image.size() / 2);
        cursor = dataBase;
        for (auto& function : module) {
            for (auto& block : function) {
                for (auto& instruction : block) {
                    if (auto* call = dyn_cast<CallBase>(&instruction)) {
                        if (auto* target = call->getCalledFunction();
                            target && target->isIntrinsic()) {
                            continue;
                        }
                        unsigned words = 0, index = 0;
                        for (auto& argument : call->args()) {
                            unsigned count = valueWords(argument->getType());
                            if (index++ >= call->getFunctionType()->getNumParams() && count == 2) {
                                words = (words + 1) & ~1u;
                            }
                            words += count;
                        }
                        mailboxWords = std::max(mailboxWords, words);
                    }
                }
            }
        }
        mailboxBytes = (mailboxWords * 4 + 8 + bytes - 1) / bytes * bytes;
        mailboxBytes = std::max(mailboxBytes, 64u);
        mailbox = allocate(mailboxBytes, bytes);
        resultAddress = mailbox + mailboxWords * 4;
    }

    [[noreturn]] static void fail(const std::string& s) {
        throw std::runtime_error(s);
    }

    std::string unique(const std::string& stem) {
        return stem + "." + std::to_string(serial++);
    }

    std::string functionLabel(Function* f) {
        return "function." + f->getName().str();
    }

    unsigned width(Type* t) {
        if (t->isDoubleTy()) {
            return 64;
        }
        if (t->isPointerTy() || t->isFloatTy()) {
            return 32;
        }
        // Clang's ILP32 ABI coerces small struct returns/arguments into word arrays.
        if (auto* array = dyn_cast<ArrayType>(t);
            array && array->getElementType()->isIntegerTy(32) && array->getNumElements() >= 1 &&
            array->getNumElements() <= 2) {
            return array->getNumElements() * 32;
        }
        if (t->isIntegerTy() && (t->getIntegerBitWidth() <= 32 || t->isIntegerTy(64))) {
            return t->getIntegerBitWidth();
        }
        std::string typeName;
        raw_string_ostream stream(typeName);
        t->print(stream);
        fail("unsupported value type: " + typeName);
    }

    unsigned valueWords(Type* type) {
        return (width(type) + 31) / 32;
    }

    uint32_t allocate(uint64_t n, unsigned align = 4) {
        auto& end = frameLayout ? frameCursor : cursor;
        uint64_t start = (uint64_t(end) + align - 1) / align * align;
        if (start + n > image.size()) {
            fail("static data exceeds configured memory");
        }
        end = uint32_t(start + n);
        return uint32_t(start);
    }

    void frameAddress(unsigned r, uint32_t offset) {
        immediate(r, offset);
        if (taskMode || reentrant) {
            alu(Opcode::Add, r, r, 7);
        }
    }

    uint32_t returnOffset() {
        return mailbox + mailboxWords * 4;
    }

    void taskFramePrologue() {
        bool needed = false;
        for (auto& bb : *currentFunction) {
            for (auto& inst : bb) {
                needed |= !inst.getType()->isVoidTy();
                if (auto* call = dyn_cast<CallBase>(&inst)) {
                    needed |= !builtin(call->getCalledFunction());
                }
            }
        }
        if (!needed) {
            return;
        }
        out.emit(Opcode::TaskId, 7);
        unsigned shift = 0;
        for (unsigned n = contextStride; n > 1; n >>= 1) {
            ++shift;
        }
        immediate(6, shift);
        alu(Opcode::ShiftLeft, 7, 7, 6);
        immediate(6, frameBase);
        alu(Opcode::Add, 7, 7, 6);
        if (reentrant || depths.at(currentFunction) > 1) {
            out.emit(Opcode::StackLimit, 0, 0, 0, 2 * (bits / 32));
        }
    }

    Function* currentFunction = nullptr;

    bool layoutBuiltin(Function* f) {
        return f && (f->getName() == "__extreme_user_layout" ||
                     f->getName() == "__extreme_activation_size" ||
                     f->getName() == "__extreme_activation_end");
    }

    void emitLayout(CallBase& call) {
        if (call.getCalledFunction()->getName() == "__extreme_activation_end") {
            if (!reentrant || !call.arg_empty() || !call.getType()->isIntegerTy(32)) {
                fail("activation end requires --reentrant and no arguments");
            }
            frameAddress(2, activationBytes.at(currentFunction));
            write(&call);
            return;
        }
        if (call.arg_size() != 1 || !call.getType()->isIntegerTy(32)) {
            fail("invalid layout intrinsic signature");
        }
        auto name = call.getCalledFunction()->getName();
        uint64_t value;
        if (name == "__extreme_user_layout") {
            auto* index = dyn_cast<ConstantInt>(call.getArgOperand(0));
            if (!index || index->getZExtValue() > 2) {
                fail("user layout index must be 0, 1, or 2");
            }
            value = index->isZero()       ? userDataBegin
                    : index->equalsInt(1) ? userDataEnd
                                          : userHeapEnd;
        } else {
            auto* target = dyn_cast<Function>(call.getArgOperand(0)->stripPointerCastsAndAliases());
            if (!reentrant || !target || !activationBytes.count(target)) {
                fail("activation size requires a defined function and --reentrant");
            }
            value = activationBound(target, 2 * (bits / 32));
        }
        if (value > 0xffffffffULL) {
            fail("activation size exceeds address space");
        }
        immediate(2, uint32_t(value));
        write(&call);
    }

    bool contextBuiltin(Function* f) {
        return f && f->getName().starts_with("__extreme_context_");
    }

    void emitContext(CallBase& call) {
        if (!reentrant) {
            fail("context operations require --reentrant");
        }
        auto name = call.getCalledFunction()->getName();
        if (name == "__extreme_context_enter") {
            if (!call.getType()->isVoidTy() || call.arg_size() != 2 ||
                !call.getArgOperand(0)->getType()->isPointerTy() ||
                !call.getArgOperand(1)->getType()->isPointerTy()) {
                fail("invalid context enter signature");
            }
            read(call.getArgOperand(0), 4);
            read(call.getArgOperand(1), 7);
            fence();
            immediate(2, 0);
            out.emit(Opcode::WriteStackPointer, 2);
            out.emit(Opcode::StackLimit, 0, 0, 0, 2 * (bits / 32));
            out.emit(Opcode::JumpRegister, 4);
            return;
        }
        bool save = name == "__extreme_context_save";
        if ((!save && name != "__extreme_context_restore") || call.arg_size() != (save ? 1 : 2) ||
            !call.getArgOperand(0)->getType()->isPointerTy() ||
            (save ? !call.getType()->isIntegerTy(32)
                  : (!call.getType()->isVoidTy() ||
                     !call.getArgOperand(1)->getType()->isIntegerTy(32)))) {
            fail("invalid context intrinsic signature");
        }
        read(call.getArgOperand(0), 6);
        // libc may place jmp_buf in ordinarily aligned malloc storage. Reserve
        // one extra bus word in the public buffer and align its payload here.
        immediate(5, bytes - 1);
        alu(Opcode::Add, 6, 6, 5);
        immediate(5, ~(bytes - 1));
        alu(Opcode::And, 6, 6, 5);
        auto address = [&](unsigned offset) {
            immediate(5, offset);
            alu(Opcode::Add, 5, 6, 5);
        };
        fence();
        if (save) {
            address(0);
            out.emit(Opcode::Store, 0, 5);
            address(bytes);
            out.emit(Opcode::Store, 1, 5);
            out.emit(Opcode::ReadStackPointer, 2);
            address(2 * bytes);
            scalarStore(2, 5);
            address(2 * bytes + 4);
            scalarStore(7, 5);
            std::string continuation = unique("context.resume");
            if (out.code.size() % bytes + 6 > bytes) {
                out.align();
            }
            unsigned offset = out.code.size();
            immediate(3, 0);
            fixups.push_back({offset + 2, continuation, true});
            address(2 * bytes + 8);
            scalarStore(3, 5);
            frameAddress(3, slots.at(&call));
            address(2 * bytes + 12);
            scalarStore(3, 5);
            immediate(2, 0);
            write(&call);
            mark(continuation);
        } else {
            read(call.getArgOperand(1), 2);
            auto zero = unique("context.zero"), nonzero = unique("context.nonzero");
            jump(zero, Opcode::BranchZero, 2);
            jump(nonzero);
            mark(zero);
            immediate(2, 1);
            mark(nonzero);
            address(2 * bytes + 12);
            scalarLoad(3, 5);
            scalarStore(2, 3);
            address(2 * bytes + 8);
            scalarLoad(4, 5);
            address(2 * bytes);
            scalarLoad(3, 5);
            address(2 * bytes + 4);
            scalarLoad(7, 5);
            address(0);
            out.emit(Opcode::Load, 0, 5);
            address(bytes);
            out.emit(Opcode::Load, 1, 5);
            out.emit(Opcode::WriteStackPointer, 3);
            out.emit(Opcode::JumpRegister, 4);
        }
    }

    bool taskBuiltin(Function* f) {
        return f && f->getName().starts_with("__extreme_task_");
    }

#ifdef EC_SIMD
    bool simdBuiltin(Function* f) {
        return f->getName().starts_with("__extreme_simd_");
    }

    void emitSimd(CallBase& call) {
        auto name = call.getCalledFunction()->getName();
        bool splat = name == "__extreme_simd_splat32";
        bool pairMove = name == "__extreme_simd_pair_swap32" ||
                        name == "__extreme_simd_pair_up32" || name == "__extreme_simd_pair_down32";
        bool pairAdd = name == "__extreme_simd_add64", pairSub = name == "__extreme_simd_sub64";
        bool unary = splat || pairMove;
        if (!call.getType()->isVoidTy() || call.arg_size() != (unary ? 2 : 3) ||
            !call.getArgOperand(0)->getType()->isPointerTy() ||
            (splat ? !call.getArgOperand(1)->getType()->isIntegerTy(32)
                   : (!call.getArgOperand(1)->getType()->isPointerTy() ||
                      (!pairMove && !call.getArgOperand(2)->getType()->isPointerTy())))) {
            fail("invalid SIMD intrinsic signature");
        }
        Opcode opcode;
        if (name == "__extreme_simd_add32" || pairAdd) {
            opcode = Opcode::SimdAdd32;
        } else if (name == "__extreme_simd_sub32" || pairSub) {
            opcode = Opcode::SimdSub32;
        } else if (name == "__extreme_simd_and32") {
            opcode = Opcode::SimdAnd32;
        } else if (name == "__extreme_simd_or32") {
            opcode = Opcode::SimdOr32;
        } else if (name == "__extreme_simd_xor32") {
            opcode = Opcode::SimdXor32;
        } else if (name == "__extreme_simd_shl32") {
            opcode = Opcode::SimdShiftLeft32;
        } else if (name == "__extreme_simd_shr32") {
            opcode = Opcode::SimdShiftRight32;
        } else if (name == "__extreme_simd_ltu32") {
            opcode = Opcode::SimdLessUnsigned32;
        } else if (name == "__extreme_simd_sar32") {
            opcode = Opcode::SimdShiftArithmetic32;
        } else if (name == "__extreme_simd_mul32") {
            opcode = Opcode::SimdMultiply32;
        } else if (name == "__extreme_simd_lts32") {
            opcode = Opcode::SimdLessSigned32;
        } else if (name == "__extreme_simd_splat32") {
            opcode = Opcode::SimdSplat32;
        } else if (name == "__extreme_simd_carry32") {
            opcode = Opcode::SimdAddCarry32;
        } else if (name == "__extreme_simd_pair_swap32") {
            opcode = Opcode::SimdPairSwap32;
        } else if (name == "__extreme_simd_pair_up32") {
            opcode = Opcode::SimdPairUp32;
        } else if (name == "__extreme_simd_pair_down32") {
            opcode = Opcode::SimdPairDown32;
        } else {
            fail("unknown SIMD intrinsic: " + name.str());
        }
        // r0/r1 hold returns and r7 holds the task frame base. Keep them intact.
        read(call.getArgOperand(0), 6);
        read(call.getArgOperand(1), 2);
        if (!unary) {
            read(call.getArgOperand(2), 3);
        }
        fence();
        if (!splat) {
            out.emit(Opcode::Load, 2, 2);
        }
        if (!unary) {
            out.emit(Opcode::Load, 3, 3);
        }
        if (pairAdd || pairSub) {
            out.emit(pairAdd ? Opcode::SimdAddCarry32 : Opcode::SimdLessUnsigned32, 5, 2, 3);
            out.emit(Opcode::SimdPairUp32, 5, 5);
        }
        out.emit(opcode, 4, 2, 3);
        if (pairAdd || pairSub) {
            out.emit(opcode, 4, 4, 5);
        }
        out.emit(Opcode::Store, 4, 6);
        fence();
    }

    bool emitSimdInteger64(BinaryOperator& value) {
        Opcode opcode;
        bool movePair = false;
        switch (value.getOpcode()) {
        case Instruction::Add:
            opcode = Opcode::SimdAdd32;
            break;
        case Instruction::Sub:
            opcode = Opcode::SimdSub32;
            break;
        case Instruction::And:
            opcode = Opcode::SimdAnd32;
            break;
        case Instruction::Or:
            opcode = Opcode::SimdOr32;
            break;
        case Instruction::Xor:
            opcode = Opcode::SimdXor32;
            break;
        case Instruction::Shl:
        case Instruction::LShr: {
            auto* count = dyn_cast<ConstantInt>(value.getOperand(1));
            if (!count || !count->equalsInt(32)) {
                return false;
            }
            opcode = value.getOpcode() == Instruction::Shl ? Opcode::SimdPairUp32
                                                           : Opcode::SimdPairDown32;
            movePair = true;
            break;
        }
        default:
            return false;
        }
        // Assemble the low/high scalar spills into one register pair. All other
        // lanes remain zero, and return registers r0/r1 and activation base r7 survive.
        read(value.getOperand(0), 2);
        read(value.getOperand(0), 3, 1);
        out.emit(Opcode::SimdPairUp32, 3, 3);
        out.emit(Opcode::SimdOr32, 2, 2, 3);
        if (!movePair) {
            read(value.getOperand(1), 3);
            read(value.getOperand(1), 4, 1);
            out.emit(Opcode::SimdPairUp32, 4, 4);
            out.emit(Opcode::SimdOr32, 3, 3, 4);
        }
        bool addition = value.getOpcode() == Instruction::Add;
        bool subtraction = value.getOpcode() == Instruction::Sub;
        if (addition || subtraction) {
            out.emit(addition ? Opcode::SimdAddCarry32 : Opcode::SimdLessUnsigned32, 5, 2, 3);
            out.emit(Opcode::SimdPairUp32, 5, 5);
        }
        out.emit(opcode, 4, 2, 3);
        if (addition || subtraction) {
            out.emit(opcode, 4, 4, 5);
        }
        write(&value, 4);
        out.emit(Opcode::SimdPairDown32, 4, 4);
        write(&value, 4, 1);
        return true;
    }
#endif

    Function* taskEntry(CallBase& call) {
        if (call.arg_size() != 3) {
            fail("task issue requires id, entry, and predecessor mask");
        }
        auto* f = dyn_cast<Function>(call.getArgOperand(1)->stripPointerCastsAndAliases());
        if (!f) {
            fail("task issue requires a constant task entry function");
        }
        if (!f->arg_empty() || !f->getReturnType()->isVoidTy() || f->isVarArg()) {
            fail("task entry must be void()");
        }
        return f;
    }

    void emitTask(CallBase& call) {
        auto name = call.getCalledFunction()->getName();
        unsigned count = name == "__extreme_task_issue"  ? 3
                         : name == "__extreme_task_read" ? 2
                         : name == "__extreme_task_id"   ? 0
                                                         : 1;
        bool terminal = name == "__extreme_task_finish" || name == "__extreme_task_abort";
        if (call.arg_size() != count ||
            (terminal ? !call.getType()->isVoidTy() : !call.getType()->isIntegerTy(32))) {
            fail("invalid task intrinsic signature");
        }
        if (count && !call.getArgOperand(0)->getType()->isIntegerTy(32)) {
            fail("task intrinsic requires a 32-bit operand");
        }
        if (name == "__extreme_task_issue" && !call.getArgOperand(2)->getType()->isIntegerTy(32)) {
            fail("task predecessor mask must be 32 bits");
        }
        if (name == "__extreme_task_read" && !call.getArgOperand(1)->getType()->isPointerTy()) {
            fail("task read requires a result pointer");
        }
        if (name == "__extreme_task_issue") {
            read(call.getArgOperand(0), 2);
            auto* target = taskEntry(call);
            if (out.code.size() % bytes + 6 > bytes) {
                out.align();
            }
            unsigned offset = out.code.size();
            immediate(3, 0);
            fixups.push_back({offset + 2, functionLabel(target), true});
            read(call.getArgOperand(2), 4);
            out.emit(Opcode::TaskIssue, 2, 3, 4);
        } else if (name == "__extreme_task_disarm" || name == "__extreme_task_state") {
            read(call.getArgOperand(0), 3);
            out.emit(name == "__extreme_task_disarm" ? Opcode::TaskDisarm : Opcode::TaskStatus, 2,
                     3);
        } else if (name == "__extreme_task_read") {
            read(call.getArgOperand(0), 3);
            out.emit(Opcode::TaskRead, 2, 3, 4);
            read(call.getArgOperand(1), 6);
            scalarStore(2, 6);
            out.emit(Opcode::Move, 2, 4);
        } else if (name == "__extreme_task_id") {
            out.emit(Opcode::TaskId, 2);
        } else if (terminal) {
            read(call.getArgOperand(0), 2);
            out.emit(name == "__extreme_task_finish" ? Opcode::TaskFinish : Opcode::TaskAbort, 2);
        } else {
            fail("unknown task intrinsic: " + name.str());
        }
        if (!terminal && !call.use_empty()) {
            write(&call);
        }
    }

    void mark(const std::string& l) {
        out.align();
        positions[l] = out.code.size();
    }

    void immediate(unsigned d, uint32_t v) {
        out.li(d, v);
    }

    void functionAddress(unsigned r, Function* f) {
        if (out.code.size() % bytes + 6 > bytes) {
            out.align();
        }
        unsigned offset = out.code.size();
        immediate(r, 0);
        fixups.push_back({offset + 2, functionLabel(f), true});
    }

    void alu(Opcode op, unsigned d, unsigned a, unsigned b) {
        out.emit(op, d, a, b);
    }

    void fence() {
        out.emit(Opcode::Barrier);
    }

    void scalarLoad(unsigned d, unsigned address, unsigned flag = 2) {
        out.emit(Opcode::LoadScalar, d, address, flag);
    }

    void scalarStore(unsigned d, unsigned address, unsigned flag = 2, bool barrier = true) {
        out.emit(Opcode::StoreScalar, d, address, flag);
        if (barrier) {
            fence();
        }
    }

    void jump(const std::string& label, Opcode op = Opcode::Jump, unsigned reg = 0) {
        // Padding before the instruction makes its fixup location unambiguous.
        unsigned n = extreme::length(uint8_t(op));
        if (out.code.size() % bytes + n > bytes) {
            out.align();
        }
        unsigned start = out.code.size();
        out.emit(op, reg);
        fixups.push_back({start + (op == Opcode::BranchZero ? 2u : 1u), label});
        if (op == Opcode::Call) {
            out.align(); // hardware return addresses are bus aligned
        }
    }

    uint32_t constant(const Constant* c) {
        if (auto* alias = dyn_cast<GlobalAlias>(c)) {
            return constant(alias->getAliasee());
        }
        if (auto* value = dyn_cast<ConstantFP>(c);
            value && (c->getType()->isFloatTy() || c->getType()->isDoubleTy())) {
            return value->getValueAPF().bitcastToAPInt().getZExtValue();
        }
        if (auto* n = dyn_cast<ConstantInt>(c)) {
            return uint32_t(n->getZExtValue());
        }
        if (isa<ConstantPointerNull>(c) || isa<UndefValue>(c) || isa<PoisonValue>(c) ||
            isa<ConstantAggregateZero>(c)) {
            return 0;
        }
        if (auto it = objects.find(c); it != objects.end()) {
            return it->second;
        }
        if (auto* e = dyn_cast<ConstantExpr>(c)) {
            auto mask = [&](uint32_t value) {
                unsigned width =
                    e->getType()->isIntegerTy() ? e->getType()->getIntegerBitWidth() : 32;
                return width < 32 ? value & ((uint32_t(1) << width) - 1) : value;
            };
            if (e->getNumOperands() == 2 && e->getType()->isIntegerTy() &&
                e->getType()->getIntegerBitWidth() <= 32) {
                uint32_t a = constant(cast<Constant>(e->getOperand(0)));
                uint32_t b = constant(cast<Constant>(e->getOperand(1)));
                switch (e->getOpcode()) {
                case Instruction::Add:
                    return mask(a + b);
                case Instruction::Sub:
                    return mask(a - b);
                case Instruction::Mul:
                    return mask(a * b);
                case Instruction::And:
                    return mask(a & b);
                case Instruction::Or:
                    return mask(a | b);
                case Instruction::Xor:
                    return mask(a ^ b);
                default:
                    break;
                }
            }
            if (e->isCast()) {
                return mask(constant(cast<Constant>(e->getOperand(0))));
            }
            if (e->getOpcode() == Instruction::GetElementPtr) {
                APInt offset(32, 0);
                if (!cast<GEPOperator>(e)->accumulateConstantOffset(dl, offset)) {
                    fail("nonconstant initializer GEP");
                }
                return constant(cast<Constant>(e->getOperand(0))) + uint32_t(offset.getZExtValue());
            }
        }
        std::string value;
        raw_string_ostream stream(value);
        c->print(stream);
        fail("unsupported constant/initializer: " + value);
    }

    void initialize(Constant* c, Type* t, uint32_t address) {
        if (isa<ConstantAggregateZero>(c) || isa<UndefValue>(c) || isa<PoisonValue>(c)) {
            return;
        }
        if (t->isIntegerTy() || t->isPointerTy() || t->isFloatTy() || t->isDoubleTy()) {
            if (auto* f = dyn_cast<Function>(c->stripPointerCastsAndAliases())) {
                if (!reentrant) {
                    fail("function pointer initializers require --reentrant");
                }
                dataFixups.push_back({address, functionLabel(f), true});
                return;
            }
            unsigned n = dl.getTypeStoreSize(t);
            if (n > 8) {
                fail("global integers wider than 64 bits are not supported");
            }
            uint64_t v = isa<ConstantInt>(c) ? cast<ConstantInt>(c)->getZExtValue()
                         : isa<ConstantFP>(c)
                             ? cast<ConstantFP>(c)->getValueAPF().bitcastToAPInt().getZExtValue()
                             : constant(c);
            for (unsigned j = 0; j < n; ++j) {
                image.at(address + j) = v >> (j * 8);
            }
            return;
        }
        if (auto* st = dyn_cast<StructType>(t)) {
            auto* layout = dl.getStructLayout(st);
            for (unsigned i = 0; i < st->getNumElements(); ++i) {
                initialize(c->getAggregateElement(i), st->getElementType(i),
                           address + layout->getElementOffset(i));
            }
            return;
        }
        if (auto* ar = dyn_cast<ArrayType>(t)) {
            unsigned stride = dl.getTypeAllocSize(ar->getElementType());
            for (unsigned i = 0; i < ar->getNumElements(); ++i) {
                initialize(c->getAggregateElement(i), ar->getElementType(), address + i * stride);
            }
            return;
        }
        fail("unsupported global aggregate");
    }

    void read(Value* v, unsigned r, unsigned part = 0) {
        if (auto* c = dyn_cast<Constant>(v); c && v->getType()->isArrayTy()) {
            width(v->getType()); // Restrict SSA aggregate values to the supported word ABI.
            immediate(r, constant(c->getAggregateElement(part)));
            return;
        }
        if (part && isa<ConstantFP>(v)) {
            immediate(r, cast<ConstantFP>(v)->getValueAPF().bitcastToAPInt().getZExtValue() >> 32);
            return;
        }
        if (part && isa<Constant>(v)) {
            auto* integer = dyn_cast<ConstantInt>(v);
            immediate(r, integer ? uint32_t(integer->getZExtValue() >> 32) : 0);
            return;
        }
        if (auto* f = dyn_cast<Function>(v->stripPointerCastsAndAliases())) {
            functionAddress(r, f);
            return;
        }
        if (auto* c = dyn_cast<Constant>(v)) {
            immediate(r, constant(c));
            return;
        }
        if (auto it = objects.find(v); it != objects.end()) {
            if (isa<AllocaInst>(v)) {
                frameAddress(r, it->second);
            } else {
                immediate(r, it->second);
            }
            return;
        }
        if (!slots.count(v)) {
            fail("value has no location");
        }
        frameAddress(5, slots.at(v) + part * 4);
        scalarLoad(r, 5);
    }

    void write(Value* v, unsigned r = 2, unsigned part = 0) {
        frameAddress(5, slots.at(v) + part * 4);
        scalarStore(r, 5);
    }

    void normalize(unsigned r, unsigned w) {
        if (w < 32) {
            immediate(4, (uint32_t(1) << w) - 1);
            alu(Opcode::And, r, r, 4);
        }
    }

    void signExtend(unsigned r, unsigned w) {
        if (w < 32) {
            immediate(4, 32 - w);
            alu(Opcode::ShiftLeft, r, r, 4);
            alu(Opcode::ShiftArithmetic, r, r, 4);
        }
    }

    bool builtin(Function* f) {
        if (!f) {
            return false;
        }
#ifdef EC_SIMD
        if (simdBuiltin(f)) {
            return true;
        }
#endif
        return layoutBuiltin(f) || contextBuiltin(f) || taskBuiltin(f) || f->isIntrinsic() ||
               f->getName() == "memcpy" || f->getName() == "memset" || f->getName() == "memmove";
    }

    unsigned discover(Function* f) {
        if (visiting.count(f)) {
            if (reentrant) {
                return 0; // Hardware bounds dynamic depth using the return-register limit.
            }
            fail("recursive calls require reentrant local storage; this static-frame ABI rejects "
                 "recursion");
        }
        if (visited.count(f)) {
            return depths.at(f);
        }
        if (f->isDeclaration()) {
            fail("unresolved function: " + f->getName().str());
        }
        visiting.insert(f);
        unsigned depth = 1;
        for (auto& block : *f) {
            for (auto& inst : block) {
                if (auto* call = dyn_cast<CallBase>(&inst)) {
                    if (call->isInlineAsm()) {
                        continue;
                    }
                    auto* target =
                        dyn_cast<Function>(call->getCalledOperand()->stripPointerCastsAndAliases());
                    if (!target) {
                        if (!reentrant) {
                            fail(
                                "indirect calls are not yet supported by the static-frame compiler "
                                "ABI; use --reentrant");
                        }
                        for (auto& candidate : module) {
                            if (candidate.hasAddressTaken() && !builtin(&candidate) &&
                                candidate.getFunctionType() == call->getFunctionType()) {
                                ordinaryCallees.insert(&candidate);
                                callGraph[f].insert(&candidate);
                                discover(&candidate);
                            }
                        }
                        continue;
                    }
                    if (target->getName() == "__extreme_task_issue") {
                        auto* task = taskEntry(*call);
                        if (taskRoots.insert(task).second) {
                            taskEntries.push_back(task);
                        }
                    }
                    if (!builtin(target)) {
                        ordinaryCallees.insert(target);
                        callGraph[f].insert(target);
                        depth = std::max(depth, 1 + discover(target));
                    }
                }
            }
        }
        visiting.erase(f);
        visited.insert(f);
        functions.push_back(f);
        depths[f] = depth;
        return depth;
    }

    uint64_t activationBound(Function* function, unsigned remainingCalls) {
        auto key = std::make_pair(function, remainingCalls);
        if (auto found = activationBounds.find(key); found != activationBounds.end()) {
            return found->second;
        }
        // Even a rejected overflowing CALL has already prepared its arguments.
        uint64_t child = remainingCalls == 0 ? mailboxBytes : 0;
        if (remainingCalls) {
            for (auto* target : callGraph[function]) {
                child = std::max(child, activationBound(target, remainingCalls - 1));
            }
        }
        return activationBounds[key] = activationBytes.at(function) + child;
    }

    void layout() {
        frameAlignment = bytes;
        for (auto* function : functions) {
            for (auto& block : *function) {
                for (auto& instruction : block) {
                    if (auto* local = dyn_cast<AllocaInst>(&instruction)) {
                        frameAlignment =
                            std::max(frameAlignment, unsigned(local->getAlign().value()));
                    }
                }
            }
        }
        for (auto& g : module.globals()) {
            if (g.getName() == "llvm.global_ctors" || g.getName() == "llvm.global_dtors") {
                fail("dynamic global constructors/destructors are unsupported");
            }
            if (g.isDeclaration() && !g.hasExternalWeakLinkage()) {
                fail("unresolved global: " + g.getName().str());
            }
            if (g.isThreadLocal()) {
                fail("thread-local storage is unsupported");
            }
        }
        // Explicit package sections delimit the mutable userspace image for
        // flat-address-space fork snapshots. Kernel globals are outside it.
        bool hasUserData = false;
        for (auto& g : module.globals()) {
            hasUserData |= g.getSection().starts_with(".extreme.");
        }
        for (unsigned group = 0; group < 4; ++group) {
            if (group == 1) {
                userDataBegin = allocate(0, hasUserData ? 4096 : bytes);
            }
            if (group == 2) {
                userDataEnd = allocate(0, hasUserData ? 4096 : bytes);
            }
            for (auto& g : module.globals()) {
                if (g.getName() == "llvm.used" || g.getName() == "llvm.compiler.used") {
                    objects[&g] = 0;
                    continue;
                }
                unsigned section = g.getSection() == ".extreme.activation" ? 3
                                   : g.getSection() == ".extreme.heap"     ? 2
                                   : g.getSection() == ".extreme.user"     ? 1
                                                                           : 0;
                if (g.isDeclaration()) {
                    objects[&g] = 0;
                    continue;
                }
                if (section == group) {
                    objects[&g] = allocate(dl.getTypeAllocSize(g.getValueType()),
                                           g.getAlign().valueOrOne().value());
                }
            }
            if (group == 2) {
                userHeapEnd = cursor;
            }
        }
        for (auto& g : module.globals()) {
            if (g.getName() == "llvm.used" || g.getName() == "llvm.compiler.used") {
                continue;
            }
            if (g.hasInitializer()) {
                initialize(g.getInitializer(), g.getValueType(), objects.at(&g));
            }
        }
        if (taskMode || reentrant) {
            frameLayout = true;
            mailbox = allocate(mailboxBytes, bytes);
            returnSave = allocate(2 * bytes, bytes);
        }
        uint32_t frameHeader = frameCursor;
        uint32_t maximumFrame = frameHeader;
        for (auto* f : functions) {
            if (reentrant) {
                frameCursor = frameHeader;
            }
            unsigned argumentWords = 0;
            for (auto& a : f->args()) {
                argumentWords += valueWords(a.getType());
            }
            if (argumentWords > mailboxWords) {
                fail("function arguments exceed mailbox capacity");
            }
            if (f->isVarArg()) {
                varargSlots[f] = allocate(mailboxWords * 4, 8);
            }

            for (auto& a : f->args()) {
                slots[&a] = allocate(valueWords(a.getType()) * 4);
            }
            for (auto& bb : *f) {
                labels[&bb] = unique(f->getName().str() + ".block");
                for (auto& i : bb) {
                    if (auto* a = dyn_cast<AllocaInst>(&i)) {
                        auto* n = dyn_cast<ConstantInt>(a->getArraySize());
                        if (!n) {
                            if (!dynamicAreas.count(f)) {
                                dynamicAreas[f] = allocate(dynamicLocalBytes, frameAlignment);
                                dynamicCursors[f] = allocate(4);
                            }
                            slots[a] = allocate(4);
                            continue;
                        }
                        objects[a] =
                            allocate(dl.getTypeAllocSize(a->getAllocatedType()) * n->getZExtValue(),
                                     a->getAlign().value());
                    } else if (!i.getType()->isVoidTy()) {
                        slots[&i] = allocate(valueWords(i.getType()) * 4);
                        if (isa<PHINode>(i)) {
                            phiTemps[&i] = allocate(valueWords(i.getType()) * 4);
                        }
                    }
                }
            }
            maximumFrame = std::max(maximumFrame, frameCursor);
            if (reentrant) {
                activationBytes[f] =
                    (frameCursor + frameAlignment - 1) / frameAlignment * frameAlignment;
            }
        }
        if (taskMode || reentrant) {
            frameLayout = false;
            frameStride = frameAlignment;
            while (frameStride < maximumFrame) {
                frameStride *= 2;
            }
            contextStride = frameStride;
            if (reentrant) {
                uint64_t bootBytes = activationBound(entryFunction, 2 * (bits / 32));
                uint64_t required = taskMode ? 0 : bootBytes;
                for (auto* task : taskRoots) {
                    required = std::max(required, activationBound(task, 2 * (bits / 32)));
                }
                contextStride = frameAlignment;
                while (uint64_t(contextStride) < required) {
                    if (contextStride > 0x7fffffff) {
                        fail("activation storage exceeds the address space");
                    }
                    contextStride *= 2;
                }
                frameBase = allocate(uint64_t(contextStride) * (taskMode ? 32 : 1), frameAlignment);
                // A large boot-only local object need not be duplicated for all task IDs.
                bootFrameBase = taskMode ? allocate(bootBytes, frameAlignment) : frameBase;
            } else {
                frameBase = allocate(uint64_t(contextStride) * (taskMode ? 33 : 1), frameAlignment);
                bootFrameBase = frameBase + (taskMode ? 32 * contextStride : 0);
            }
        }
    }

    void edge(BasicBlock* from, BasicBlock* to) {
        // All incoming values are saved before any PHI destination is changed.
        for (auto& phi : to->phis()) {
            for (unsigned part = 0; part < valueWords(phi.getType()); ++part) {
                read(phi.getIncomingValueForBlock(from), 2, part);
                frameAddress(5, phiTemps.at(&phi) + part * 4);
                scalarStore(2, 5);
            }
        }
        for (auto& phi : to->phis()) {
            for (unsigned part = 0; part < valueWords(phi.getType()); ++part) {
                frameAddress(5, phiTemps.at(&phi) + part * 4);
                scalarLoad(2, 5);
                write(&phi, 2, part);
            }
        }
        jump(labels.at(to));
    }

    void copy(CallBase& call) {
        Value* dest = call.getArgOperand(0);
        Value* src = call.getArgOperand(1);
        Value* size = call.getArgOperand(2);
        if (auto* mi = dyn_cast<MemIntrinsic>(&call)) {
            if (mi->isVolatile()) {
                memmoveCall(call);
                return;
            }
        }
        if (!call.getType()->isVoidTy()) {
            read(dest, 2);
            write(&call);
        }
        // Preserve the return-address lanes while using the ISA's implicit COPY registers.
        bool preserveReturns =
            call.getFunction() != entryFunction && !taskRoots.count(call.getFunction());
        if (preserveReturns) {
            if (taskMode || reentrant) {
                frameAddress(5, returnSave);
                out.emit(Opcode::Store, 0, 5);
                frameAddress(5, returnSave + bytes);
                out.emit(Opcode::Store, 1, 5);
            } else {
                out.emit(Opcode::Move, 6, 0);
                out.emit(Opcode::Move, 7, 1);
            }
        }
        read(src, 0);
        read(dest, 1);
        bool aligned = false;
        uint32_t length = 0;
        if (auto* n = dyn_cast<ConstantInt>(size)) {
            length = n->getZExtValue();
            if (auto* mi = dyn_cast<MemCpyInst>(&call)) {
                aligned = mi->getSourceAlign().valueOrOne().value() >= bytes &&
                          mi->getDestAlign().valueOrOne().value() >= bytes;
            }
            if (isa<Constant>(src) && isa<Constant>(dest)) {
                aligned = (constant(cast<Constant>(src)) % bytes == 0 &&
                           constant(cast<Constant>(dest)) % bytes == 0);
            }
        }
        if (aligned && isa<ConstantInt>(size) && length % bytes == 0) {
            immediate(2, length / bytes);
            out.emit(Opcode::Copy);
            ++copyCount;
        } else {
            read(size, 2);
            auto prefix = unique("copy.prefix"), bulk = unique("copy.bulk"),
                 tail = unique("copy.tail"), done = unique("copy.done");
            mark(prefix);
            jump(done, Opcode::BranchZero, 2);
            alu(Opcode::Or, 3, 0, 1);
            immediate(4, bytes - 1);
            alu(Opcode::And, 3, 3, 4);
            jump(bulk, Opcode::BranchZero, 3);
            scalarLoad(3, 0, 4);
            scalarStore(3, 1, 0, false);
            immediate(4, 1);
            alu(Opcode::Add, 0, 0, 4);
            alu(Opcode::Add, 1, 1, 4);
            alu(Opcode::Sub, 2, 2, 4);
            jump(prefix);
            mark(bulk);
            immediate(4, bytes - 1);
            alu(Opcode::And, 4, 2, 4);
            immediate(5, 0);
            unsigned shift = 0;
            for (unsigned n = bytes; n > 1; n >>= 1) {
                ++shift;
            }
            immediate(5, shift);
            alu(Opcode::ShiftRight, 2, 2, 5);
            out.emit(Opcode::Copy);
            ++copyCount;
            out.emit(Opcode::Move, 2, 4);
            mark(tail);
            jump(done, Opcode::BranchZero, 2);
            scalarLoad(3, 0, 4);
            scalarStore(3, 1, 0, false);
            immediate(4, 1);
            alu(Opcode::Add, 0, 0, 4);
            alu(Opcode::Add, 1, 1, 4);
            alu(Opcode::Sub, 2, 2, 4);
            jump(tail);
            mark(done);
        }
        fence();
        if (preserveReturns) {
            if (taskMode || reentrant) {
                frameAddress(5, returnSave);
                out.emit(Opcode::Load, 0, 5);
                frameAddress(5, returnSave + bytes);
                out.emit(Opcode::Load, 1, 5);
            } else {
                out.emit(Opcode::Move, 0, 6);
                out.emit(Opcode::Move, 1, 7);
            }
        }
    }

    void memmoveCall(CallBase& call) {
        auto* memory = dyn_cast<MemIntrinsic>(&call);
        bool volatileAccess = memory && memory->isVolatile();
        read(call.getArgOperand(0), 2);
        read(call.getArgOperand(1), 3);
        read(call.getArgOperand(2), 4);
        if (!call.getType()->isVoidTy()) {
            write(&call, 2);
        }
        fence();
        auto forward = unique("move.forward"), backward = unique("move.backward"),
             done = unique("move.done");
        jump(done, Opcode::BranchZero, 4);
        alu(Opcode::LessUnsigned, 6, 3, 2);
        jump(forward, Opcode::BranchZero, 6);
        alu(Opcode::Add, 2, 2, 4);
        alu(Opcode::Add, 3, 3, 4);
        mark(backward);
        immediate(5, 1);
        alu(Opcode::Sub, 2, 2, 5);
        alu(Opcode::Sub, 3, 3, 5);
        scalarLoad(6, 3, 4);
        scalarStore(6, 2, 0, volatileAccess);
        alu(Opcode::Sub, 4, 4, 5);
        jump(done, Opcode::BranchZero, 4);
        jump(backward);
        mark(forward);
        scalarLoad(6, 3, 4);
        scalarStore(6, 2, 0, volatileAccess);
        immediate(5, 1);
        alu(Opcode::Add, 2, 2, 5);
        alu(Opcode::Add, 3, 3, 5);
        alu(Opcode::Sub, 4, 4, 5);
        jump(done, Opcode::BranchZero, 4);
        jump(forward);
        mark(done);
        fence();
    }

    void memsetCall(CallBase& call) {
        auto* memory = dyn_cast<MemIntrinsic>(&call);
        bool volatileAccess = memory && memory->isVolatile();
        read(call.getArgOperand(0), 2);
        read(call.getArgOperand(1), 3);
        read(call.getArgOperand(2), 4);
        if (!call.getType()->isVoidTy()) {
            write(&call, 2);
        }
        auto loop = unique("set.loop"), done = unique("set.done");
        mark(loop);
        jump(done, Opcode::BranchZero, 4);
        scalarStore(3, 2, 0, volatileAccess);
        immediate(5, 1);
        alu(Opcode::Add, 2, 2, 5);
        alu(Opcode::Sub, 4, 4, 5);
        jump(loop);
        mark(done);
        fence();
    }

    void emitCall(CallBase& call) {
        if (call.isInlineAsm()) {
            auto* assembly = cast<InlineAsm>(call.getCalledOperand());
            std::istringstream source(assembly->getAsmString().str());
            std::string line;
            while (std::getline(source, line)) {
                auto text = StringRef(line).trim();
                if (!text.empty() && !text.starts_with("#")) {
                    fail("target assembly is not supported: " + assembly->getAsmString().str());
                }
            }
            fence();
            if (!call.getType()->isVoidTy()) {
                if (call.arg_size() != 1 || (assembly->getConstraintString() != "=r,0" &&
                                             assembly->getConstraintString() != "=r,0,~{memory}")) {
                    fail("inline assembly result requires a tied input");
                }
                read(call.getArgOperand(0), 2);
                write(&call);
            }
            return;
        }
        auto* f = dyn_cast<Function>(call.getCalledOperand()->stripPointerCastsAndAliases());
        auto name = f ? f->getName() : StringRef();
#ifdef EC_SIMD
        if (f && simdBuiltin(f)) {
            emitSimd(call);
            return;
        }
#endif
        if (layoutBuiltin(f)) {
            emitLayout(call);
            return;
        }
        if (contextBuiltin(f)) {
            emitContext(call);
            return;
        }
        if (taskBuiltin(f)) {
            emitTask(call);
            return;
        }
        if ((name.starts_with("llvm.lifetime.") ||
             name == "llvm.experimental.noalias.scope.decl") ||
            name.starts_with("llvm.assume") || name.starts_with("llvm.dbg.")) {
            return;
        }
        if (name.starts_with("llvm.memcpy.") || name == "memcpy") {
            copy(call);
            return;
        }
        if (name.starts_with("llvm.memmove.") || name == "memmove") {
            memmoveCall(call);
            return;
        }
        if (name.starts_with("llvm.memset.") || name == "memset") {
            memsetCall(call);
            return;
        }
        if (name == "llvm.bswap.i16" || name == "llvm.bswap.i32") {
            unsigned bytes = width(call.getType()) / 8;
            read(call.getArgOperand(0), 2);
            immediate(6, 0);
            for (unsigned byte = 0; byte < bytes; ++byte) {
                immediate(4, byte * 8);
                alu(Opcode::ShiftRight, 3, 2, 4);
                immediate(5, 255);
                alu(Opcode::And, 3, 3, 5);
                immediate(4, (bytes - 1 - byte) * 8);
                alu(Opcode::ShiftLeft, 3, 3, 4);
                alu(Opcode::Or, 6, 6, 3);
            }
            out.emit(Opcode::Move, 2, 6);
            write(&call);
            return;
        }
        if ((name.starts_with("llvm.fshl.i") || name.starts_with("llvm.fshr.i")) &&
            width(call.getType()) <= 32) {
            unsigned laneWidth = width(call.getType());
            read(call.getArgOperand(0), 2);
            read(call.getArgOperand(1), 3);
            read(call.getArgOperand(2), 4);
            immediate(6, laneWidth - 1);
            alu(Opcode::And, 4, 4, 6);
            auto zero = unique("funnel.zero"), done = unique("funnel.done");
            jump(zero, Opcode::BranchZero, 4);
            bool left = name.starts_with("llvm.fshl.i");
            alu(left ? Opcode::ShiftLeft : Opcode::ShiftRight, left ? 2 : 3, left ? 2 : 3, 4);
            immediate(6, laneWidth);
            alu(Opcode::Sub, 4, 6, 4);
            alu(left ? Opcode::ShiftRight : Opcode::ShiftLeft, left ? 3 : 2, left ? 3 : 2, 4);
            alu(Opcode::Or, 2, 2, 3);
            jump(done);
            mark(zero);
            if (!left) {
                out.emit(Opcode::Move, 2, 3);
            }
            mark(done);
            normalize(2, laneWidth);
            write(&call);
            return;
        }
        if (name.starts_with("llvm.stacksave")) {
            if (dynamicCursors.count(currentFunction)) {
                frameAddress(5, dynamicCursors.at(currentFunction));
                scalarLoad(2, 5);
            } else {
                immediate(2, 0);
            }
            write(&call);
            return;
        }
        if (name.starts_with("llvm.stackrestore")) {
            if (dynamicCursors.count(currentFunction)) {
                read(call.getArgOperand(0), 2);
                frameAddress(5, dynamicCursors.at(currentFunction));
                scalarStore(2, 5);
            }
            return;
        }
        if (name == "llvm.ptrmask.p0.i32") {
            read(call.getArgOperand(0), 2);
            read(call.getArgOperand(1), 3);
            alu(Opcode::And, 2, 2, 3);
            write(&call);
            return;
        }
        if (name.starts_with("llvm.va_start")) {
            unsigned fixedWords = 0;
            for (auto& argument : call.getFunction()->args()) {
                fixedWords += valueWords(argument.getType());
            }
            read(call.getArgOperand(0), 2);
            frameAddress(3, varargSlots.at(call.getFunction()) + fixedWords * 4);
            scalarStore(3, 2);
            return;
        }
        if (name.starts_with("llvm.va_end")) {
            return;
        }
        if (name.starts_with("llvm.va_copy")) {
            read(call.getArgOperand(0), 2);
            read(call.getArgOperand(1), 3);
            scalarLoad(3, 3);
            scalarStore(3, 2);
            return;
        }
        if (name == "llvm.abs.i32") {
            read(call.getArgOperand(0), 2);
            immediate(3, 31);
            alu(Opcode::ShiftArithmetic, 3, 2, 3);
            alu(Opcode::Xor, 2, 2, 3);
            alu(Opcode::Sub, 2, 2, 3);
            write(&call);
            return;
        }
        if (name == "llvm.usub.sat.i32") {
            read(call.getArgOperand(0), 2);
            read(call.getArgOperand(1), 3);
            alu(Opcode::LessUnsigned, 4, 2, 3);
            auto normal = unique("usub.normal"), done = unique("usub.done");
            jump(normal, Opcode::BranchZero, 4);
            immediate(2, 0);
            jump(done);
            mark(normal);
            alu(Opcode::Sub, 2, 2, 3);
            mark(done);
            write(&call);
            return;
        }
        if (name == "llvm.ctpop.i32") {
            read(call.getArgOperand(0), 2);
            immediate(3, 0);
            immediate(4, 1);
            auto loop = unique("popcount.loop"), done = unique("popcount.done");
            mark(loop);
            jump(done, Opcode::BranchZero, 2);
            alu(Opcode::Sub, 6, 2, 4);
            alu(Opcode::And, 2, 2, 6);
            alu(Opcode::Add, 3, 3, 4);
            jump(loop);
            mark(done);
            write(&call, 3);
            return;
        }
        if (name == "llvm.ctlz.i32" || name == "llvm.cttz.i32") {
            bool trailing = name == "llvm.cttz.i32";
            read(call.getArgOperand(0), 2);
            immediate(3, 0);
            immediate(4, trailing ? 1 : 0x80000000u);
            auto loop = unique("clz.loop"), zero = unique("clz.zero"), done = unique("clz.done");
            jump(zero, Opcode::BranchZero, 2);
            mark(loop);
            alu(Opcode::And, 6, 2, 4);
            auto next = unique("clz.next");
            jump(next, Opcode::BranchZero, 6);
            jump(done);
            mark(next);
            immediate(5, 1);
            alu(trailing ? Opcode::ShiftRight : Opcode::ShiftLeft, 2, 2, 5);
            alu(Opcode::Add, 3, 3, 5);
            jump(loop);
            mark(zero);
            immediate(3, 32);
            mark(done);
            write(&call, 3);
            return;
        }
        if (name == "llvm.trap" || name == "llvm.ubsantrap") {
            out.code.push_back(0xff);
            return;
        }
        if (name.starts_with("llvm.smax.") || name.starts_with("llvm.smin.") ||
            name.starts_with("llvm.umax.") || name.starts_with("llvm.umin.")) {
            read(call.getArgOperand(0), 2);
            read(call.getArgOperand(1), 3);
            bool signedOp = name.starts_with("llvm.s");
            if (signedOp) {
                signExtend(2, width(call.getType()));
                signExtend(3, width(call.getType()));
            }
            alu(signedOp ? Opcode::LessSigned : Opcode::LessUnsigned, 4, 2, 3);
            auto keep = unique("minmax.keep");
            bool max = name.contains("max");
            if (!max) {
                immediate(5, 1);
                alu(Opcode::Xor, 4, 4, 5);
            }
            jump(keep, Opcode::BranchZero, 4);
            out.emit(Opcode::Move, 2, 3);
            mark(keep);
            normalize(2, width(call.getType()));
            write(&call);
            return;
        }
        if (f && f->isIntrinsic()) {
            fail("unsupported LLVM intrinsic: " + name.str());
        }
        unsigned i = 0, argumentIndex = 0;
        unsigned nextFrame = reentrant ? activationBytes.at(call.getFunction()) : 0;
        for (auto& arg : call.args()) {
            if (argumentIndex++ >= call.getFunctionType()->getNumParams() &&
                valueWords(arg->getType()) == 2) {
                i = (i + 1) & ~1u;
            }
            for (unsigned part = 0; part < valueWords(arg->getType()); ++part) {
                if (i >= mailboxWords) {
                    fail("call arguments exceed mailbox capacity");
                }
                read(arg.get(), 2, part);
                frameAddress(5, mailbox + 4 * i++ + nextFrame);
                scalarStore(2, 5);
            }
        }
        if (!f) {
            read(call.getCalledOperand(), 6);
        }
        if (reentrant) {
            immediate(4, nextFrame);
            alu(Opcode::Add, 7, 7, 4);
        }
        if (f) {
            jump(functionLabel(f), Opcode::Call);
        } else {
            out.emit(Opcode::CallRegister, 6);
            out.align();
        }
        if (!call.getType()->isVoidTy()) {
            for (unsigned part = 0; part < valueWords(call.getType()); ++part) {
                frameAddress(5, returnOffset() + part * 4);
                scalarLoad(2 + part, 5);
            }
        }
        if (reentrant) {
            immediate(4, nextFrame);
            alu(Opcode::Sub, 7, 7, 4);
        }
        if (!call.getType()->isVoidTy()) {
            for (unsigned part = 0; part < valueWords(call.getType()); ++part) {
                write(&call, 2 + part, part);
            }
        }
    }

    void instruction(Instruction& i) {
        if (isa<PHINode>(i)) {
            return;
        }
        if (auto* local = dyn_cast<AllocaInst>(&i)) {
            if (objects.count(local)) {
                return;
            }
            read(local->getArraySize(), 2);
            unsigned element = dl.getTypeAllocSize(local->getAllocatedType());
            immediate(3, dynamicLocalBytes / element);
            alu(Opcode::LessUnsigned, 4, 3, 2);
            auto countOk = unique("alloca.count"), spaceOk = unique("alloca.space");
            jump(countOk, Opcode::BranchZero, 4);
            out.code.push_back(0xff);
            mark(countOk);
            immediate(3, element);
            alu(Opcode::Multiply, 2, 2, 3);
            frameAddress(5, dynamicCursors.at(currentFunction));
            scalarLoad(3, 5);
            unsigned alignment = local->getAlign().value();
            immediate(4, alignment - 1);
            alu(Opcode::Add, 3, 3, 4);
            immediate(4, ~(alignment - 1));
            alu(Opcode::And, 3, 3, 4);
            alu(Opcode::Add, 4, 3, 2);
            immediate(6, dynamicLocalBytes);
            alu(Opcode::LessUnsigned, 6, 6, 4);
            jump(spaceOk, Opcode::BranchZero, 6);
            out.code.push_back(0xff);
            mark(spaceOk);
            frameAddress(5, dynamicCursors.at(currentFunction));
            scalarStore(4, 5);
            frameAddress(2, dynamicAreas.at(currentFunction));
            alu(Opcode::Add, 2, 2, 3);
            write(local);
            return;
        }
        if (auto* extract = dyn_cast<ExtractValueInst>(&i)) {
            auto* source = extract->getAggregateOperand();
            width(source->getType());
            if (!source->getType()->isArrayTy() || extract->getNumIndices() != 1) {
                fail("only word-array aggregate extracts are supported");
            }
            read(source, 2, extract->getIndices()[0]);
            write(&i);
            return;
        }
        if (auto* insert = dyn_cast<InsertValueInst>(&i)) {
            unsigned words = valueWords(i.getType());
            if (!i.getType()->isArrayTy() || insert->getNumIndices() != 1) {
                fail("only word-array aggregate inserts are supported");
            }
            for (unsigned part = 0; part < words; ++part) {
                if (part == insert->getIndices()[0]) {
                    read(insert->getInsertedValueOperand(), 2);
                } else {
                    read(insert->getAggregateOperand(), 2, part);
                }
                write(&i, 2, part);
            }
            return;
        }
        if (auto* b = dyn_cast<BinaryOperator>(&i)) {
            if (width(b->getType()) > 32) {
#ifdef EC_SIMD
                if (emitSimdInteger64(*b)) {
                    return;
                }
#endif
                fail("64-bit arithmetic was not legalized");
            }
            read(b->getOperand(0), 2);
            read(b->getOperand(1), 3);
            Opcode op;
            switch (b->getOpcode()) {
            case Instruction::Add:
                op = Opcode::Add;
                break;
            case Instruction::Sub:
                op = Opcode::Sub;
                break;
            case Instruction::Mul:
                op = Opcode::Multiply;
                break;
            case Instruction::And:
                op = Opcode::And;
                break;
            case Instruction::Or:
                op = Opcode::Or;
                break;
            case Instruction::Xor:
                op = Opcode::Xor;
                break;
            case Instruction::Shl:
                op = Opcode::ShiftLeft;
                break;
            case Instruction::LShr:
                op = Opcode::ShiftRight;
                break;
            case Instruction::AShr:
                op = Opcode::ShiftArithmetic;
                signExtend(2, width(i.getType()));
                break;
            case Instruction::UDiv:
                op = Opcode::DivideUnsigned;
                break;
            case Instruction::URem:
                op = Opcode::RemainderUnsigned;
                break;
            case Instruction::SDiv:
                op = Opcode::DivideSigned;
                signExtend(2, width(i.getType()));
                signExtend(3, width(i.getType()));
                break;
            case Instruction::SRem:
                op = Opcode::RemainderSigned;
                signExtend(2, width(i.getType()));
                signExtend(3, width(i.getType()));
                break;
            default:
                fail("unsupported binary operator");
            }
            alu(op, 2, 2, 3);
            normalize(2, width(i.getType()));
            write(&i);
            return;
        }
        if (auto* l = dyn_cast<LoadInst>(&i)) {
            if (l->isAtomic()) {
                fail("atomic loads unsupported");
            }
            unsigned n = width(l->getType());
            if (n == 64) {
                for (unsigned part = 0; part < 2; ++part) {
                    read(l->getPointerOperand(), 2);
                    immediate(4, part * 4);
                    alu(Opcode::Add, 2, 2, 4);
                    if (l->getAlign().value() < 4) {
                        immediate(6, 0);
                        for (unsigned byte = 0; byte < 4; ++byte) {
                            scalarLoad(3, 2, 4);
                            immediate(4, byte * 8);
                            alu(Opcode::ShiftLeft, 3, 3, 4);
                            alu(Opcode::Or, 6, 6, 3);
                            immediate(4, 1);
                            alu(Opcode::Add, 2, 2, 4);
                        }
                        out.emit(Opcode::Move, 2, 6);
                    } else {
                        scalarLoad(2, 2);
                    }
                    write(&i, 2, part);
                }
                return;
            }
            read(l->getPointerOperand(), 2);
            unsigned flag = n <= 8 ? 4 : n <= 16 ? 5 : 2;
            if (l->getAlign().value() < (n + 7) / 8 && n > 8) {
                // Packed/unaligned C++ objects: assemble bytes without misaligned bus accesses.
                immediate(6, 0);
                for (unsigned j = 0; j < (n + 7) / 8; ++j) {
                    scalarLoad(3, 2, 4);
                    immediate(4, j * 8);
                    alu(Opcode::ShiftLeft, 3, 3, 4);
                    alu(Opcode::Or, 6, 6, 3);
                    immediate(4, 1);
                    alu(Opcode::Add, 2, 2, 4);
                }
                out.emit(Opcode::Move, 2, 6);
            } else {
                scalarLoad(2, 2, flag);
            }
            normalize(2, n);
            write(&i);
            return;
        }
        if (auto* s = dyn_cast<StoreInst>(&i)) {
            if (s->isAtomic()) {
                fail("atomic stores unsupported");
            }
            // Clang coalesces small aggregate initializers into wide constant stores.
            // Legalize their bytes without introducing 64-bit architectural arithmetic.
            if (auto* constant = dyn_cast<ConstantInt>(s->getValueOperand());
                constant && constant->getBitWidth() > 32 && !s->isVolatile()) {
                read(s->getPointerOperand(), 3);
                for (unsigned byte = 0; byte < (constant->getBitWidth() + 7) / 8; ++byte) {
                    immediate(2, constant->getValue().lshr(byte * 8).trunc(8).getZExtValue());
                    scalarStore(2, 3, 0, false);
                    immediate(4, 1);
                    alu(Opcode::Add, 3, 3, 4);
                }
                fence();
                return;
            }
            unsigned n = width(s->getValueOperand()->getType());
            if (n == 64) {
                for (unsigned part = 0; part < 2; ++part) {
                    read(s->getValueOperand(), 2, part);
                    read(s->getPointerOperand(), 3);
                    immediate(4, part * 4);
                    alu(Opcode::Add, 3, 3, 4);
                    if (s->getAlign().value() < 4) {
                        for (unsigned byte = 0; byte < 4; ++byte) {
                            scalarStore(2, 3, 0, false);
                            immediate(4, 8);
                            alu(Opcode::ShiftRight, 2, 2, 4);
                            immediate(4, 1);
                            alu(Opcode::Add, 3, 3, 4);
                        }
                        fence();
                    } else {
                        scalarStore(2, 3);
                    }
                }
                return;
            }
            read(s->getValueOperand(), 2);
            read(s->getPointerOperand(), 3);
            if (s->getAlign().value() < (n + 7) / 8 && n > 8) {
                for (unsigned j = 0; j < (n + 7) / 8; ++j) {
                    scalarStore(2, 3, 0, false);
                    immediate(4, 8);
                    alu(Opcode::ShiftRight, 2, 2, 4);
                    immediate(4, 1);
                    alu(Opcode::Add, 3, 3, 4);
                }
                fence();
            } else {
                scalarStore(2, 3, n <= 8 ? 0 : n <= 16 ? 1 : 2);
            }
            return;
        }
        if (auto* g = dyn_cast<GetElementPtrInst>(&i)) {
            read(g->getPointerOperand(), 2);
            Type* type = g->getSourceElementType();
            unsigned index = 0;
            for (auto iter = g->idx_begin(); iter != g->idx_end(); ++iter, ++index) {
                Value* offset = iter->get();
                uint32_t scale;
                if (index == 0) {
                    scale = dl.getTypeAllocSize(type);
                } else if (auto* st = dyn_cast<StructType>(type)) {
                    auto* c = dyn_cast<ConstantInt>(offset);
                    if (!c) {
                        fail("dynamic struct index");
                    }
                    unsigned field = c->getZExtValue();
                    immediate(3, dl.getStructLayout(st)->getElementOffset(field));
                    alu(Opcode::Add, 2, 2, 3);
                    type = st->getElementType(field);
                    continue;
                } else if (auto* ar = dyn_cast<ArrayType>(type)) {
                    type = ar->getElementType();
                    scale = dl.getTypeAllocSize(type);
                } else {
                    fail("unsupported GEP element type");
                }
                read(offset, 3);
                immediate(4, scale);
                alu(Opcode::Multiply, 3, 3, 4);
                alu(Opcode::Add, 2, 2, 3);
            }
            write(&i);
            return;
        }
        if (auto* c = dyn_cast<CastInst>(&i)) {
            if (c->getOpcode() != Instruction::Trunc && c->getOpcode() != Instruction::ZExt &&
                c->getOpcode() != Instruction::SExt && c->getOpcode() != Instruction::PtrToInt &&
                c->getOpcode() != Instruction::IntToPtr && c->getOpcode() != Instruction::BitCast) {
                fail("unsupported cast");
            }
            read(c->getOperand(0), 2);
            if (c->getOpcode() == Instruction::SExt) {
                signExtend(2, width(c->getOperand(0)->getType()));
            }
            normalize(2, width(c->getType()));
            write(&i);
            if (width(c->getType()) == 64) {
                if (width(c->getOperand(0)->getType()) == 64) {
                    read(c->getOperand(0), 2, 1);
                } else if (c->getOpcode() == Instruction::SExt) {
                    immediate(4, 31);
                    alu(Opcode::ShiftArithmetic, 2, 2, 4);
                } else {
                    immediate(2, 0);
                }
                write(&i, 2, 1);
            }
            return;
        }
        if (auto* cmp = dyn_cast<ICmpInst>(&i)) {
            if (width(cmp->getOperand(0)->getType()) == 64) {
                auto pred = cmp->getPredicate();
                read(cmp->getOperand(0), 2, 1);
                read(cmp->getOperand(1), 3, 1);
                if (pred == ICmpInst::ICMP_EQ || pred == ICmpInst::ICMP_NE) {
                    alu(Opcode::Xor, 2, 2, 3);
                    read(cmp->getOperand(0), 3);
                    read(cmp->getOperand(1), 4);
                    alu(Opcode::Xor, 3, 3, 4);
                    alu(Opcode::Or, 2, 2, 3);
                    immediate(3, 0);
                    alu(Opcode::LessUnsigned, 2, 3, 2);
                    if (pred == ICmpInst::ICMP_EQ) {
                        immediate(3, 1);
                        alu(Opcode::Xor, 2, 2, 3);
                    }
                } else {
                    auto low = unique("compare64.low"), done = unique("compare64.done");
                    bool swap = pred == ICmpInst::ICMP_UGT || pred == ICmpInst::ICMP_SGT ||
                                pred == ICmpInst::ICMP_ULE || pred == ICmpInst::ICMP_SLE;
                    alu(Opcode::Xor, 4, 2, 3);
                    jump(low, Opcode::BranchZero, 4);
                    alu(cmp->isSigned() ? Opcode::LessSigned : Opcode::LessUnsigned, 2,
                        swap ? 3 : 2, swap ? 2 : 3);
                    jump(done);
                    mark(low);
                    read(cmp->getOperand(0), 2);
                    read(cmp->getOperand(1), 3);
                    alu(Opcode::LessUnsigned, 2, swap ? 3 : 2, swap ? 2 : 3);
                    mark(done);
                    if (pred == ICmpInst::ICMP_UGE || pred == ICmpInst::ICMP_SGE ||
                        pred == ICmpInst::ICMP_ULE || pred == ICmpInst::ICMP_SLE) {
                        immediate(3, 1);
                        alu(Opcode::Xor, 2, 2, 3);
                    }
                }
                write(&i);
                return;
            }
            read(cmp->getOperand(0), 2);
            read(cmp->getOperand(1), 3);
            auto pred = cmp->getPredicate();
            if (cmp->isSigned()) {
                signExtend(2, width(cmp->getOperand(0)->getType()));
                signExtend(3, width(cmp->getOperand(1)->getType()));
            }
            if (pred == ICmpInst::ICMP_EQ || pred == ICmpInst::ICMP_NE) {
                alu(Opcode::Xor, 2, 2, 3);
                immediate(3, 0);
                alu(Opcode::LessUnsigned, 2, 3, 2);
                if (pred == ICmpInst::ICMP_EQ) {
                    immediate(3, 1);
                    alu(Opcode::Xor, 2, 2, 3);
                }
            } else {
                bool swap = pred == ICmpInst::ICMP_UGT || pred == ICmpInst::ICMP_SGT ||
                            pred == ICmpInst::ICMP_ULE || pred == ICmpInst::ICMP_SLE;
                alu(cmp->isSigned() ? Opcode::LessSigned : Opcode::LessUnsigned, 2, swap ? 3 : 2,
                    swap ? 2 : 3);
                if (pred == ICmpInst::ICMP_UGE || pred == ICmpInst::ICMP_SGE ||
                    pred == ICmpInst::ICMP_ULE || pred == ICmpInst::ICMP_SLE) {
                    immediate(3, 1);
                    alu(Opcode::Xor, 2, 2, 3);
                }
            }
            write(&i);
            return;
        }
        if (auto* s = dyn_cast<SelectInst>(&i)) {
            if (width(s->getType()) == 64) {
                auto no = unique("select64.no"), done = unique("select64.done");
                read(s->getCondition(), 2);
                jump(no, Opcode::BranchZero, 2);
                for (unsigned part = 0; part < 2; ++part) {
                    read(s->getTrueValue(), 2, part);
                    write(&i, 2, part);
                }
                jump(done);
                mark(no);
                for (unsigned part = 0; part < 2; ++part) {
                    read(s->getFalseValue(), 2, part);
                    write(&i, 2, part);
                }
                mark(done);
                return;
            }
            auto no = unique("select.no"), done = unique("select.done");
            read(s->getCondition(), 2);
            jump(no, Opcode::BranchZero, 2);
            read(s->getTrueValue(), 2);
            jump(done);
            mark(no);
            read(s->getFalseValue(), 2);
            mark(done);
            write(&i);
            return;
        }
        if (auto* b = dyn_cast<BranchInst>(&i)) {
            if (b->isUnconditional()) {
                edge(i.getParent(), b->getSuccessor(0));
            } else {
                auto no = unique("branch.no");
                read(b->getCondition(), 2);
                jump(no, Opcode::BranchZero, 2);
                edge(i.getParent(), b->getSuccessor(0));
                mark(no);
                edge(i.getParent(), b->getSuccessor(1));
            }
            return;
        }
        if (auto* sw = dyn_cast<SwitchInst>(&i)) {
            for (auto c : sw->cases()) {
                auto next = unique("switch.next");
                read(sw->getCondition(), 2);
                immediate(3, c.getCaseValue()->getZExtValue());
                alu(Opcode::Xor, 2, 2, 3);
                if (width(sw->getCondition()->getType()) == 64) {
                    read(sw->getCondition(), 3, 1);
                    immediate(4, c.getCaseValue()->getZExtValue() >> 32);
                    alu(Opcode::Xor, 3, 3, 4);
                    alu(Opcode::Or, 2, 2, 3);
                }
                auto yes = unique("switch.yes");
                jump(yes, Opcode::BranchZero, 2);
                jump(next);
                mark(yes);
                edge(i.getParent(), c.getCaseSuccessor());
                mark(next);
            }
            edge(i.getParent(), sw->getDefaultDest());
            return;
        }
        if (auto* c = dyn_cast<CallBase>(&i)) {
            if (!isa<CallInst>(i)) {
                fail("exceptional/indirect control flow is unsupported");
            }
            emitCall(*c);
            return;
        }
        if (auto* r = dyn_cast<ReturnInst>(&i)) {
            if (taskRoots.count(i.getFunction())) {
                immediate(2, 0);
                out.emit(Opcode::TaskFinish, 2);
                return;
            }
            if (auto* v = r->getReturnValue()) {
                for (unsigned part = 0; part < valueWords(v->getType()); ++part) {
                    read(v, 2, part);
                    if (i.getFunction() == entryFunction) {
                        immediate(5, resultAddress + part * 4);
                    } else {
                        frameAddress(5, returnOffset() + part * 4);
                    }
                    scalarStore(2, 5);
                }
            }
            out.emit(i.getFunction() == entryFunction ? Opcode::Halt : Opcode::Return);
            return;
        }
        if (auto* f = dyn_cast<FreezeInst>(&i)) {
            for (unsigned part = 0; part < valueWords(f->getType()); ++part) {
                read(f->getOperand(0), 2, part);
                write(&i, 2, part);
            }
            return;
        }
        if (isa<UnreachableInst>(i)) {
            out.code.push_back(0xff);
            return;
        }
        std::string text;
        raw_string_ostream stream(text);
        i.print(stream);
        fail("unsupported IR instruction: " + text);
    }

    void compile(const std::string& entry, const std::string& output) {
        auto* root = module.getFunction(entry);
        if (!root) {
            fail("entry function not found: " + entry);
        }
        if (!root->arg_empty()) {
            fail("kernel entry must have no arguments");
        }
        entryFunction = root;
        unsigned depth = discover(root) - 1;
        if (reentrant) {
            for (auto& f : module) {
                if (f.hasAddressTaken() && !builtin(&f)) {
                    discover(&f);
                }
            }
            if (ordinaryCallees.count(root)) {
                fail("the image entry cannot also be called as an ordinary function");
            }
        }
        for (unsigned t = 0; t < taskEntries.size(); ++t) {
            depth = std::max(depth, discover(taskEntries[t]) - 1);
        }
        for (auto* task : taskEntries) {
            if (task == root || ordinaryCallees.count(task)) {
                fail("task entries cannot also be called as ordinary functions");
            }
        }
        taskMode = !taskEntries.empty();
        if (!reentrant && depth > 2 * (bits / 32)) {
            fail("call depth exceeds two reserved return-address registers");
        }
        layout();
        if (taskMode || reentrant) {
            immediate(7, bootFrameBase);
        }
        if (depth || reentrant) {
            out.emit(Opcode::StackLimit, 0, 0, 0, 2 * (bits / 32));
        }
        jump(functionLabel(root));
        out.emit(Opcode::Halt);
        out.align();
        idleAddress = out.code.size();
        out.emit(Opcode::Halt);
        for (auto* f : functions) {
            currentFunction = f;
            mark(functionLabel(f));
            if (taskRoots.count(f)) {
                taskFramePrologue();
            }
            if (dynamicCursors.count(f)) {
                immediate(2, 0);
                frameAddress(5, dynamicCursors.at(f));
                scalarStore(2, 5);
            }
            if (f->isVarArg()) {
                for (unsigned word = 0; word < mailboxWords; ++word) {
                    frameAddress(5, mailbox + word * 4);
                    scalarLoad(2, 5);
                    frameAddress(5, varargSlots.at(f) + word * 4);
                    scalarStore(2, 5);
                }
            }
            unsigned arg = 0;
            for (auto& a : f->args()) {
                for (unsigned part = 0; part < valueWords(a.getType()); ++part) {
                    frameAddress(5, mailbox + arg++ * 4);
                    scalarLoad(2, 5);
                    write(&a, 2, part);
                }
            }
            for (auto& bb : *f) {
                mark(labels.at(&bb));
                for (auto& i : bb) {
                    instruction(i);
                }
            }
        }
        out.align();
        out.code.resize(out.code.size() + bytes, 0); // readable prefetch guard
        if (out.code.size() > dataBase) {
            fail("generated code overlaps static data; increase memory size");
        }
        for (auto& fix : fixups) {
            auto it = positions.find(fix.label);
            if (it == positions.end()) {
                fail("unresolved code label");
            }
            uint32_t word = fix.byteAddress ? it->second : it->second / bytes;
            for (unsigned i = 0; i < 4; ++i) {
                out.code.at(fix.offset + i) = word >> (8 * i);
            }
        }
        for (auto& fix : dataFixups) {
            auto it = positions.find(fix.label);
            if (it == positions.end()) {
                fail("unresolved function address in global initializer: " + fix.label);
            }
            for (unsigned i = 0; i < 4; ++i) {
                image.at(fix.offset + i) = it->second >> (8 * i);
            }
        }
        std::copy(out.code.begin(), out.code.end(), image.begin());
        std::ofstream binary(output, std::ios::binary);
        if (!binary) {
            fail("cannot open output image");
        }
        auto field = [&](uint32_t x) {
            for (unsigned i = 0; i < 4; ++i) {
                binary.put(x >> (8 * i));
            }
        };
        for (uint32_t x : {0x58434345u, 1u, bits, regs, banks, words, 0u, idleAddress,
                           resultAddress, uint32_t(image.size())}) {
            field(x);
        }
        binary.write(reinterpret_cast<const char*>(image.data()), image.size());
        if (!binary) {
            fail("image write failed");
        }
        std::ofstream map(output + ".map");
        std::string callDepth = reentrant ? "dynamic" : std::to_string(depth);
        map << "code_bytes " << out.code.size() << "\ndata_bytes " << cursor - dataBase
            << "\ncall_depth " << callDepth << "\nreturn_slots " << 2 * (bits / 32)
            << "\nmailbox_words " << mailboxWords << "\ncopy_instructions " << copyCount << '\n';
        if (reentrant) {
            map << "reentrant 1\nactivation_bytes " << frameStride << "\ncontext_bytes "
                << contextStride << '\n';
        }
        if (taskMode) {
            map << "task_frame_base " << frameBase << "\ntask_frame_stride " << contextStride
                << "\ntask_contexts 33\nboot_frame_base " << bootFrameBase << '\n';
        }
        for (auto* f : functions) {
            map << "function " << f->getName().str() << ' ' << positions.at(functionLabel(f))
                << '\n';
            if (reentrant) {
                map << "activation " << f->getName().str() << ' ' << activationBytes.at(f) << '\n';
            }
        }
        map << "user_data_begin " << userDataBegin << "\nuser_data_end " << userDataEnd
            << "\nuser_heap_end " << userHeapEnd << '\n';
        for (auto& g : module.globals()) {
            map << "global " << g.getName().str() << ' ' << objects.at(&g) << '\n';
        }
        std::cout << "Extreme: " << out.code.size() << " code bytes, " << cursor - dataBase
                  << " static data bytes, call depth " << callDepth << ", COPY instructions "
                  << copyCount << '\n';
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 8 && !(argc == 9 && std::string(argv[8]) == "--reentrant")) {
            std::cerr << "usage: extreme-codegen input.ll output.ecx bits registers banks "
                         "bank_words entry [--reentrant]\n";
            return 2;
        }
        LLVMContext context;
        SMDiagnostic error;
        auto module = parseIRFile(argv[1], error, context);
        if (!module) {
            error.print(argv[0], errs());
            return 1;
        }
        if (verifyModule(*module, &errs())) {
            return 1;
        }
        lowerLibcContexts(*module);
        lowerSoftFloat(*module);
        lowerOverflow(*module);
        lowerInteger64(*module);
        if (verifyModule(*module, &errs())) {
            return 1;
        }
        Backend backend(*module, std::stoul(argv[3]), std::stoul(argv[4]), std::stoul(argv[5]),
                        std::stoul(argv[6]));
        backend.reentrant = argc == 9;
        backend.compile(argv[7], argv[2]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Extreme compiler: " << e.what() << '\n';
        return 1;
    }
}
