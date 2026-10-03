#pragma once
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <stdexcept>
#include <vector>

// Retain i64 storage/ABI values, but express arithmetic through scalar-lane
// helpers before discovering the call graph and reserving activation storage.
inline void lowerInteger64(llvm::Module& module) {
    using namespace llvm;
    // Expand wide bit-count and funnel intrinsics first, so their generated
    // i64 shifts are included in the ordinary arithmetic legalization below.
    std::vector<IntrinsicInst*> intrinsics;
    for (auto& function : module) {
        for (auto& block : function) {
            for (auto& instruction : block) {
                if (auto* call = dyn_cast<IntrinsicInst>(&instruction)) {
                    if (call->getType()->isIntegerTy(64)) {
                        intrinsics.push_back(call);
                    }
                }
            }
        }
    }
    for (auto* call : intrinsics) {
        IRBuilder<> builder(call);
        auto id = call->getIntrinsicID();
        auto* a = call->getArgOperand(0);
        Value* result = nullptr;
        if (id == Intrinsic::abs) {
            result = builder.CreateSelect(builder.CreateICmpSLT(a, builder.getInt64(0)),
                                          builder.CreateSub(builder.getInt64(0), a), a);
        } else if (id == Intrinsic::ctlz || id == Intrinsic::cttz || id == Intrinsic::ctpop) {
            auto* lo = builder.CreateTrunc(a, builder.getInt32Ty());
            auto* hi = builder.CreateTrunc(builder.CreateLShr(a, 32), builder.getInt32Ty());
            auto count = [&](Value* value) -> Value* {
                if (id == Intrinsic::ctpop) {
                    return builder.CreateIntrinsic(id, {builder.getInt32Ty()}, {value});
                }
                return builder.CreateIntrinsic(id, {builder.getInt32Ty()},
                                               {value, builder.getFalse()});
            };
            if (id == Intrinsic::ctpop) {
                result = builder.CreateAdd(count(lo), count(hi));
            } else {
                auto* first = id == Intrinsic::ctlz ? hi : lo;
                auto* second = id == Intrinsic::ctlz ? lo : hi;
                result = builder.CreateSelect(
                    builder.CreateICmpEQ(first, builder.getInt32(0)),
                    builder.CreateAdd(count(second), builder.getInt32(32)), count(first));
            }
            result = builder.CreateZExt(result, builder.getInt64Ty());
        } else if (id == Intrinsic::fshl || id == Intrinsic::fshr) {
            auto* b = call->getArgOperand(1);
            auto* n = builder.CreateAnd(call->getArgOperand(2), builder.getInt64(63));
            auto* inverse =
                builder.CreateAnd(builder.CreateSub(builder.getInt64(0), n), builder.getInt64(63));
            auto* shifted =
                id == Intrinsic::fshl
                    ? builder.CreateOr(builder.CreateShl(a, n), builder.CreateLShr(b, inverse))
                    : builder.CreateOr(builder.CreateShl(a, inverse), builder.CreateLShr(b, n));
            result = builder.CreateSelect(builder.CreateICmpEQ(n, builder.getInt64(0)),
                                          id == Intrinsic::fshl ? a : b, shifted);
        }
        if (result) {
            call->replaceAllUsesWith(result);
            call->eraseFromParent();
        }
    }
    std::vector<Instruction*> arithmetic;
    for (auto& function : module) {
        for (auto& block : function) {
            for (auto& instruction : block) {
                if (instruction.getType()->isIntegerTy(64) &&
                    (isa<BinaryOperator>(instruction) || isa<IntrinsicInst>(instruction))) {
                    arithmetic.push_back(&instruction);
                }
            }
        }
    }
    for (auto* instruction : arithmetic) {
        IRBuilder<> builder(instruction);
        Value* left = instruction->getOperand(0);
        Value* right = nullptr;
        std::string operation;
        if (auto* binary = dyn_cast<BinaryOperator>(instruction)) {
#ifdef EC_SIMD
            auto opcode = binary->getOpcode();
            auto* count = dyn_cast<ConstantInt>(binary->getOperand(1));
            if (opcode == Instruction::Add || opcode == Instruction::Sub ||
                opcode == Instruction::And || opcode == Instruction::Or ||
                opcode == Instruction::Xor ||
                ((opcode == Instruction::Shl || opcode == Instruction::LShr) && count &&
                 count->equalsInt(32))) {
                continue; // The backend emits paired 32-bit lane operations directly.
            }
#endif
            operation = binary->getOpcodeName();
            right = binary->getOperand(1);
        } else {
            auto* intrinsic = cast<IntrinsicInst>(instruction);
            switch (intrinsic->getIntrinsicID()) {
            case Intrinsic::bswap:
                operation = "bswap";
                right = builder.getInt64(0);
                break;
            case Intrinsic::umin:
            case Intrinsic::umax:
            case Intrinsic::smin:
            case Intrinsic::smax: {
                auto id = intrinsic->getIntrinsicID();
                auto predicate = id == Intrinsic::umin   ? ICmpInst::ICMP_ULT
                                 : id == Intrinsic::umax ? ICmpInst::ICMP_UGT
                                 : id == Intrinsic::smin ? ICmpInst::ICMP_SLT
                                                         : ICmpInst::ICMP_SGT;
                right = intrinsic->getArgOperand(1);
                auto* result =
                    builder.CreateSelect(builder.CreateICmp(predicate, left, right), left, right);
                instruction->replaceAllUsesWith(result);
                instruction->eraseFromParent();
                continue;
            }
            default:
                continue; // The backend diagnoses other unsupported intrinsics.
            }
        }
        auto* helper = module.getFunction("__extreme_i64_" + operation);
        if (!helper || helper->isDeclaration()) {
            throw std::runtime_error("missing 64-bit runtime helper for " + operation);
        }
        IRBuilder<> locals(&*instruction->getFunction()->getEntryBlock().getFirstInsertionPt());
        auto* output = locals.CreateAlloca(builder.getInt64Ty(), nullptr, "wide.result");
        auto* a = locals.CreateAlloca(builder.getInt64Ty(), nullptr, "wide.left");
        auto* b = locals.CreateAlloca(builder.getInt64Ty(), nullptr, "wide.right");
        builder.CreateStore(left, a);
        builder.CreateStore(right, b);
        builder.CreateCall(helper, {output, a, b});
        auto* result = builder.CreateLoad(builder.getInt64Ty(), output);
        instruction->replaceAllUsesWith(result);
        instruction->eraseFromParent();
    }
}
