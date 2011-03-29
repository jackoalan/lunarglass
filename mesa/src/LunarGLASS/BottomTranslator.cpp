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
#include <stack>

// LunarGLASS includes
#include "CFG.h"
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


    private:
        gla::BackEndTranslator* backEndTranslator;
        gla::BackEnd* backEnd;

        BottomTranslator* translator;

        gla::EFlowControlMode flowControlMode;

        // Set of blocks belonging to flowcontrol constructs that we've already handled
        llvm::SmallPtrSet<const llvm::BasicBlock*,8> handledBlocks;

        // Stack of confluence points
        std::stack<llvm::BasicBlock*> confluencePoints;

        llvm::LoopInfo* loopInfo;

        // Handle and dispatch the given block, updating handledBlocks.
        void handleBlock(const llvm::BasicBlock*);

        // Handle and dispatch for an if block
        void handleIfBlock(const llvm::BasicBlock*);

        // Send off all the non-terminating instructions in a basic block to the
        // backend
        void handleNonTerminatingInstructions(const llvm::BasicBlock*);

        // Handle latch duties, such as phi copies and closing the loop
        void handleLatch(const llvm::BranchInst*);

        // Handle exiting loop duties, such as setting up the condition and
        // adding breaks
        void handleExiting(const llvm::BranchInst*, llvm::Loop*);

        // Given a loop block, handle it. Dispatches to other methods and ends
        // up handling all blocks in the loop.
        void handleLoopBlock(const llvm::BasicBlock*);

        // Given a block ending in return, output it and the return
        void handleReturnBlock(const llvm::BasicBlock*);

        void handleUncondBranch(const llvm::BasicBlock*);

        void handleConditional(const llvm::BasicBlock*);


        ~CodeGeneration()
        {
            delete translator;
        }

    };
} // end namespace

void BottomTranslator::addFlowControl(const llvm::Instruction* llvmInstruction, bool removePhiFunctions)
{
    assert(!"We shouldn't be here");

    // Translate from LLVM CFG style to structured style. This is done by
    // identifying and handling loops via LoopInfo, and conditionals are handled
    // by using a stack to keep track of what is pending. So far nested loops
    // are not supported

    // Also, translate from SSA form to non-SSA form (remove phi functions).
    // This is done by looking ahead for phi functions and adding copies in the
    // phi-predecessor blocks.2

    // Currently, this is done in a fragile way. Only (un-nested) loops are
    // handled via some form of analysis. For all other LLVM branches found, we
    // assume they must be representing if-then-else constructs.

    // if (removePhiFunctions) {
    //     // All branches that branch to a block having phi instructions for that
    //     // branch need copies inserted.
    //     addPhiCopies(llvmInstruction);
    // }

    // // If it's an unconditional branch into a loop header, ignore it and move on
    // const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(llvmInstruction);
    // if (branchInst && branchInst->isUnconditional() && loopInfo->isLoopHeader(branchInst->getSuccessor(0))) {
    //     return;
    // }

    // switch (llvmInstruction->getNumOperands()) {
    // case 1:
    //     // We are doing an unconditional branch
    //     // Assume it is to the merge of the if-then-else or the if-then
    //     if (flowControl.back() == llvmInstruction->getOperand(0)) {
    //         // This must be the end of the if block
    //         backEndTranslator->addEndif();
    //         flowControl.pop_back();
    //     } else {
    //         // This must be the end of a then that has as else
    //         backEndTranslator->addElse();
    //         flowControl.pop_back();
    //         flowControl.push_back(llvmInstruction->getOperand(0));
    //     }
    //     break;
    // case 3:
    //     // We are splitting into two children.
    //     // Assume we are entering an if-then-else statement or if-then statement.
    //     flowControl.push_back(llvmInstruction->getOperand(1));
    //     backEndTranslator->addIf(llvmInstruction->getOperand(0));
    //     break;
    // default:
    //     gla::UnsupportedFunctionality("Flow Control in Bottom IR");
    // }
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

void CodeGeneration::handleNonTerminatingInstructions(const llvm::BasicBlock* bb) {
    // Add the non-terminating instructions
    for (llvm::BasicBlock::const_iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
        const llvm::Instruction* inst = i;

        // Don't handle the terminator
        if (bb->getTerminator() == inst)
            break;

        if (! (backEnd->getRemovePhiFunctions() && inst->getOpcode() == llvm::Instruction::PHI))
            backEndTranslator->add(inst);

    }
}

void CodeGeneration::handleLatch(const llvm::BranchInst* branchInst) {
    // Add phi copies and close
    translator->addPhiCopies(branchInst);
    backEndTranslator->addLoopEnd();
}

void CodeGeneration::handleExiting(const llvm::BranchInst* branchInst, llvm::Loop* loop) {
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
        assert(!"condition inversion");
    }
}

void CodeGeneration::handleLoopBlock(const llvm::BasicBlock* bb)
{
    llvm::Loop* loop = loopInfo->getLoopFor(bb);
    assert(loop && "handleLoopBlock called on non-loop");

    // Helper bool for whether the internals have already been handled
    bool handledInternals = false;

    // We don't handle nested loops yet
    if (loop->getLoopDepth() > 1) {
        gla::UnsupportedFunctionality("Nested loops");
    }

    bool isHeader  = loop->getHeader() == bb;
    bool isExiting = loop->isLoopExiting(bb);
    bool isLatch   = loop->getLoopLatch() == bb;

    // If it's a loop header, have the back-end add it
    if (isHeader) {
        backEndTranslator->addLoop(NULL);
    }

    // If the block's neither a latch nor exiting, then we're dealing
    // with internal flow control.
    if (!isLatch && !isExiting) {
        handleConditional(bb);
        handledInternals = true;
    }

    // If we've not handled the internals yet, handle them now
    if (!handledInternals)
        handleNonTerminatingInstructions(bb);

    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && "handleLoopsBlock called with non-branch terminator");

    // If the block's an exit, handle it
    if (isExiting) {
        handleExiting(branchInst, loop);
    }


    // If it's header, then add all of the other blocks in the loop. This is
    // because we want to finish the entire loop before we consider any blocks
    // outside the loop (as other blocks may be before the loop blocks in the
    // LLVM-IR's lineralization).
    if (isHeader) {
        for (llvm::Loop::block_iterator i = loop->block_begin(), e = loop->block_end(); i != e; ++i) {
            handleBlock(*i);
        }
    }

    // If the block's a latch, handle it
    if (isLatch) {
        handleLatch(branchInst);
    }


    return;
}

void CodeGeneration::handleIfBlock(const llvm::BasicBlock* bb)
{
    handleNonTerminatingInstructions(bb);

    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && branchInst->getNumSuccessors() == 2 && "handleIfBlock called with improper terminator");

    // Find the earliest confluence point and add it to our stack
    llvm::BasicBlock* cBB = gla::FindEarliestConfluencePoint(branchInst->getSuccessor(0), branchInst->getSuccessor(1));
    confluencePoints.push(cBB);
    //llvm::errs() << "confluence: " << *cBB;

    if (backEnd->getRemovePhiFunctions()) {
        // All branches that branch to a block having phi instructions for that
        // branch need copies inserted.
        translator->addPhiCopies(branchInst);
    }

    // Create an if, and handle the then
    backEndTranslator->addIf(branchInst->getCondition());
    handleBlock(branchInst->getSuccessor(0));

    // If we branch to the confluence point, then we're an if-then, else were're
    // dealing with an if-then-else
    bool ifThenElse = branchInst->getSuccessor(1) != confluencePoints.top();

    // If we're an if-then-else, add in the else
    if (ifThenElse) {
        backEndTranslator->addElse();
        handleBlock(branchInst->getSuccessor(1));
    }

    backEndTranslator->addEndif();

    confluencePoints.pop();
    return;

}

void CodeGeneration::handleReturnBlock(const llvm::BasicBlock* bb)
{
    assert(llvm::isa<llvm::ReturnInst>(bb->getTerminator()));

    handleNonTerminatingInstructions(bb);
    backEndTranslator->add(bb->getTerminator());

    return;
}

void CodeGeneration::handleUncondBranch(const llvm::BasicBlock* bb)
{
    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && branchInst->isUnconditional());

    handleNonTerminatingInstructions(bb);
    if (backEnd->getRemovePhiFunctions()) {
        // All branches that branch to a block having phi instructions for that
        // branch need copies inserted.
        translator->addPhiCopies(branchInst);
    }

    // If we're going to a confluence block, then end the if, otherwise just proceed
    if (confluencePoints.size() && confluencePoints.top() == branchInst->getSuccessor(0)) {
        //backEndTranslator->addEndif();
    }

    return;
}

void CodeGeneration::handleConditional(const llvm::BasicBlock* bb)
{
    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst);

    if (branchInst->getNumSuccessors() == 2) {
        handleIfBlock(bb);
        return;
    }

    if (branchInst->isUnconditional()) {
        handleUncondBranch(bb);
        return;
    }

}

void CodeGeneration::handleBlock(const llvm::BasicBlock* bb)
{
    if (handledBlocks.count(bb))
        return;
    handledBlocks.insert(bb);

    // If the block exhibits loop-relevant control flow,
    // handle it specially
    llvm::Loop* loop = loopInfo->getLoopFor(bb);
    if (loop && (loop->getHeader() == bb || loop->getLoopLatch() == bb || loop->isLoopExiting(bb))
             && flowControlMode == gla::EFcmStructuredOpCodes) {
        handleLoopBlock(bb);
        return;
    }

    // If the block's still a branching block, then handle it as a conditional
    if (llvm::isa<llvm::BranchInst>(bb->getTerminator())) {
        handleConditional(bb);
        return;
    }

    // Otherwise we're a block ending in a return statement
    handleReturnBlock(bb);
    return;
}

bool CodeGeneration::runOnModule(llvm::Module& module)
{
    //
    // Query the back end about its flow control
    //
    bool breakOp, continueOp, earlyReturnOp, discardOp;
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
                handleBlock(bb);
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
