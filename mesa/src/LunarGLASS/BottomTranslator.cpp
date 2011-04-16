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
//   on an instruction by instruction level.  This is because some constructs
//   like do-while loops need to know as soon as they begin what kind of flow
//   control construct they represent.
//
// * Each block is passed to handleBlock, which dispatches it depending on
//   available info.  If it's a loop header, latch, or exit, then it will
//   dispatch it to handleLoopBlock.  Otherwise, if it's a branch, dispatch to
//   handleBranchingBlock.  If none of the above apply, then it will get passed
//   to handleReturnBlock.  handleBlock keeps track of blocks it's already seen,
//   so it wont process the same block twice, allowing it to be called by other
//   handlers when a certain basic block processing order must be maintained.
//   This also allows handleBlock to know when it's handling the last program
//   order (not neccessarily llvm order) block.
//
// * Loops in LLVM are a set of blocks, possibly identified with up to 3
//   properties: header, exiting, and latch.  A block that is a loop header
//   (there is only 1 per loop) has the loop's backedge branching to it.  It
//   also is the first block encountered in program or LLVM IR order.  An
//   exiting block is a block that exits the loop, and the block outside the
//   loop that the exiting block branches to is called an exit block.  If flow
//   control is structured, then there should only be 1 exit block, but there
//   may be many exiting blocks.  A latch is a block with a backedge.
//   Canonicalization (via loop-simplify or a prerequisite for any of the loop
//   optimizations) enforces that there is only 1 latch, and that all of the
//   predecessors of an exit block are inside the loop.  During
//   canonicalization, if there are more than 1 latches, a new (unconditional)
//   latch block is created and the previous latches now branch to it instead of
//   the header.  Any blocks without one of the above properties can be handled
//   normally, as though they weren't even in a loop.
//
// * Loops are presented to the backend using the loop interfaces present in
//   Manager.h.  Nested loops are currently not supported.
//
// * handleLoopBlock proceeds as follows for the following loop block types:
//
//     - Header:  Call beginLoop interface. If the header is not also a latch or
//                exiting block, then it's the start of some internal control
//                flow, so pass it off to handleBranchingBlock.  Otherwise
//                handle its instructions, handle it as a latch or exit if it's
//                also a latch or exit, and pass every block in the loop to
//                handleBlock. This is done to make sure that all loop internal
//                blocks are handled before further loop external blocks are.
//                Finally, end the loop.
//
//     - Latch:   Handle its instructions, add phi copies if applicable, call
//                addLoopBack interface.
//
//     - Exiting: Handle its instructions, call addLoopExit interface
//
// * handleBranching handles it's instructions, and adds phi nodes if specified
//   by the backend. On an unconditional branch, it checks to see if the block
//   being branched is a subtree of the cfg and if so handles it, otherwise it
//   does nothing. On a conditional branch, it will find the earliest confluce
//   point, determine if it's an if-then-else construct, call the addIf
//   interface, and handle the then block. It also takes care of condition
//   inversion when the then branch is the confluence point. If the construct is
//   an if-then-else construct, it will then call the addElse interface and
//   handle the else block. Finally, it calls the addEndIf interface.
//
// * handleReturnBlock handles it's instructions, and calls the
//   handleReturnBlock interface
//
//===----------------------------------------------------------------------===//

// TODO: - test for return statements inside a loop.
// Unimplemented: - loops with return statements inside them

// LLVM includes
#include "llvm/DerivedTypes.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Intrinsics.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/PassManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <string>
#include <map>
#include <vector>
#include <stack>
#include <algorithm>

// LunarGLASS includes
#include "Exceptions.h"
#include "LunarGLASSBackend.h"
#include "LunarGLASSLlvmInterface.h"
#include "Manager.h"
#include "Options.h"

// LunarGLASS Passes
#include "Passes/Analysis/IdentifyConditionals.h"

namespace {

    class BottomTranslator : public llvm::ModulePass {
    public:
        BottomTranslator() : ModulePass(ID)
        { }

        ~BottomTranslator()
        { }

        // Translate from LLVM CFG style to structured style.
        void declarePhiCopies(const llvm::Function*);

        void setLoopInfo(llvm::LoopInfo* li) { loopInfo = li; }

        void addPhiCopies(const llvm::Instruction*);

        void setBackEndTranslator(gla::BackEndTranslator* bet) { backEndTranslator = bet; }
        void setBackEnd(gla::BackEnd* be)                      { backEnd = be; }

        // Module Pass implementation
        static char ID;
        bool runOnModule(llvm::Module&);
        void print(std::ostream&, const llvm::Module*) const;
        void getAnalysisUsage(llvm::AnalysisUsage&) const;

    protected:
        gla::BackEndTranslator* backEndTranslator;
        gla::BackEnd* backEnd;
        gla::EFlowControlMode flowControlMode;

        llvm::LoopInfo* loopInfo;
        llvm::DominatorTree* domTree;
        llvm::IdentifyConditionals* idConds;
        llvm::ScalarEvolution* scalarEvo;
        llvm::LazyValueInfo* lazyInfo;

        bool lastBlock;

        llvm::SmallPtrSet<const llvm::BasicBlock*,8> handledBlocks;

        // Data for handling loops
        std::stack<llvm::BasicBlock*> headers;
        std::stack<llvm::BasicBlock*> latches;
        std::stack<llvm::BasicBlock*> exits;
        std::stack<llvm::Loop*> loops;
        std::stack<bool> preservedBackedge;

        // Set everything up for handling a new loop
        void newLoop(const llvm::BasicBlock*);

        // Reset loop data
        void closeLoop();

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

        // Force the output of the current latch. Assumes it's only being called
        // when the source-level backedge is not preserved.
        void forceOutputLatch();

        // If dominator properly dominates dominatee, then handle it, else do nothing
        void attemptHandleDominatee(const llvm::BasicBlock* dominator, const llvm::BasicBlock* dominatee);

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

void BottomTranslator::handleNonTerminatingInstructions(const llvm::BasicBlock* bb) {
    // Add the non-terminating instructions
    for (llvm::BasicBlock::const_iterator i = bb->begin(), e = bb->getTerminator(); i != e; ++i) {
        const llvm::Instruction* inst = i;

        if (! (backEnd->getRemovePhiFunctions() && inst->getOpcode() == llvm::Instruction::PHI))
            backEndTranslator->add(inst, lastBlock);

    }
}

static bool properExitBlock(const llvm::BasicBlock* bb, const llvm::Loop* loop)
{
    for (llvm::const_pred_iterator i = pred_begin(bb), e = pred_end(bb); i != e; ++i) {
        if (! loop->contains(*i))
            return false;
    }

    return true;
}

void BottomTranslator::newLoop(const llvm::BasicBlock* bb)
{
    llvm::Loop* loop = loopInfo->getLoopFor(bb);
    assert(loop && "newLoop called on non-loop");

    llvm::BasicBlock* header = loop->getHeader();
    llvm::BasicBlock* exit   = loop->getUniqueExitBlock();
    llvm::BasicBlock* latch  = loop->getLoopLatch();

    assert(exit && "Unstructured flow control: flow exits a loop into multiple places");
    assert(header && latch && gla::Util::getNumLatches(loop) == 1 && properExitBlock(exit, loop) && "Loop not in canonical form");
    assert((gla::Util::isUnConditional(latch) || loop->isLoopExiting(latch)) && "Non-bottom latching loop structure");

    assert(loops.size() == headers.size() && headers.size() == exits.size() && exits.size() == latches.size()
           && latches.size() == preservedBackedge.size() && "Internal data mismatch");

    if (loops.size() != 0 || loop->getLoopDepth() > 1)
        gla::UnsupportedFunctionality("nested loops");

    bool preserved = gla::Util::isConditional(latch) || latch->getSinglePredecessor();

    // We'll have to handle the latch specially if the backedge is not preserved.
    if (! preserved) {
        handledBlocks.insert(latch);
    }

    loops.push(loop);
    headers.push(header);
    exits.push(exit);
    latches.push(latch);
    preservedBackedge.push(preserved);
}

void BottomTranslator::attemptHandleDominatee(const llvm::BasicBlock* dominator, const llvm::BasicBlock* dominatee)
{
    llvm::BasicBlock* unconstTor = const_cast<llvm::BasicBlock*>(dominator); // Necessary
    llvm::BasicBlock* unconstTee = const_cast<llvm::BasicBlock*>(dominatee); // Necessary

    if (domTree->properlyDominates(unconstTor, unconstTee)) {
        handleBlock(dominatee);
    }
}

void BottomTranslator::handleLoopBlock(const llvm::BasicBlock* bb)
{
    assert(loops.size() != 0 && "handleLoopBlock called on a new loop without newLoop being called");

    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst && "handleLoopBlock called with non-branch terminator");

    llvm::Value* condition = branchInst->isConditional() ? branchInst->getCondition() : NULL;

    bool isHeader  = bb == headers.top();
    bool isExiting = bb == exits.top();
    bool isLatch   = bb == latches.top();

    // If it's a loop header, have the back-end add it
    if (isHeader) {
        // llvm::PHINode* pn = loop->getCanonicalInductionVariable();
        // if (pn)
        //     backEndTranslator->beginInductiveLoop();
        backEndTranslator->beginLoop();
    }

    // If the branch is conditional and not a latch or exiting, we're dealing
    // with conditional (e.g. if-then-else) flow control from a
    // header. Otherwise handle it's instructions ourselves.
    if (condition && !isLatch && !isExiting) {
        assert(isHeader);
        assert(idConds->getConditional(bb));
        handleBranchingBlock(bb);
    } else {
        handleNonTerminatingInstructions(bb);
    }

    // Add phi copies (if applicable)
    if ((isLatch || isExiting) && backEnd->getRemovePhiFunctions())
        addPhiCopies(branchInst);

    // If it's a latch, add the (possibly conditional) loop-back. Immediately
    // handle the other block if we dominate it.
    if (isLatch) {
        backEndTranslator->addLoopBack(condition, branchInst->getSuccessor(0) != headers.top());
        assert(( !condition || isExiting) && "redundant assertion failed");
    }

    // If we're exiting, add the (possibly conditional) exit. Immediately handle
    // the other block if we dominate it.
    if (isExiting) {
        if (exits.top() == branchInst->getSuccessor(0)) {
            backEndTranslator->addLoopExit(condition, false);
            if (condition)
                attemptHandleDominatee(bb, branchInst->getSuccessor(1));
        } else if (condition && exits.top() == branchInst->getSuccessor(1)) {
            backEndTranslator->addLoopExit(condition, true);
            attemptHandleDominatee(bb, branchInst->getSuccessor(0));
        } else {
            assert(!"Exiting block does not branch to exit");
        }
    }

    // We've been fully handled by now if we're a latch. If we're also a header,
    // we must be a single block self-loop, so we're still done.
    if (isLatch) {
        assert(( !isHeader || ++(loops.top()->block_begin()) == loops.top()->block_end()) && "Header's a latch, and not a self-loop");
        return;
    }
    assert(isHeader || isExiting);

    // If we branch to a latch, and the backedge is not source-preserved, then
    // we should replicate the latch block.
    if ( !preservedBackedge.top() && gla::Util::isPredecessor(bb, latches.top()))
        forceOutputLatch();


    // We've been fully handled by now, unless we're a header
    if (! isHeader)
        return;
    assert(isHeader);


    // If we're a header, by this point, all of our blocks in our loop should of
    // been handled.
    for (llvm::Loop::block_iterator i = loops.top()->block_begin(), e = loops.top()->block_end(); i != e; ++i) {
        assert(handledBlocks.count(*i) && "Loop blocks remaining that were not handled structurally");
    }

    backEndTranslator->endLoop();
    closeLoop();

    return;
}

void BottomTranslator::closeLoop()
{
    loops.pop();
    headers.pop();
    exits.pop();
    latches.pop();
    preservedBackedge.pop();
}

void BottomTranslator::forceOutputLatch()
{
    assert(latches.top() && !preservedBackedge.top());
    assert(handledBlocks.count(latches.top()));

    llvm::BranchInst* br = llvm::dyn_cast<llvm::BranchInst>(latches.top()->getTerminator());
    assert(br && br->isUnconditional());

    handleNonTerminatingInstructions(latches.top());

    if (backEnd->getRemovePhiFunctions()) {
        addPhiCopies(br);
    }

    backEndTranslator->addLoopBack(NULL, false);
}

void BottomTranslator::handleIfBlock(const llvm::BasicBlock* bb)
{

    const llvm::Conditional* cond = idConds->getConditional(bb);
    assert(cond);

    bool invert = cond->isIfElse();

    // Add an if
    backEndTranslator->addIf(cond->getCondition(), invert);

    // Add the then block, flipping it if we're inverted
    if (invert) {
        handleBlock(cond->getElseBlock());
    } else {
        handleBlock(cond->getThenBlock());
    }

    // Add the else block, if we're an if-then-else.
    if (cond->isIfThenElse()) {
        backEndTranslator->addElse();
        handleBlock(cond->getElseBlock());
    }

    backEndTranslator->addEndif();

    // We'd like to now shedule the handling of the merge block, just in case
    // the order we get the blocks in doesn't have it next.
    handleBlock(cond->getMergeBlock());

    return;
}

void BottomTranslator::handleReturnBlock(const llvm::BasicBlock* bb)
{
    assert(llvm::isa<llvm::ReturnInst>(bb->getTerminator()));

    handleNonTerminatingInstructions(bb);
    backEndTranslator->add(bb->getTerminator(), lastBlock);

    return;
}

void BottomTranslator::handleBranchingBlock(const llvm::BasicBlock* bb)
{
    const llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
    assert(branchInst);

    // Handle it's instructions and do phi node removal if appropriate
    handleNonTerminatingInstructions(bb);
    if (backEnd->getRemovePhiFunctions()) {
        addPhiCopies(branchInst);
    }

    // TODO: handle for if we're branching into a latch

    // If it's unconditional, we'll want to handle any subtrees that it points to.
    if (branchInst->isUnconditional()) {
        if (domTree->dominates(bb, branchInst->getSuccessor(0)))
            handleBlock(branchInst->getSuccessor(0));
        return;
    }

    assert(branchInst->getNumSuccessors() == 2 && "Ill-formed conditional branch");

    handleIfBlock(bb);
    return;
}

void BottomTranslator::handleBlock(const llvm::BasicBlock* bb)
{
    if (handledBlocks.count(bb))
        return;
    handledBlocks.insert(bb);

    // Are we on the last block?
    if (handledBlocks.size() == bb->getParent()->getBasicBlockList().size())
        lastBlock = true;

    // If the block exhibits loop-relevant control flow,
    // handle it specially
    llvm::Loop* loop = loopInfo->getLoopFor(bb);
    if (loop && (loop->getHeader() == bb || gla::Util::isLatch(bb, loop) || loop->isLoopExiting(bb))
             && flowControlMode == gla::EFcmStructuredOpCodes) {

        if (loop->getHeader() == bb)
            newLoop(bb);

        handleLoopBlock(bb);
        return;
    }

    // If the block's a branching block, handle it specially.
    if (llvm::isa<llvm::BranchInst>(bb->getTerminator())) {
        handleBranchingBlock(bb);
        return;
    }

    // Otherwise we're a block ending in a return statement
    handleReturnBlock(bb);
    return;
}

bool BottomTranslator::runOnModule(llvm::Module& module)
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
            loopInfo  = &getAnalysis<llvm::LoopInfo>             (*function);
            domTree   = &getAnalysis<llvm::DominatorTree>        (*function);
            idConds   = &getAnalysis<llvm::IdentifyConditionals> (*function);
            scalarEvo = &getAnalysis<llvm::ScalarEvolution>      (*function);
            lazyInfo  = &getAnalysis<llvm::LazyValueInfo>        (*function);

            // debug stuff
            if (gla::Options.debug && loopInfo->begin() != loopInfo->end()) {
                llvm::errs() << "\n\nLoop info:\n";        loopInfo->print(llvm::errs());
                llvm::errs() << "\n\nScalar evolution:\n"; scalarEvo->print(llvm::errs());
            }

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
                declarePhiCopies(function);

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

void BottomTranslator::getAnalysisUsage(llvm::AnalysisUsage& AU) const
{
    AU.addRequired<llvm::LoopInfo>();
    AU.addRequired<llvm::DominatorTree>();
    AU.addRequired<llvm::IdentifyConditionals>();
    AU.addRequired<llvm::ScalarEvolution>();
    AU.addRequired<llvm::LazyValueInfo>();
    return;
}

char BottomTranslator::ID = 0;

namespace llvm {
    INITIALIZE_PASS(BottomTranslator,
                    "bottom-transl",
                    "LunarGLASS bottom translator pass",
                    true,   // Whether it preserves the CFG
                    false); // Whether it is an analysis pass
} // end namespace llvm

static llvm::ModulePass* createBottomTranslatorPass(gla::BackEndTranslator* bet, gla::BackEnd* be)
{
    BottomTranslator* bot = new BottomTranslator();
    bot->setBackEndTranslator(bet);
    bot->setBackEnd(be);
    return bot;
}

// The below are the only externally exposed functionality

void gla::PrivateManager::translateBottomToTarget()
{
    llvm::PassManager passManager;
    passManager.add(createBottomTranslatorPass(backEndTranslator, backEnd));
    passManager.run(*module);
}
