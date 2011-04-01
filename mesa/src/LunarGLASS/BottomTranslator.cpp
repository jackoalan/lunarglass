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

//===----------------------------------------------------------------------===//
// Description of how flow control is handled:
//
// * Flow control is handled on a basic block by basic block level, rather than
//   on an instruction by instruction level. This is because some constructs
//   like do-while loops need to know as soon as they begin what kind of flow
//   control construct they represent.
//
// * Each block is passed to handleBlock, which dispatches it depending on
//   available info. If it's a loop header, latch, or exit, then it will
//   dispatch it to handleLoopBlock. Otherwise, if it's a branch, dispatch to
//   handleBranchingBlock. If none of the above apply, then it will get passed
//   to handleReturnBlock. handleBlock keeps track of blocks it's already seen,
//   so it wont process the same block twice, allowing it to be called by other
//   handlers when a certain basic block processing order must be
//   maintained. This also allows handleBlock to know when it's handling the
//   last program order (not neccessarily llvm order) block.
//
// * Loops in LLVM are a set of blocks, possibly tagged with 3 properties. A
//   block tagged as a loop header (there is only 1 per loop) has the loop's
//   backedge branching to it. It also is the first block encountered in program
//   or LLVM IR order. An exit block is a block that exits the loop. A latch is
//   a block with a backedge. There may be many exit blocks and many latches,
//   and any given block could have multiple tags. Any untagged blocks can be
//   handled normally, as though they weren't even in a loop.
//
// * Loops are presented to the backend using the addLoop, addLoopEnd,
//   addLoopExit, and addLoopBack interfaces. It is then up to the backends to
//   decide through these interfaces how they want to construct the resulting
//   loop. Unnested loops are currently not supported.
//
// * handleLoopBlock proceeds as follows for the following loop block types:
//
//     - Header:  Call addLoop interface. If the header is not also a latch or
//                exiting block, then it's the start of some internal control
//                flow, so pass it off to handleBranchingBlock.  Otherwise
//                handle its instructions, call addLoopExit interface if it's an
//                exit block as well, and pass every block in the loop to
//                handleBlock. This is done to make sure that all loop internal
//                blocks are handled before further loop external blocks are.
//                If it's also a latch, add phi copies if applicable and call
//                addLoopBack interface. Finally, end the loop.
//
//     - Latch:   Handle its instructions, add phi copies if applicable, call
//                addLoopBack interface.
//
//     - Exiting: Handle its instructions, call addLoopExit interface
//
// * handleBranching handles it's instructions. On an unconditional branch, it
//   then does nothing. On a conditional branch with 2 successors, it will find
//   the earliest confluce point, determine if it's an if-then-else construct,
//   call the addIf interface, and handle the then block. If the construct is an
//   if-then-else construct, it will then call the addElse interface and handle
//   the else block. Finally, it calls the addEndIf interface. A conditional
//   branch that does not have 2 successors would be unhandled control flow.
//
// * handleReturnBlock handles it's instructions, and calls the
//   handleReturnBlock interface
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
#include "Exceptions.h"
#include "LunarGLASSBackend.h"
#include "LunarGLASSLlvmInterface.h"
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

        bool lastBlock;

        // Set of blocks belonging to flowcontrol constructs that we've already handled
        llvm::SmallPtrSet<const llvm::BasicBlock*,8> handledBlocks;

        llvm::LoopInfo* loopInfo;

        // Handle and dispatch the given block, updating handledBlocks.
        void handleBlock(const llvm::BasicBlock*);

        // Handle and dispatch for an if block
        void handleIfBlock(const llvm::BasicBlock*);

        // Send off all the non-terminating instructions in a basic block to the
        // backend
        void handleNonTerminatingInstructions(const llvm::BasicBlock*);

        // Given a loop block, handle it. Dispatches to other methods and ends
        // up handling all blocks in the loop.
        void handleLoopBlock(const llvm::BasicBlock*);

        // Given a block ending in return, output it and the return
        void handleReturnBlock(const llvm::BasicBlock*);

        // Handle non-loop control flow
        void handleBranchingBlock(const llvm::BasicBlock*);


        ~CodeGeneration()
        {
            delete translator;
        }

    };
} // end namespace

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
            backEndTranslator->add(inst, lastBlock);

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
    bool isLatch   = gla::Util::isLatch(bb, loop);

    // If it's a loop header, have the back-end add it
    if (isHeader) {
        gla::LoopExitType let;
        if (isExiting && !isLatch) {
            let = gla::ELETTopExit;
        } else {
            let = gla::ELETNeither;
        }
        backEndTranslator->addLoop(let, false, bb);
    }

    // If the block's neither a latch nor exiting, then we're dealing with
    // internal flow control.
    if (!isLatch && !isExiting) {
        handleBranchingBlock(bb);
        handledInternals = true;
    }

    // If we've not handled the internals yet, handle them now
    if (!handledInternals)
        handleNonTerminatingInstructions(bb);

    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && "handleLoopsBlock called with non-branch terminator");

    // If the block's an exit, pass it on to the back end.
    if (isExiting) {
        // TODO: add internal consistency check to make sure that in the event
        // of a conditional exit, the exit occurs when the condition holds
        backEndTranslator->addLoopExit(bb);
    }


    // If it's header, then add all of the other blocks in the loop. This is
    // because we want to finish the entire loop before we consider any blocks
    // outside the loop (as other blocks may be before the loop blocks in the
    // LLVM-IR's lineralization).
    if (isHeader) {
        // We want to handle the blocks in LLVM IR order, instead of LoopInfo
        // order (which may be out of order, e.g. put an else before the if
        // block).
        // TODO: Find more cleaver/efficient way to do the below.
        for (llvm::Function::const_iterator i = bb->getParent()->begin(), e = bb->getParent()->end(); i != e; ++i) {
            if (loop->contains(i)) {
                handleBlock(i);
            }
        }
    }

    // If the block's a latch, add in our phi copies
    if (isLatch) {
        // Add phi copies (if applicable) and close
        if (backEnd->getRemovePhiFunctions()) {
            translator->addPhiCopies(branchInst);
        }
        backEndTranslator->addLoopBack(bb, gla::Util::getNumLatches(loop) == 1);
    }

    // If the block's a header (meaning by now that all the loop's blocks have
    // been handled), then close the loop
    if (isHeader) {
        backEndTranslator->addLoopEnd(bb);
    }

    return;
}

void CodeGeneration::handleIfBlock(const llvm::BasicBlock* bb)
{

    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && branchInst->getNumSuccessors() == 2 && "handleIfBlock called with improper terminator");

    // Get the conflunece BB
    llvm::BasicBlock* cBB = gla::Util::findEarliestConfluencePoint(branchInst->getSuccessor(0), branchInst->getSuccessor(1));
    // If we branch to the confluence point, then we're an if-then, else were're
    // dealing with an if-then-else
    bool ifThenElse = branchInst->getSuccessor(1) != cBB;

    // Create an if, and handle the then
    backEndTranslator->addIf(branchInst->getCondition());
    handleBlock(branchInst->getSuccessor(0));


    // If we're an if-then-else, add in the else
    if (ifThenElse) {
        backEndTranslator->addElse();
        handleBlock(branchInst->getSuccessor(1));
    }

    backEndTranslator->addEndif();

    return;
}

void CodeGeneration::handleReturnBlock(const llvm::BasicBlock* bb)
{
    assert(llvm::isa<llvm::ReturnInst>(bb->getTerminator()));

    handleNonTerminatingInstructions(bb);
    backEndTranslator->add(bb->getTerminator(), lastBlock);

    return;
}

void CodeGeneration::handleBranchingBlock(const llvm::BasicBlock* bb)
{
    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst);

    // Handle it's instructions and do phi node removal if appropriate
    handleNonTerminatingInstructions(bb);
    if (backEnd->getRemovePhiFunctions()) {
        translator->addPhiCopies(branchInst);
    }

    // If it's unconditional, we're done here.
    if (branchInst->isUnconditional()) {
        return;
    }

    // If it has 2 successors, then it's an if block
    if (branchInst->getNumSuccessors() == 2) {
        handleIfBlock(bb);
        return;
    }

    // We currently do not handle any constructs that are not if-then or
    // if-then-else
    gla::UnsupportedFunctionality("Conditional branch in bottom IR with != 2 successors");
}

void CodeGeneration::handleBlock(const llvm::BasicBlock* bb)
{
    if (handledBlocks.count(bb))
        return;
    handledBlocks.insert(bb);

    // Are we on the last block?
    if (handledBlocks.size() + 1 == bb->getParent()->getBasicBlockList().size())
        lastBlock = true;

    // If the block exhibits loop-relevant control flow,
    // handle it specially
    llvm::Loop* loop = loopInfo->getLoopFor(bb);
    if (loop && (loop->getHeader() == bb || gla::Util::isLatch(bb, loop) || loop->isLoopExiting(bb))
             && flowControlMode == gla::EFcmStructuredOpCodes) {
        handleLoopBlock(bb);
        return;
    }

    // If the block's still a branching block, then handle it.
    if (llvm::isa<llvm::BranchInst>(bb->getTerminator())) {
        handleBranchingBlock(bb);
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
            llvm::errs() << "\n\nLoop info:\n";
            loopInfo->print(llvm::errs());

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

            lastBlock = false;

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
