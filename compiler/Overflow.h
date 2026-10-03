#pragma once
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <stdexcept>
#include <vector>

inline void lowerOverflow(llvm::Module& module) {
    using namespace llvm;
    std::vector<IntrinsicInst*> calls;
    for (auto& f : module) {
        for (auto& b : f) {
            for (auto& i : b) {
                if (auto* call = dyn_cast<IntrinsicInst>(&i)) {
                    if (call->getIntrinsicID() == Intrinsic::umul_with_overflow ||
                        call->getIntrinsicID() == Intrinsic::uadd_with_overflow ||
                        call->getIntrinsicID() == Intrinsic::usub_with_overflow ||
                        call->getIntrinsicID() == Intrinsic::uadd_sat ||
                        call->getIntrinsicID() == Intrinsic::usub_sat ||
                        call->getIntrinsicID() == Intrinsic::sadd_sat ||
                        call->getIntrinsicID() == Intrinsic::ssub_sat ||
                        call->getIntrinsicID() == Intrinsic::ucmp ||
                        call->getIntrinsicID() == Intrinsic::scmp) {
                        calls.push_back(call);
                    }
                }
            }
        }
    }
    for (auto* call : calls) {
        IRBuilder<> builder(call);
        auto* a = call->getArgOperand(0);
        auto* b = call->getArgOperand(1);
        auto* type = cast<IntegerType>(a->getType());
        auto* zero = ConstantInt::get(type, 0);
        auto id = call->getIntrinsicID();
        if (id == Intrinsic::ucmp || id == Intrinsic::scmp) {
            auto* resultType = cast<IntegerType>(call->getType());
            auto* less =
                id == Intrinsic::ucmp ? builder.CreateICmpULT(a, b) : builder.CreateICmpSLT(a, b);
            auto* positive =
                builder.CreateSelect(builder.CreateICmpEQ(a, b), ConstantInt::get(resultType, 0),
                                     ConstantInt::get(resultType, 1));
            call->replaceAllUsesWith(
                builder.CreateSelect(less, ConstantInt::getAllOnesValue(resultType), positive));
            call->eraseFromParent();
            continue;
        }
        if (id == Intrinsic::uadd_sat || id == Intrinsic::usub_sat || id == Intrinsic::sadd_sat ||
            id == Intrinsic::ssub_sat) {
            bool subtract = id == Intrinsic::usub_sat || id == Intrinsic::ssub_sat;
            auto* result = subtract ? builder.CreateSub(a, b) : builder.CreateAdd(a, b);
            Value* overflow;
            Value* saturated;
            if (id == Intrinsic::uadd_sat || id == Intrinsic::usub_sat) {
                overflow =
                    subtract ? builder.CreateICmpULT(a, b) : builder.CreateICmpULT(result, a);
                saturated = subtract ? zero : ConstantInt::getAllOnesValue(type);
            } else {
                auto* signs = builder.CreateXor(a, b);
                if (!subtract) {
                    signs = builder.CreateNot(signs);
                }
                overflow = builder.CreateICmpSLT(
                    builder.CreateAnd(signs, builder.CreateXor(a, result)), zero);
                saturated = builder.CreateSelect(
                    builder.CreateICmpSLT(a, zero),
                    ConstantInt::get(type, APInt::getSignedMinValue(type->getBitWidth())),
                    ConstantInt::get(type, APInt::getSignedMaxValue(type->getBitWidth())));
            }
            call->replaceAllUsesWith(builder.CreateSelect(overflow, saturated, result));
            call->eraseFromParent();
            continue;
        }
        Value* value;
        Value* overflow;
        switch (call->getIntrinsicID()) {
        case Intrinsic::uadd_with_overflow:
            value = builder.CreateAdd(a, b);
            overflow = builder.CreateICmpULT(value, a);
            break;
        case Intrinsic::usub_with_overflow:
            value = builder.CreateSub(a, b);
            overflow = builder.CreateICmpULT(a, b);
            break;
        default: {
            value = builder.CreateMul(a, b);
            auto* nonzero = builder.CreateICmpNE(b, zero);
            auto* divisor = builder.CreateSelect(nonzero, b, ConstantInt::get(type, 1));
            auto* limit = builder.CreateUDiv(ConstantInt::getAllOnesValue(type), divisor);
            overflow = builder.CreateAnd(nonzero, builder.CreateICmpUGT(a, limit));
            break;
        }
        }
        std::vector<ExtractValueInst*> extracts;
        for (auto* user : call->users()) {
            auto* extract = dyn_cast<ExtractValueInst>(user);
            if (!extract || extract->getNumIndices() != 1) {
                throw std::runtime_error("overflow result requires scalar extracts");
            }
            extracts.push_back(extract);
        }
        for (auto* extract : extracts) {
            extract->replaceAllUsesWith(extract->getIndices()[0] == 0 ? value : overflow);
            extract->eraseFromParent();
        }
        call->eraseFromParent();
    }
}
