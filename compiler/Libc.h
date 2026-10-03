#pragma once
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <stdexcept>
#include <vector>

// Nonlocal returns must save the caller's continuation, not a temporary C
// wrapper's activation. Signal-mask work remains an ordinary libc operation.
inline void lowerLibcContexts(llvm::Module& module) {
    using namespace llvm;
    std::vector<CallInst*> calls;
    for (auto& function : module) {
        for (auto& block : function) {
            for (auto& instruction : block) {
                if (auto* call = dyn_cast<CallInst>(&instruction)) {
                    calls.push_back(call);
                }
            }
        }
    }
    for (auto* call : calls) {
        auto* target = dyn_cast<Function>(call->getCalledOperand()->stripPointerCastsAndAliases());
        if (!target) {
            continue;
        }
        auto name = target->getName();
        bool signalSave = name == "sigsetjmp" || name == "__sigsetjmp";
        bool save = signalSave || name == "setjmp" || name == "_setjmp";
        bool signalRestore = name == "siglongjmp";
        bool restore = signalRestore || name == "longjmp" || name == "_longjmp";
        if (!save && !restore) {
            continue;
        }
        IRBuilder<> builder(call);
        auto* pointer = PointerType::getUnqual(module.getContext());
        if (signalSave || signalRestore) {
            auto helper =
                signalSave ? "__extreme_sigsetjmp_prepare" : "__extreme_siglongjmp_prepare";
            auto* function = module.getFunction(helper);
            if (!function || function->isDeclaration()) {
                throw std::runtime_error(std::string("missing libc context helper: ") + helper);
            }
            if (signalSave) {
                builder.CreateCall(function, {call->getArgOperand(0), call->getArgOperand(1)});
            } else {
                builder.CreateCall(function, {call->getArgOperand(0)});
            }
        }
        if (restore) {
            if (auto* prepare = module.getFunction("__extreme_longjmp_prepare");
                prepare && !prepare->isDeclaration()) {
                builder.CreateCall(prepare);
            }
        }
        auto callee =
            save ? module.getOrInsertFunction("__extreme_context_save", builder.getInt32Ty(),
                                              pointer)
                 : module.getOrInsertFunction("__extreme_context_restore", builder.getVoidTy(),
                                              pointer, builder.getInt32Ty());
        CallInst* replacement;
        if (save) {
            replacement = builder.CreateCall(callee, {call->getArgOperand(0)});
            replacement->addFnAttr(Attribute::ReturnsTwice);
        } else {
            replacement =
                builder.CreateCall(callee, {call->getArgOperand(0), call->getArgOperand(1)});
            replacement->addFnAttr(Attribute::NoReturn);
        }
        call->replaceAllUsesWith(replacement);
        call->eraseFromParent();
    }
}
