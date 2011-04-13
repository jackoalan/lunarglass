//===- CanonicalizeCFG.cpp - Canonicalize the CFG for LunarGLASS ----------===//
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
// Author: Michael Ilseman, LunarG
//
//===----------------------------------------------------------------------===//
//
// Canonicalize the CFG for LunarGLASS, this includes the following:
//   * All basic blocks without predecessors are removed.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"

#include "CanonicalizeCFG.h"
#include "LunarGLASSLlvmInterface.h"

using namespace llvm;

namespace  {
    class CanonicalizeCFG : public FunctionPass {
    public:
        // Standard pass stuff
        static char ID;
        CanonicalizeCFG() : FunctionPass(ID) {}
        virtual bool runOnFunction(Function&);
        void print(raw_ostream&, const Module* = 0) const;
        virtual void getAnalysisUsage(AnalysisUsage&) const;

    protected:
        bool removeNoPredecessorBlocks(Function& F);

        // bool removeIndirectExits(Function& F);

        LoopInfo* loopInfo;

    };
} // end namespace

bool CanonicalizeCFG::runOnFunction(Function& F)
{
    bool changed = false;

    loopInfo = &getAnalysis<LoopInfo>();

    // while (removeIndirectExits(F)) {
    //     changed = true;
    // }

    while (removeNoPredecessorBlocks(F)) {
        changed = true;
    }

    return changed;
}

bool CanonicalizeCFG::removeNoPredecessorBlocks(Function& F)
{
    bool changed = false;

    // Loop over all but the entry block
    for (Function::iterator bbI = F.begin(), bbE = F.end(); bbI != bbE; ++bbI) {

        // For some reason, iterating with "bbI = ++F.begin()" instead of
        // testing against "front()" can fail to skip the entry block with the
        // presence of loop-simplify, so make sure we're never deleting the
        // entry block
        if (&F.getEntryBlock() == &*bbI)
            continue;

        // If the block has no predecessors, remove it from the function
        if (pred_begin(bbI) == pred_end(bbI)) {
            changed = true;

            for (succ_iterator sI = succ_begin(bbI), sE = succ_end(bbI); sI != sE; ++sI) {
                (*sI)->removePredecessor(bbI);
            }
            bbI->dropAllReferences();
            bbI = F.getBasicBlockList().erase(bbI);
        }
    }

    return changed;
}

// // Returns true if exits is size 1, or if all the blocks in exits are empty
// // unconditionally branching blocks that branch to the same destination.
// static bool AllProperExits(SmallVector<BasicBlock*, 4>& exits)
// {
//     if (exits.size() == 1)
//         return true;

//     BranchInst* bi = dyn_cast<BranchInst>(exits[0]->getTerminator());
//     if (!bi)
//         return false;

//     BasicBlock* target = bi->getSuccessor(0);

//     for (SmallVector<BasicBlock*,4>::iterator bbI = exits.begin(), bbE = exits.end(); bbI != bbE; ++bbI) {
//         BranchInst* bi = dyn_cast<BranchInst>((*bbI)->getTerminator());
//         if (!bi || bi->isConditional() || (bi->getSuccessor(0) != target))
//             return false;
//     }

//     return true;
// }

// bool CanonicalizeCFG::removeIndirectExits(Function& F)
// {
//     bool changed = false;

//     SmallVector<BasicBlock*, 4> exits;

//     // TODO: traverse the loopInfo structure instead of F
//     for (Function::iterator bbI = F.begin(), bbE = F.end(); bbI != bbE; ++bbI) {
//         Loop* loop = loopInfo->getLoopFor(bbI);
//         if (!loop || !loop->hasDedicatedExits())
//             continue;

//         exits.clear();

//         loop->getUniqueExitBlocks(exits);

//         if (!AllProperExits(exits))
//             continue;

//         for (SmallVector<BasicBlock*, 4>::iterator i = exits.begin(), e = exits.end(); i != e; ++i) {
//             if (isa<BranchInst>((*i)->getTerminator())) {
//                 changed |= TryToSimplifyUncondBranchFromEmptyBlock(*i);
//             }
//         }

//     }

//     return changed;
// }


void CanonicalizeCFG::getAnalysisUsage(AnalysisUsage& AU) const
{
    AU.addRequired<LoopInfo>();
    return;
}

void CanonicalizeCFG::print(raw_ostream&, const Module*) const
{
    return;
}

char CanonicalizeCFG::ID = 0;
INITIALIZE_PASS(CanonicalizeCFG,
                "canonicalize-cfg",
                "Canonicalize the CFG for LunarGLASS",
                false,  // Whether it preserves the CFG
                false); // Whether it is an analysis pass
FunctionPass* llvm::createCanonicalizeCFGPass()
{
    return new CanonicalizeCFG();
}
