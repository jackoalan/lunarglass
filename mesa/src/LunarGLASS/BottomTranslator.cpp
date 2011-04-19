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

using namespace llvm;


namespace {

    class BottomTranslator : public ModulePass {
    public:
        BottomTranslator() : ModulePass(ID)
        { }

        ~BottomTranslator()
        { }

        // Translate from LLVM CFG style to structured style.
        void declarePhiCopies(const Function*);

        void setLoopInfo(LoopInfo* li) { loopInfo = li; }

        void addPhiCopies(const Instruction*);

        void setBackEndTranslator(gla::BackEndTranslator* bet) { backEndTranslator = bet; }
        void setBackEnd(gla::BackEnd* be)                      { backEnd = be; }

        // Module Pass implementation
        static char ID;
        bool runOnModule(Module&);
        void print(std::ostream&, const Module*) const;
        void getAnalysisUsage(AnalysisUsage&) const;

    protected:
        gla::BackEndTranslator* backEndTranslator;
        gla::BackEnd* backEnd;
        gla::EFlowControlMode flowControlMode;

        LoopInfo* loopInfo;
        DominatorTree* domTree;
        IdentifyConditionals* idConds;
        ScalarEvolution* scalarEvo;
        LazyValueInfo* lazyInfo;

        bool lastBlock;

        SmallPtrSet<const BasicBlock*,8> handledBlocks;

        // Data for handling loops
        std::stack<BasicBlock*> headers;
        std::stack<BasicBlock*> latches;
        std::stack< SmallVector<BasicBlock*,4> > exits;
        std::stack<Loop*> loops;
        std::stack<bool> preservedBackedge;

        // Set everything up for handling a new loop
        void newLoop(const BasicBlock*);

        // Reset loop data
        void closeLoop();

        // Handle and dispatch the given block, updating handledBlocks.
        void handleBlock(const BasicBlock*);

        // Handle and dispatch for an if block
        void handleIfBlock(const BasicBlock*);

        // Send off all the non-terminating instructions in a basic block to the
        // backend
        void handleNonTerminatingInstructions(const BasicBlock*);

        // Given a loop block, handle it. Dispatches to other methods and ends
        // up handling all blocks in the loop.
        void handleLoopBlock(const BasicBlock*);

        // Given a block ending in return, output it and the return
        void handleReturnBlock(const BasicBlock*);

        // Handle non-loop control flow
        void handleBranchingBlock(const BasicBlock*);

        // Force the output of the current latch. Assumes it's only being called
        // when the source-level backedge is not preserved.
        void forceOutputLatch();

        // If dominator properly dominates dominatee, then handle it, else do nothing
        void attemptHandleDominatee(const BasicBlock* dominator, const BasicBlock* dominatee);

    };

} // end namespace

void BottomTranslator::declarePhiCopies(const Function* function)
{
    // basic blocks
    for (Function::const_iterator bb = function->begin(), E = function->end(); bb != E; ++bb) {

        // instructions in the basic block
        for (BasicBlock::const_iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
            const Instruction* llvmInstruction = i;

            if (llvmInstruction->getOpcode() == Instruction::PHI)
                backEndTranslator->declarePhiCopy(llvmInstruction);
        }
    }
}

void BottomTranslator::addPhiCopies(const Instruction* llvmInstruction)
{
    // for each child block
    for (unsigned int op = 0; op < llvmInstruction->getNumOperands(); ++op) {

        // get the destination block (not all operands are blocks, but consider each that is)
        const BasicBlock *phiBlock = dyn_cast<BasicBlock>(llvmInstruction->getOperand(op));
        if (! phiBlock)
            continue;

        // for each llvm phi node, add a copy instruction
        for (BasicBlock::const_iterator i = phiBlock->begin(), e = phiBlock->end(); i != e; ++i) {
            const Instruction* destInstruction = i;
            const PHINode *phiNode = dyn_cast<PHINode>(destInstruction);

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

void BottomTranslator::handleNonTerminatingInstructions(const BasicBlock* bb) {
    // Add the non-terminating instructions
    for (BasicBlock::const_iterator i = bb->begin(), e = bb->getTerminator(); i != e; ++i) {
        const Instruction* inst = i;

        if (! (backEnd->getRemovePhiFunctions() && inst->getOpcode() == Instruction::PHI))
            backEndTranslator->add(inst, lastBlock);

    }
}

static bool properExitBlock(const BasicBlock* bb, const Loop* loop)
{
    for (const_pred_iterator i = pred_begin(bb), e = pred_end(bb); i != e; ++i) {
        if (! loop->contains(*i))
            return false;
    }

    return true;
}

void BottomTranslator::newLoop(const BasicBlock* bb)
{
    Loop* loop = loopInfo->getLoopFor(bb);
    assert(loop && "newLoop called on non-loop");

    BasicBlock* header = loop->getHeader();
    BasicBlock* latch  = loop->getLoopLatch();

    SmallVector<BasicBlock*, 4> exitVec;
    loop->getUniqueExitBlocks(exitVec);

    //todo: have exit lookahead.

    //assert(exits && "Unstructured flow control: flow exits a loop into multiple places");
    // assert(header && latch && gla::Util::getNumLatches(loop) == 1 && properExitBlock(exit, loop) && "Loop not in canonical form");
    assert((gla::Util::isUnconditional(latch) || loop->isLoopExiting(latch)) && "Non-bottom latching loop structure");

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
    exits.push(exitVec);
    latches.push(latch);
    preservedBackedge.push(preserved);
}

void BottomTranslator::attemptHandleDominatee(const BasicBlock* dominator, const BasicBlock* dominatee)
{
    BasicBlock* unconstTor = const_cast<BasicBlock*>(dominator); // Necessary
    BasicBlock* unconstTee = const_cast<BasicBlock*>(dominatee); // Necessary

    if (domTree->properlyDominates(unconstTor, unconstTee)) {
        handleBlock(dominatee);
    }
}



void BottomTranslator::handleLoopBlock(const BasicBlock* bb)
{
    assert(loops.size() != 0 && "handleLoopBlock called on a new loop without newLoop being called");
    assert(loops.size() == 1 && "uncaught nested loop");

    const BranchInst* branchInst = dyn_cast<BranchInst>(bb->getTerminator());
    assert(branchInst && "handleLoopBlock called with non-branch terminator");

    Value* condition = branchInst->isConditional() ? branchInst->getCondition() : NULL;

    Loop* loop = loops.top();

    bool isHeader  = bb == headers.top();
    bool isLatch   = bb == latches.top();
    bool isExiting = loop->isLoopExiting(bb);

    bool preserved = preservedBackedge.top();

    assert(!isLatch || preserved); // isLatch => preserved

    // If it's a loop header, have the back-end add it
    if (isHeader) {
        // PHINode* pn = loop->getCanonicalInductionVariable();
        // if (pn)
        //     backEndTranslator->beginInductiveLoop();
        backEndTranslator->beginLoop();
    }

    // If the branch is conditional and not a latch nor exiting, we're dealing
    // with conditional (e.g. if-then-else) flow control from a header.
    // Otherwise handle it's instructions ourselves.
    if (condition && (!isLatch && !isExiting)) {
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
        if (preservedBackedge.top()) {
            backEndTranslator->addLoopBack(condition, branchInst->getSuccessor(0) != headers.top());
            assert(( !condition || isExiting) && "redundant assertion failed");
        } else {
            forceOutputLatch();
            backEndTranslator->addLoopBack(condition, branchInst->getSuccessor(0) != latches.top());

            int pos = branchInst->getSuccessor(0) == latches.top() ? 0 : 1;
            assert(branchInst->getSuccessor(pos) == latches.top() && "Latch block does not branch to latch");

            if (branchInst->isConditional() && loop->contains(branchInst->getSuccessor(!pos)))
                attemptHandleDominatee(bb, branchInst->getSuccessor(!pos));

        }
    }

    // If we're exiting, add the (possibly conditional) exit. Immediately handle
    // the other block if we dominate it.
    if (isExiting) {
        if (gla::Util::smallVectorContains(exits.top(), branchInst->getSuccessor(0))) {
            backEndTranslator->addLoopExit(condition, false);
            if (condition)
                attemptHandleDominatee(bb, branchInst->getSuccessor(1));
        } else if (condition && gla::Util::smallVectorContains(exits.top(), branchInst->getSuccessor(1))) {
            backEndTranslator->addLoopExit(condition, true);
            attemptHandleDominatee(bb, branchInst->getSuccessor(0));
        } else {
            assert(!"Exiting block does not branch to exit");
        }
    }

    // We've been fully handled by now, unless we're a header
    if (! isHeader)
        return;
    assert(isHeader);

    // If we're an unconditional header (e.g. branching immediately to an inner
    // loop), add our target
    if (branchInst->isUnconditional()) {
        handleBlock(branchInst->getSuccessor(0));
    }

    // By this point, we're a header and all of our blocks in our loop should of
    // been handled.
    for (Loop::block_iterator i = loops.top()->block_begin(), e = loops.top()->block_end(); i != e; ++i) {
        if (! handledBlocks.count(*i))
            errs() << " unhandled: " << **i << "\n";
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

    BranchInst* br = dyn_cast<BranchInst>(latches.top()->getTerminator());
    assert(br && br->isUnconditional());

    handleNonTerminatingInstructions(latches.top());

    if (backEnd->getRemovePhiFunctions()) {
        addPhiCopies(br);
    }
}

void BottomTranslator::handleIfBlock(const BasicBlock* bb)
{

    const Conditional* cond = idConds->getConditional(bb);

    // If we don't have a conditional entry for bb, then we're dealing with
    // conditionals with backedges/exits and other tricky control flow in them.
    if (! cond) {
        const BranchInst* br = dyn_cast<BranchInst>(bb->getTerminator());
        assert(br && br->isConditional());

        backEndTranslator->addIf(br->getCondition(), false);
        handleBlock(br->getSuccessor(0));

        backEndTranslator->addElse();
        handleBlock(br->getSuccessor(1));

        backEndTranslator->addEndif();

        return;
    }


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

void BottomTranslator::handleReturnBlock(const BasicBlock* bb)
{
    assert(isa<ReturnInst>(bb->getTerminator()));

    handleNonTerminatingInstructions(bb);
    backEndTranslator->add(bb->getTerminator(), lastBlock);

    return;
}

void BottomTranslator::handleBranchingBlock(const BasicBlock* bb)
{
    const BranchInst* branchInst = dyn_cast<BranchInst>(bb->getTerminator());
    assert(branchInst);

    // Handle it's instructions and do phi node removal if appropriate
    handleNonTerminatingInstructions(bb);
    if (backEnd->getRemovePhiFunctions()) {
        addPhiCopies(branchInst);
    }

    // // TODO: handle for if we're branching into a latch
    // if (Loop* loop = loopInfo->getLoopFor(bb)) {
    //     if (loops.size() && loop == loops.top() && gla::Util::isPredecessor(bb, latches.top()) && !preservedBackedge.top())
    //         gla::UnsupportedFunctionality("inner continues, for now");
    // }

    // If it's unconditional, we'll want to handle any subtrees (and introduced
    // latches) that it points to.
    if (branchInst->isUnconditional()) {
        if (domTree->dominates(bb, branchInst->getSuccessor(0))
            || (loops.size() && !preservedBackedge.top() && branchInst->getSuccessor(0) == latches.top()))
            handleBlock(branchInst->getSuccessor(0));

        return;
    }

    assert(branchInst->getNumSuccessors() == 2 && "Ill-formed conditional branch");

    handleIfBlock(bb);
    return;
}

void BottomTranslator::handleBlock(const BasicBlock* bb)
{
    if (loops.size() && latches.top() == bb && !preservedBackedge.top()) {
        assert(gla::Util::isUnconditional(bb));
        forceOutputLatch();
        backEndTranslator->addLoopBack(NULL, false);
        return;
    }

    if (handledBlocks.count(bb))
        return;
    handledBlocks.insert(bb);

    // Are we on the last block?
    if (handledBlocks.size() == bb->getParent()->getBasicBlockList().size())
        lastBlock = true;

    // If the block exhibits loop-relevant control flow,
    // handle it specially
    Loop* loop = loopInfo->getLoopFor(bb);
    if (loop && (loop->getHeader() == bb || gla::Util::isLatch(bb, loop) || loop->isLoopExiting(bb))
             && flowControlMode == gla::EFcmStructuredOpCodes) {

        if (loop->getHeader() == bb)
            newLoop(bb);

        handleLoopBlock(bb);
        return;
    }

    // If the block's a branching block, handle it specially.
    if (isa<BranchInst>(bb->getTerminator())) {
        handleBranchingBlock(bb);
       return;
    }

    // Otherwise we're a block ending in a return statement
    handleReturnBlock(bb);
    return;
}

bool BottomTranslator::runOnModule(Module& module)
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
    for (Module::const_global_iterator global = module.global_begin(), end = module.global_end(); global != end; ++global)
        backEndTranslator->addGlobal(global);

    //
    // Translate code.
    //
    Module::iterator function, lastFunction;
    for (function = module.begin(), lastFunction = module.end(); function != lastFunction; ++function) {
        if (function->isDeclaration()) {
            //?? do we need to handle declarations of functions, or just definitions?
        } else {

            // Get/set the loop info
            loopInfo  = &getAnalysis<LoopInfo>             (*function);
            domTree   = &getAnalysis<DominatorTree>        (*function);
            idConds   = &getAnalysis<IdentifyConditionals> (*function);
            scalarEvo = &getAnalysis<ScalarEvolution>      (*function);
            lazyInfo  = &getAnalysis<LazyValueInfo>        (*function);

            // debug stuff
            if (gla::Options.debug && loopInfo->begin() != loopInfo->end()) {
                errs() << "\n\nLoop info:\n";        loopInfo->print(errs());
                errs() << "\n\nScalar evolution:\n"; scalarEvo->print(errs());
            }

            // handle function's with bodies

            backEndTranslator->startFunctionDeclaration(function->getFunctionType(), function->getNameStr());

            // paramaters and arguments
            for (Function::const_arg_iterator arg = function->arg_begin(), endArg = function->arg_end(); arg != endArg; ++arg) {
                Function::const_arg_iterator nextArg = arg;
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
            for (Function::const_iterator bb = function->begin(), E = function->end(); bb != E; ++bb) {
                handleBlock(bb);
            }

            backEndTranslator->endFunctionBody();
        }
    }

    //set_branchtargets(&v, currentInstructionructions, num_instructions);
    backEndTranslator->print();

    return false;
}

void BottomTranslator::getAnalysisUsage(AnalysisUsage& AU) const
{
    AU.addRequired<LoopInfo>();
    AU.addRequired<DominatorTree>();
    AU.addRequired<IdentifyConditionals>();
    AU.addRequired<ScalarEvolution>();
    AU.addRequired<LazyValueInfo>();
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

static ModulePass* createBottomTranslatorPass(gla::BackEndTranslator* bet, gla::BackEnd* be)
{
    BottomTranslator* bot = new BottomTranslator();
    bot->setBackEndTranslator(bet);
    bot->setBackEnd(be);
    return bot;
}

// The below are the only externally exposed functionality

void gla::PrivateManager::translateBottomToTarget()
{
    PassManager passManager;
    passManager.add(createBottomTranslatorPass(backEndTranslator, backEnd));
    passManager.run(*module);
}
