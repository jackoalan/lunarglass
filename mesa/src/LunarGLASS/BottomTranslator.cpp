//===- BottomTranslator.cpp - Translate bottom IR to Generic IR -----------===//
//
// LunarGLASS: An Open Modular Shader Compiler Architecture
// Copyright (C) 2010-2011 LunarG, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; version 2 of the
// License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.
//
//===----------------------------------------------------------------------===//
//
// Author: John Kessenich, LunarG
// Author: Michael Ilseman, LunarG
//
// Translate bottom IR to another IR through an LLVM module pass
//
//===----------------------------------------------------------------------===//

// LLVM includes
#include "llvm/DerivedTypes.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Intrinsics.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/PassManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <string>
#include <map>
#include <vector>

// LunarGLASS includes
#include "Exceptions.h"
#include "LunarGLASSBackend.h"
#include "Manager.h"

namespace {
    class BottomTranslator {
    public:
        BottomTranslator(gla::BackEndTranslator* t)
            : backEndTranslator(t)
            , loopInfo(NULL)
        { }


        ~BottomTranslator()
        { }

        // Translate from LLVM CFG style to structured style.
        void addFlowControl(const llvm::Instruction*, bool);

        void declarePhiCopies(const llvm::Function*);

        void setLoopInfo(llvm::LoopInfo* li) { loopInfo = li; }

        void addPhiCopies(const llvm::Instruction*);

    protected:
        gla::BackEndTranslator* backEndTranslator;
        std::vector<const llvm::Value*> flowControl;

        llvm::LoopInfo* loopInfo;
    };

    // Code Generation Class
    class CodeGeneration : public llvm::ModulePass {
    public:
        CodeGeneration() : ModulePass(ID)
        { }

        // Module Pass implementation
        static char ID;
        bool runOnModule(llvm::Module&);
        void print(std::ostream&, const llvm::Module*) const;
        void getAnalysisUsage(llvm::AnalysisUsage&) const;

        void setBackEndTranslator(gla::BackEndTranslator* bet) { backEndTranslator = bet; }
        void setBackEnd(gla::BackEnd* be)                      { backEnd = be; }
        void setBottomTranslator(BottomTranslator* bt)         { translator = bt; }

        // Given a loop block, handle it. Dispatches to other methods and ends
        // up handling all blocks in the loop, updating loopBlockSet as it goes.
        void handleLoopBlock(const llvm::BasicBlock*, bool lastBlock);

    private:
        gla::BackEndTranslator* backEndTranslator;
        gla::BackEnd* backEnd;

        BottomTranslator* translator;

        // Set of blocks belonging to loops that we've already handled
        llvm::SmallPtrSet<const llvm::BasicBlock*,8> loopBlockSet;

        llvm::LoopInfo* loopInfo;

        // Have the back-end output the start of the loop, then output all of
        // the loop's members.
        void handleLoopHeader(const llvm::BasicBlock*, bool lastBlock);

        // Send off all the non-terminating instructions in a basic block to the
        // backend
        void handleNonTerminatingInstructions(const llvm::BasicBlock*, bool lastBlock);

        ~CodeGeneration()
        {
            delete translator;
        }

    };
} // end namespace

void BottomTranslator::addFlowControl(const llvm::Instruction* llvmInstruction, bool removePhiFunctions)
{
    // Translate from LLVM CFG style to structured style. This is done by
    // identifying and handling loops via LoopInfo, and conditionals are handled
    // by using a stack to keep track of what is pending. So far nested loops
    // are not supported

    // Also, translate from SSA form to non-SSA form (remove phi functions).
    // This is done by looking ahead for phi functions and adding copies in the
    // phi-predecessor blocks.

    // Currently, this is done in a fragile way. Only (un-nested) loops are
    // handled via some form of analysis. For all other LLVM branches found, we
    // assume they must be representing if-then-else constructs.

    if (removePhiFunctions) {
        // All branches that branch to a block having phi instructions for that
        // branch need copies inserted.
        addPhiCopies(llvmInstruction);
    }

    // If it's an unconditional branch into a loop header, ignore it and move on
    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(llvmInstruction);
    if (branchInst && branchInst->isUnconditional() && loopInfo->isLoopHeader(branchInst->getSuccessor(0))) {
        return;
    }

    switch (llvmInstruction->getNumOperands()) {
    case 1:
        // We are doing an unconditional branch
        // Assume it is to the merge of the if-then-else or the if-then
        if (flowControl.back() == llvmInstruction->getOperand(0)) {
            // This must be the end of the if block
            backEndTranslator->addEndif();
            flowControl.pop_back();
        } else {
            // This must be the end of a then that has as else
            backEndTranslator->addElse();
            flowControl.pop_back();
            flowControl.push_back(llvmInstruction->getOperand(0));
        }
        break;
    case 3:
        // We are splitting into two children.
        // Assume we are entering an if-then-else statement or if-then statement.
        flowControl.push_back(llvmInstruction->getOperand(1));
        backEndTranslator->addIf(llvmInstruction->getOperand(0));
        break;
    default:
        gla::UnsupportedFunctionality("Flow Control in Bottom IR");
    }
}

void BottomTranslator::declarePhiCopies(const llvm::Function* function)
{
    // basic blocks
    for (llvm::Function::const_iterator bb = function->begin(), E = function->end(); bb != E; ++bb) {

        // instructions in the basic block
        for (llvm::BasicBlock::const_iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
            const llvm::Instruction* llvmInstruction = i;

            if (llvmInstruction->getOpcode() == llvm::Instruction::PHI)
                backEndTranslator->declarePhiCopy(llvmInstruction);
        }
    }
}

void BottomTranslator::addPhiCopies(const llvm::Instruction* llvmInstruction)
{
    // for each child block
    for (unsigned int op = 0; op < llvmInstruction->getNumOperands(); ++op) {

        // get the destination block (not all operands are blocks, but consider each that is)
        const llvm::BasicBlock *phiBlock = llvm::dyn_cast<llvm::BasicBlock>(llvmInstruction->getOperand(op));
        if (! phiBlock)
            continue;

        // for each llvm phi node, add a copy instruction
        for (llvm::BasicBlock::const_iterator i = phiBlock->begin(), e = phiBlock->end(); i != e; ++i) {
            const llvm::Instruction* destInstruction = i;
            const llvm::PHINode *phiNode = llvm::dyn_cast<llvm::PHINode>(destInstruction);

            if (phiNode) {
                // find the operand whose predecessor is us
                // each phi operand takes up two normal operands,
                // so don't directly access operands; use the Index encapsulation
                int predIndex = phiNode->getBasicBlockIndex(llvmInstruction->getParent());
                if (predIndex >= 0) {
                    // then we found ourselves
                    backEndTranslator->addPhiCopy(phiNode, phiNode->getIncomingValue(predIndex));
                }
            }
        }
    }
}

void CodeGeneration::handleNonTerminatingInstructions(const llvm::BasicBlock* bb, bool lastBlock) {
    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && "handleLoopsBlock called with non-branch terminator");

    // Add the non-terminating instructions
    for (llvm::BasicBlock::const_iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
        // Don't handle the terminator
        if (branchInst == i)
            break;

        const llvm::Instruction* llvmInstruction = i;

        if (! (backEnd->getRemovePhiFunctions() && llvmInstruction->getOpcode() == llvm::Instruction::PHI))
            backEndTranslator->add(llvmInstruction, lastBlock);

    }
}

void CodeGeneration::handleLoopHeader(const llvm::BasicBlock* bb, bool lastBlock)
{
    // Add the loop
    backEndTranslator->addLoop(NULL);

    llvm::Loop* loop = loopInfo->getLoopFor(bb);
    assert(loop && "handleLoopHeader called on non-loop");

    handleNonTerminatingInstructions(bb, lastBlock);

    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && "handleLoopsBlock called with non-branch terminator");

    // If the header's also an exit, then test the condition and break on it
    if (loop->isLoopExiting(bb)) {
        llvm::Value* condition = branchInst->getCondition();
        assert(condition && "conditional branch without condition");

        // Find the successor that exits the loop
        llvm::SmallVector<llvm::BasicBlock*, 8> exitBlocks;
        loop->getExitBlocks(exitBlocks);
        int succNum = -1;
        for (int i = 0; i < branchInst->getNumSuccessors(); ++i) {
            for (llvm::SmallVector<llvm::BasicBlock*,8>::iterator bbI = exitBlocks.begin(), bbE = exitBlocks.end(); bbI != bbE; ++bbI) {
                if (*bbI == branchInst->getSuccessor(i)) {
                    succNum = i;
                    break;
                }
            }
        }
        assert(succNum != -1 && succNum <= 1);

        // if it's the first guy, just pass the condition on, otherwise we have
        // to invert it
        if (succNum == 0) {
            backEndTranslator->addIf(condition);
            backEndTranslator->addBreak();
            backEndTranslator->addEndif();
        } else {
            // invert it then do stuff
            assert(!"not handled yet");
        }
    }

    // For every block in this loop, handle it.
    for (llvm::Loop::block_iterator i = loop->block_begin(), e = loop->block_end(); i != e; ++i) {
        handleLoopBlock(*i, lastBlock);
    }

    // If the block's a latch, close it
    if (loop->getLoopLatch() == bb) {
        translator->addPhiCopies(branchInst);
        backEndTranslator->addLoopEnd();
    }

    return;
}

void CodeGeneration::handleLoopBlock(const llvm::BasicBlock* bb, bool lastBlock)
{
    // llvm::errs() << "\n\n";
    // llvm::errs() << *bb;

    // If we've already handled it, move on, else handle it
    if (loopBlockSet.count(bb))
        return;
    loopBlockSet.insert(bb);

    llvm::Loop* loop = loopInfo->getLoopFor(bb);
    assert(loop && "handleLoopBlock called on non-loop");

    // We don't handle nested loops yet
    if (loop->getLoopDepth() > 1) {
        gla::UnsupportedFunctionality("Nested loops");
    }

    // If it's a loop header, handle it specially, else send out instructions
    if (loop->getHeader() == bb) {
        handleLoopHeader(bb, lastBlock);
        return;
    }

    handleNonTerminatingInstructions(bb, lastBlock);

    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && "handleLoopsBlock called with non-branch terminator");

    // If the block's an exit, then test the condition and break on it
    if (loop->isLoopExiting(bb)) {
        llvm::Value* condition = branchInst->getCondition();
        assert(condition && "conditional branch without condition");

        // Find the successor that exits the loop
        llvm::SmallVector<llvm::BasicBlock*, 8> exitBlocks;
        loop->getExitBlocks(exitBlocks);
        int succNum = -1;
        for (int i = 0; i < branchInst->getNumSuccessors(); ++i) {
            for (llvm::SmallVector<llvm::BasicBlock*,8>::iterator bbI = exitBlocks.begin(), bbE = exitBlocks.end(); bbI != bbE; ++bbI) {
                if (*bbI == branchInst->getSuccessor(i)) {
                    succNum = i;
                    break;
                }
            }
        }
        assert(succNum != -1 && succNum <= 1);

        // if it's the first guy, just pass the condition on, otherwise we have
        // to invert it
        if (succNum == 0) {
            backEndTranslator->addIf(condition);
            backEndTranslator->addBreak();
            backEndTranslator->addEndif();
        } else {
            // invert it then do stuff
            assert(!"not handled yet");
        }
    }

    // If the block's a latch, close it
    if (loop->getLoopLatch() == bb) {
        translator->addPhiCopies(branchInst);
        backEndTranslator->addLoopEnd();
    }

    return;
}

bool CodeGeneration::runOnModule(llvm::Module& module)
{
    //
    // Query the back end about its flow control
    //
    bool breakOp, continueOp, earlyReturnOp, discardOp;
    gla::EFlowControlMode flowControlMode;
    backEnd->getControlFlowMode(flowControlMode, breakOp, continueOp, earlyReturnOp, discardOp);
    if (flowControlMode == gla::EFcmExplicitMasking)
        gla::UnsupportedFunctionality("explicit masking in middle end");

    //
    // Translate globals.
    //
    for (llvm::Module::const_global_iterator global = module.global_begin(), end = module.global_end(); global != end; ++global)
        backEndTranslator->addGlobal(global);

    //
    // Translate code.
    //
    llvm::Module::iterator function, lastFunction;
    for (function = module.begin(), lastFunction = module.end(); function != lastFunction; ++function) {
        if (function->isDeclaration()) {
            //?? do we need to handle declarations of functions, or just definitions?
        } else {

            // Get/set the loop info
            loopInfo = &getAnalysis<llvm::LoopInfo>(*function);
            translator->setLoopInfo(loopInfo);

            // debug stuff
            // llvm::errs() << "\n\nLoop info:\n";
            // loopInfo->print(llvm::errs());

            // handle function's with bodies

            backEndTranslator->startFunctionDeclaration(function->getFunctionType(), function->getNameStr());

            // paramaters and arguments
            for (llvm::Function::const_arg_iterator arg = function->arg_begin(), endArg = function->arg_end(); arg != endArg; ++arg) {
                llvm::Function::const_arg_iterator nextArg = arg;
                ++nextArg;
                backEndTranslator->addArgument(arg, nextArg == endArg);
            }

            backEndTranslator->endFunctionDeclaration();

            backEndTranslator->startFunctionBody();

            // Phi declaration pass
            if (backEnd->getDeclarePhiCopies())
                translator->declarePhiCopies(function);

            // basic blocks
            for (llvm::Function::const_iterator bb = function->begin(), E = function->end(); bb != E; ++bb) {
                bool lastBlock = (bb->getNextNode() == E);

                // If the basicblock's exhibits loop-relevant control flow,
                // handle it specially
                llvm::Loop* loop = loopInfo->getLoopFor(bb);
                if (loop && loop->contains(bb)) {
                    handleLoopBlock(bb, lastBlock);
                    continue;
                }

                // instructions in the basic block
                for (llvm::BasicBlock::const_iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
                    const llvm::Instruction* llvmInstruction = i;

                    //?? what are compare llvmInstruction predicates
                    // if (const CmpInst *CI = dyn_cast<CmpInst>(&llvmInstruction))

                    if (llvmInstruction->getOpcode() == llvm::Instruction::Br && flowControlMode == gla::EFcmStructuredOpCodes)
                        translator->addFlowControl(llvmInstruction, backEnd->getRemovePhiFunctions());
                    else {
                        if (! (backEnd->getRemovePhiFunctions() && llvmInstruction->getOpcode() == llvm::Instruction::PHI))
                            backEndTranslator->add(llvmInstruction, lastBlock);
                    }
                }
            }

            backEndTranslator->endFunctionBody();
        }
    }

    //set_branchtargets(&v, currentInstructionructions, num_instructions);
    backEndTranslator->print();

    return false;
}

void CodeGeneration::getAnalysisUsage(llvm::AnalysisUsage& AU) const
{
    AU.addRequired<llvm::LoopInfo>();
    return;
}

char CodeGeneration::ID = 0;

namespace llvm {
    INITIALIZE_PASS(CodeGeneration,
                    "code-gen",
                    "LunarGLASS code generation pass",
                    true,   // Whether it preserves the CFG
                    false); // Whether it is an analysis pass
} // end namespace llvm

namespace gla {
    llvm::ModulePass* createCodeGenerationPass(BackEndTranslator* bet, BackEnd* be)
    {
        CodeGeneration* cgp = new CodeGeneration();
        cgp->setBackEndTranslator(bet);
        cgp->setBackEnd(be);
        cgp->setBottomTranslator(new BottomTranslator(bet));
        return cgp;
    }
} // end gla namespace

void gla::PrivateManager::translateBottomToTarget()
{
    llvm::PassManager passManager;
    passManager.add(gla::createCodeGenerationPass(backEndTranslator, backEnd));
    passManager.run(*module);
}
