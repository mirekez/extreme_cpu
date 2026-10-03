// LLVM IR -> Extreme bytecode. The frontend supplies an explicit ILP32 ABI.
// This deliberately small backend uses static SSA spill slots, not a data stack.
#include "../arch/instruction.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
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
    std::vector<Function*> functions;
    std::set<Function*> visiting, visited;
    std::map<Function*, unsigned> depths;
    Function* entryFunction = nullptr;
    std::set<Function*> taskRoots, ordinaryCallees;
    std::vector<Function*> taskEntries;
    bool taskMode = false, frameLayout = false;
    uint32_t frameCursor = 0, frameBase = 0, frameStride = 0, returnSave = 0;
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
        mailbox = allocate(64, bytes);
        resultAddress = mailbox + 32;
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
        if (t->isPointerTy()) {
            return 32;
        }
        if (t->isIntegerTy() && t->getIntegerBitWidth() <= 32) {
            return t->getIntegerBitWidth();
        }
        fail("unsupported value type: only scalar integers up to 32 bits and pointers are "
             "currently legal");
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
        if (taskMode) {
            alu(Opcode::Add, r, r, 7);
        }
    }

    uint32_t returnOffset() {
        return mailbox + 32;
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
        for (unsigned n = frameStride; n > 1; n >>= 1) {
            ++shift;
        }
        immediate(6, shift);
        alu(Opcode::ShiftLeft, 7, 7, 6);
        immediate(6, frameBase);
        alu(Opcode::Add, 7, 7, 6);
        if (depths.at(currentFunction) > 1) {
            out.emit(Opcode::StackLimit, 0, 0, 0, 2 * (bits / 32));
        }
    }

    Function* currentFunction = nullptr;

    bool taskBuiltin(Function* f) {
        return f->getName().starts_with("__extreme_task_");
    }

#ifdef EC_SIMD
    bool simdBuiltin(Function* f) {
        return f->getName().starts_with("__extreme_simd_");
    }

    void emitSimd(CallBase& call) {
        auto name = call.getCalledFunction()->getName();
        bool splat = name == "__extreme_simd_splat32";
        if (!call.getType()->isVoidTy() || call.arg_size() != (splat ? 2 : 3) ||
            !call.getArgOperand(0)->getType()->isPointerTy() ||
            (splat ? !call.getArgOperand(1)->getType()->isIntegerTy(32)
                   : (!call.getArgOperand(1)->getType()->isPointerTy() ||
                      !call.getArgOperand(2)->getType()->isPointerTy()))) {
            fail("invalid SIMD intrinsic signature");
        }
        Opcode opcode;
        if (name == "__extreme_simd_add32") {
            opcode = Opcode::SimdAdd32;
        } else if (name == "__extreme_simd_sub32") {
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
        } else {
            fail("unknown SIMD intrinsic: " + name.str());
        }
        // r0/r1 hold returns and r7 holds the task frame base. Keep them intact.
        read(call.getArgOperand(0), 6);
        read(call.getArgOperand(1), 2);
        if (!splat) {
            read(call.getArgOperand(2), 3);
        }
        fence();
        if (!splat) {
            out.emit(Opcode::Load, 2, 2);
            out.emit(Opcode::Load, 3, 3);
        }
        out.emit(opcode, 4, 2, 3);
        out.emit(Opcode::Store, 4, 6);
        fence();
    }
#endif

    Function* taskEntry(CallBase& call) {
        if (call.arg_size() != 3) {
            fail("task issue requires id, entry, and predecessor mask");
        }
        auto* f = dyn_cast<Function>(call.getArgOperand(1)->stripPointerCasts());
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
            if (e->isCast()) {
                return constant(cast<Constant>(e->getOperand(0)));
            }
            if (e->getOpcode() == Instruction::GetElementPtr) {
                APInt offset(32, 0);
                if (!cast<GEPOperator>(e)->accumulateConstantOffset(dl, offset)) {
                    fail("nonconstant initializer GEP");
                }
                return constant(cast<Constant>(e->getOperand(0))) + uint32_t(offset.getZExtValue());
            }
        }
        fail("unsupported constant/initializer");
    }

    void initialize(Constant* c, Type* t, uint32_t address) {
        if (isa<ConstantAggregateZero>(c) || isa<UndefValue>(c) || isa<PoisonValue>(c)) {
            return;
        }
        if (t->isIntegerTy() || t->isPointerTy()) {
            unsigned n = dl.getTypeStoreSize(t);
            if (n > 4) {
                fail("64-bit/global floating initializers are not yet supported");
            }
            uint32_t v = constant(c);
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

    void read(Value* v, unsigned r) {
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
        frameAddress(5, slots.at(v));
        scalarLoad(r, 5);
    }

    void write(Value* v, unsigned r = 2) {
        frameAddress(5, slots.at(v));
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
#ifdef EC_SIMD
        if (simdBuiltin(f)) {
            return true;
        }
#endif
        return taskBuiltin(f) || f->isIntrinsic() || f->getName() == "memcpy" ||
               f->getName() == "memset";
    }

    unsigned discover(Function* f) {
        if (visiting.count(f)) {
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
                    auto* target = call->getCalledFunction();
                    if (!target) {
                        fail("indirect calls are not yet supported by the static-frame compiler "
                             "ABI");
                    }
                    if (target->getName() == "__extreme_task_issue") {
                        auto* task = taskEntry(*call);
                        if (taskRoots.insert(task).second) {
                            taskEntries.push_back(task);
                        }
                    }
                    if (!builtin(target)) {
                        ordinaryCallees.insert(target);
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

    void layout() {
        for (auto& g : module.globals()) {
            if (g.getName() == "llvm.global_ctors" || g.getName() == "llvm.global_dtors") {
                fail("dynamic global constructors/destructors are unsupported");
            }
            if (g.isDeclaration()) {
                fail("unresolved global: " + g.getName().str());
            }
            if (g.isThreadLocal()) {
                fail("thread-local storage is unsupported");
            }
            objects[&g] =
                allocate(dl.getTypeAllocSize(g.getValueType()), g.getAlign().valueOrOne().value());
        }
        for (auto& g : module.globals()) {
            initialize(g.getInitializer(), g.getValueType(), objects.at(&g));
        }
        if (taskMode) {
            frameLayout = true;
            mailbox = allocate(64, bytes);
            returnSave = allocate(2 * bytes, bytes);
        }
        for (auto* f : functions) {
            if (f->isVarArg() || f->arg_size() > 8) {
                fail("variadic functions or more than eight scalar arguments are unsupported");
            }
            for (auto& a : f->args()) {
                width(a.getType());
                slots[&a] = allocate(4);
            }
            for (auto& bb : *f) {
                labels[&bb] = unique(f->getName().str() + ".block");
                for (auto& i : bb) {
                    if (auto* a = dyn_cast<AllocaInst>(&i)) {
                        auto* n = dyn_cast<ConstantInt>(a->getArraySize());
                        if (!n) {
                            fail("dynamic alloca requires a data stack and is unsupported");
                        }
                        objects[a] =
                            allocate(dl.getTypeAllocSize(a->getAllocatedType()) * n->getZExtValue(),
                                     a->getAlign().value());
                    } else if (!i.getType()->isVoidTy()) {
                        width(i.getType());
                        slots[&i] = allocate(4);
                        if (isa<PHINode>(i)) {
                            phiTemps[&i] = allocate(4);
                        }
                    }
                }
            }
        }
        if (taskMode) {
            frameLayout = false;
            frameStride = bytes;
            while (frameStride < frameCursor) {
                frameStride *= 2;
            }
            frameBase = allocate(uint64_t(frameStride) * 33, bytes);
        }
    }

    void edge(BasicBlock* from, BasicBlock* to) {
        // All incoming values are saved before any PHI destination is changed.
        for (auto& phi : to->phis()) {
            read(phi.getIncomingValueForBlock(from), 2);
            frameAddress(5, phiTemps.at(&phi));
            scalarStore(2, 5);
        }
        for (auto& phi : to->phis()) {
            frameAddress(5, phiTemps.at(&phi));
            scalarLoad(2, 5);
            write(&phi);
        }
        jump(labels.at(to));
    }

    void copy(CallBase& call) {
        Value* dest = call.getArgOperand(0);
        Value* src = call.getArgOperand(1);
        Value* size = call.getArgOperand(2);
        if (auto* mi = dyn_cast<MemIntrinsic>(&call)) {
            if (mi->isVolatile()) {
                fail("volatile memory intrinsics are unsupported");
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
            if (taskMode) {
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
            if (taskMode) {
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

    void memsetCall(CallBase& call) {
        if (auto* mi = dyn_cast<MemIntrinsic>(&call)) {
            if (mi->isVolatile()) {
                fail("volatile memset is unsupported");
            }
        }
        read(call.getArgOperand(0), 2);
        read(call.getArgOperand(1), 3);
        read(call.getArgOperand(2), 4);
        if (!call.getType()->isVoidTy()) {
            write(&call, 2);
        }
        auto loop = unique("set.loop"), done = unique("set.done");
        mark(loop);
        jump(done, Opcode::BranchZero, 4);
        scalarStore(3, 2, 0, false);
        immediate(5, 1);
        alu(Opcode::Add, 2, 2, 5);
        alu(Opcode::Sub, 4, 4, 5);
        jump(loop);
        mark(done);
        fence();
    }

    void emitCall(CallBase& call) {
        auto* f = call.getCalledFunction();
        auto name = f->getName();
#ifdef EC_SIMD
        if (simdBuiltin(f)) {
            emitSimd(call);
            return;
        }
#endif
        if (taskBuiltin(f)) {
            emitTask(call);
            return;
        }
        if (name.starts_with("llvm.lifetime.") || name.starts_with("llvm.assume") ||
            name.starts_with("llvm.dbg.")) {
            return;
        }
        if (name.starts_with("llvm.memcpy.") || name == "memcpy") {
            copy(call);
            return;
        }
        if (name.starts_with("llvm.memset.") || name == "memset") {
            memsetCall(call);
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
        if (f->isIntrinsic()) {
            fail("unsupported LLVM intrinsic: " + name.str());
        }
        unsigned i = 0;
        for (auto& arg : call.args()) {
            read(arg.get(), 2);
            frameAddress(5, mailbox + 4 * i++);
            scalarStore(2, 5);
        }
        jump(functionLabel(f), Opcode::Call);
        if (!call.getType()->isVoidTy()) {
            frameAddress(5, returnOffset());
            scalarLoad(2, 5);
            write(&call);
        }
    }

    void instruction(Instruction& i) {
        if (isa<PHINode>(i) || isa<AllocaInst>(i)) {
            return;
        }
        if (auto* b = dyn_cast<BinaryOperator>(&i)) {
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
            unsigned n = width(s->getValueOperand()->getType());
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
            return;
        }
        if (auto* cmp = dyn_cast<ICmpInst>(&i)) {
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
                read(v, 2);
                if (i.getFunction() == entryFunction) {
                    immediate(5, resultAddress);
                } else {
                    frameAddress(5, returnOffset());
                }
                scalarStore(2, 5);
            }
            out.emit(i.getFunction() == entryFunction ? Opcode::Halt : Opcode::Return);
            return;
        }
        if (auto* f = dyn_cast<FreezeInst>(&i)) {
            read(f->getOperand(0), 2);
            write(&i);
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
        for (unsigned t = 0; t < taskEntries.size(); ++t) {
            depth = std::max(depth, discover(taskEntries[t]) - 1);
        }
        for (auto* task : taskEntries) {
            if (task == root || ordinaryCallees.count(task)) {
                fail("task entries cannot also be called as ordinary functions");
            }
        }
        taskMode = !taskEntries.empty();
        if (depth > 2 * (bits / 32)) {
            fail("call depth exceeds two reserved return-address registers");
        }
        layout();
        if (taskMode) {
            immediate(7, frameBase + 32 * frameStride);
        }
        if (depth) {
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
            unsigned arg = 0;
            for (auto& a : f->args()) {
                frameAddress(5, mailbox + arg++ * 4);
                scalarLoad(2, 5);
                write(&a);
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
        map << "code_bytes " << out.code.size() << "\ndata_bytes " << cursor - dataBase
            << "\ncall_depth " << depth << "\nreturn_slots " << 2 * (bits / 32)
            << "\ncopy_instructions " << copyCount << '\n';
        if (taskMode) {
            map << "task_frame_base " << frameBase << "\ntask_frame_stride " << frameStride
                << "\ntask_contexts 33\n";
        }
        for (auto* f : functions) {
            map << "function " << f->getName().str() << ' ' << positions.at(functionLabel(f))
                << '\n';
        }
        for (auto& g : module.globals()) {
            map << "global " << g.getName().str() << ' ' << objects.at(&g) << '\n';
        }
        std::cout << "Extreme: " << out.code.size() << " code bytes, " << cursor - dataBase
                  << " static data bytes, call depth " << depth << ", COPY instructions "
                  << copyCount << '\n';
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 8) {
            std::cerr << "usage: extreme-codegen input.ll output.ecx bits registers banks "
                         "bank_words entry\n";
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
        Backend backend(*module, std::stoul(argv[3]), std::stoul(argv[4]), std::stoul(argv[5]),
                        std::stoul(argv[6]));
        backend.compile(argv[7], argv[2]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Extreme compiler: " << e.what() << '\n';
        return 1;
    }
}
