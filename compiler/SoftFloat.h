#pragma once
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <stdexcept>
#include <vector>

// Optional compiler-rt ABI support. Callers link the required C helper sources;
// float values are represented by their IEEE-754 bits in ordinary scalar lanes.
inline void lowerSoftFloat(llvm::Module& module) {
    using namespace llvm;
    std::vector<Instruction*> instructions;
    for (auto& function : module) {
        for (auto& block : function) {
            for (auto& instruction : block) {
                instructions.push_back(&instruction);
            }
        }
    }
    auto helper = [&](StringRef name) -> Function* {
        auto* function = module.getFunction(name);
        if (!function || function->isDeclaration()) {
            throw std::runtime_error("unsupported floating operation: link compiler-rt " +
                                     name.str());
        }
        return function;
    };
    for (auto* instruction : instructions) {
        IRBuilder<> builder(instruction);
        Value* result = nullptr;
        if (auto* binary = dyn_cast<BinaryOperator>(instruction);
            binary && (binary->getType()->isFloatTy() || binary->getType()->isDoubleTy())) {
            std::string name;
            switch (binary->getOpcode()) {
            case Instruction::FAdd:
                name = "__addsf3";
                break;
            case Instruction::FSub:
                name = "__subsf3";
                break;
            case Instruction::FMul:
                name = "__mulsf3";
                break;
            case Instruction::FDiv:
                name = "__divsf3";
                break;
            default:
                continue;
            }
            if (binary->getType()->isDoubleTy()) {
                name.replace(name.find("sf"), 2, "df");
            }
            result =
                builder.CreateCall(helper(name), {binary->getOperand(0), binary->getOperand(1)});
        } else if (auto* cast = dyn_cast<CastInst>(instruction)) {
            auto* source = cast->getOperand(0);
            std::string name;
            Type* input = source->getType();
            Type* output = cast->getType();
            bool inputFP = input->isFloatTy() || input->isDoubleTy();
            bool outputFP = output->isFloatTy() || output->isDoubleTy();
            if (inputFP && output->isIntegerTy() && output->getIntegerBitWidth() <= 64 &&
                (cast->getOpcode() == Instruction::FPToUI ||
                 cast->getOpcode() == Instruction::FPToSI)) {
                name = cast->getOpcode() == Instruction::FPToUI ? "__fixuns" : "__fix";
                name += input->isFloatTy() ? "sf" : "df";
                name += output->getIntegerBitWidth() <= 32 ? "si" : "di";
            } else if (input->isIntegerTy() && input->getIntegerBitWidth() <= 64 && outputFP &&
                       (cast->getOpcode() == Instruction::UIToFP ||
                        cast->getOpcode() == Instruction::SIToFP)) {
                bool unsignedValue = cast->getOpcode() == Instruction::UIToFP;
                name = unsignedValue ? "__floatunsi" : "__floatsi";
                if (input->getIntegerBitWidth() > 32) {
                    name = unsignedValue ? "__floatundi" : "__floatdi";
                }
                name += output->isFloatTy() ? "sf" : "df";
                if (input->getIntegerBitWidth() < 32) {
                    source = unsignedValue ? builder.CreateZExt(source, builder.getInt32Ty())
                                           : builder.CreateSExt(source, builder.getInt32Ty());
                }
            } else if (cast->getOpcode() == Instruction::FPExt && input->isFloatTy() &&
                       output->isDoubleTy()) {
                name = "__extendsfdf2";
            } else if (cast->getOpcode() == Instruction::FPTrunc && input->isDoubleTy() &&
                       output->isFloatTy()) {
                name = "__truncdfsf2";
            }
            if (!name.empty()) {
                result = builder.CreateCall(helper(name), {source});
                if (result->getType() != output) {
                    result = builder.CreateTrunc(result, output);
                }
            }
        } else if (auto* compare = dyn_cast<FCmpInst>(instruction);
                   compare && (compare->getOperand(0)->getType()->isFloatTy() ||
                               compare->getOperand(0)->getType()->isDoubleTy())) {
            auto* a = compare->getOperand(0);
            auto* b = compare->getOperand(1);
            auto predicate = compare->getPredicate();
            if (predicate == FCmpInst::FCMP_FALSE || predicate == FCmpInst::FCMP_TRUE) {
                result = builder.getInt1(predicate == FCmpInst::FCMP_TRUE);
            } else {
                auto* unordered = builder.CreateICmpNE(
                    builder.CreateCall(
                        helper(a->getType()->isFloatTy() ? "__unordsf2" : "__unorddf2"), {a, b}),
                    builder.getInt32(0));
                if (predicate == FCmpInst::FCMP_UNO || predicate == FCmpInst::FCMP_ORD) {
                    result =
                        predicate == FCmpInst::FCMP_UNO ? unordered : builder.CreateNot(unordered);
                } else {
                    auto* relation = builder.CreateCall(
                        helper(a->getType()->isFloatTy() ? "__lesf2" : "__ledf2"), {a, b});
                    CmpInst::Predicate integerPredicate;
                    switch (predicate) {
                    case FCmpInst::FCMP_OEQ:
                    case FCmpInst::FCMP_UEQ:
                        integerPredicate = ICmpInst::ICMP_EQ;
                        break;
                    case FCmpInst::FCMP_ONE:
                    case FCmpInst::FCMP_UNE:
                        integerPredicate = ICmpInst::ICMP_NE;
                        break;
                    case FCmpInst::FCMP_OLT:
                    case FCmpInst::FCMP_ULT:
                        integerPredicate = ICmpInst::ICMP_SLT;
                        break;
                    case FCmpInst::FCMP_OLE:
                    case FCmpInst::FCMP_ULE:
                        integerPredicate = ICmpInst::ICMP_SLE;
                        break;
                    case FCmpInst::FCMP_OGT:
                    case FCmpInst::FCMP_UGT:
                        integerPredicate = ICmpInst::ICMP_SGT;
                        break;
                    default:
                        integerPredicate = ICmpInst::ICMP_SGE;
                        break;
                    }
                    auto* comparison =
                        builder.CreateICmp(integerPredicate, relation, builder.getInt32(0));
                    result = CmpInst::isUnordered(predicate)
                                 ? builder.CreateOr(unordered, comparison)
                                 : builder.CreateAnd(builder.CreateNot(unordered), comparison);
                }
            }
        } else if (instruction->getOpcode() == Instruction::FNeg &&
                   (instruction->getType()->isFloatTy() || instruction->getType()->isDoubleTy())) {
            bool wide = instruction->getType()->isDoubleTy();
            auto* integer = wide ? builder.getInt64Ty() : builder.getInt32Ty();
            auto* bits = builder.CreateBitCast(instruction->getOperand(0), integer);
            auto* sign = ConstantInt::get(integer, uint64_t(1) << (wide ? 63 : 31));
            result = builder.CreateBitCast(builder.CreateXor(bits, sign), instruction->getType());
        }
        if (auto* intrinsic = dyn_cast<IntrinsicInst>(instruction);
            intrinsic &&
            (intrinsic->getType()->isFloatTy() || intrinsic->getType()->isDoubleTy())) {
            bool wide = intrinsic->getType()->isDoubleTy();
            auto* integer = wide ? builder.getInt64Ty() : builder.getInt32Ty();
            uint64_t signMask = uint64_t(1) << (wide ? 63 : 31);
            if (intrinsic->getIntrinsicID() == Intrinsic::fmuladd) {
                // llvm.fmuladd permits an unfused result; llvm.fma does not.
                auto* product =
                    builder.CreateCall(helper(wide ? "__muldf3" : "__mulsf3"),
                                       {intrinsic->getArgOperand(0), intrinsic->getArgOperand(1)});
                result = builder.CreateCall(helper(wide ? "__adddf3" : "__addsf3"),
                                            {product, intrinsic->getArgOperand(2)});
            } else if (intrinsic->getIntrinsicID() == Intrinsic::fabs ||
                       intrinsic->getIntrinsicID() == Intrinsic::copysign) {
                auto* bits = builder.CreateBitCast(intrinsic->getArgOperand(0), integer);
                Value* magnitude = builder.CreateAnd(bits, ConstantInt::get(integer, signMask - 1));
                if (intrinsic->getIntrinsicID() == Intrinsic::copysign) {
                    auto* sign = builder.CreateBitCast(intrinsic->getArgOperand(1), integer);
                    magnitude = builder.CreateOr(
                        magnitude, builder.CreateAnd(sign, ConstantInt::get(integer, signMask)));
                }
                result = builder.CreateBitCast(magnitude, intrinsic->getType());
            } else {
                std::string name;
                switch (intrinsic->getIntrinsicID()) {
                case Intrinsic::ceil:
                    name = "ceil";
                    break;
                case Intrinsic::floor:
                    name = "floor";
                    break;
                case Intrinsic::trunc:
                    name = "trunc";
                    break;
                case Intrinsic::round:
                    name = "round";
                    break;
                default:
                    break;
                }
                if (!name.empty()) {
                    if (!wide) {
                        name += "f";
                    }
                    result = builder.CreateCall(helper(name), {intrinsic->getArgOperand(0)});
                }
            }
        }
        if (result) {
            instruction->replaceAllUsesWith(result);
            instruction->eraseFromParent();
        }
    }
}
