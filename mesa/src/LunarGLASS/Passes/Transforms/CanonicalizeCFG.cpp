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
//   * All single predecessor/single successor sequences of basic blocks are
//     condensed into one block. Currently unimplemented.
//
//   * Pointless phi nodes are removed (invalidating LCSSA).
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Dominators.h"
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
        DominatorTree* domTree;

    };
} // end namespace

bool CanonicalizeCFG::runOnFunction(Function& F)
{
    bool changed = false;

    loopInfo = &getAnalysis<LoopInfo>();
    domTree  = &getAnalysis<DominatorTree>();

    // while (removeIndirectExits(F)) {
    //     changed = true;
    // }

    while (removeNoPredecessorBlocks(F)) {
        changed = true;
    }

    // Remove unneeded phi nodes
    SmallVector<PHINode*, 64> deadPHIs;
    for (Function::iterator bbI = F.begin(), bbE = F.end(); bbI != bbE; ++bbI) {
        for (BasicBlock::iterator instI = bbI->begin(), instE = bbI->end(); instI != instE; ++instI) {
            PHINode* pn = dyn_cast<PHINode>(instI);
            if (!pn)
                break;

            Value* v = pn->hasConstantValue(domTree);
            if (!v)
                continue;

            pn->replaceAllUsesWith(v);

            // Remove it
            deadPHIs.push_back(pn);
        }
    }
    for (SmallVector<PHINode*, 64>::iterator i = deadPHIs.begin(), e = deadPHIs.end(); i != e; ++i) {
        (*i)->eraseFromParent();
    }

    // // Remove needless phi nodes from single-predecessor blocks
    // for (Function::iterator bb = F.begin(), e = F.end(); bb != e; ++bb) {
    //     if (&F.getEntryBlock() == &*bb)
    //         continue;
    //     if (++pred_begin(bb) == pred_end(bb))
    //         FoldSingleEntryPHINodes(bb);
    // }

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

void CanonicalizeCFG::getAnalysisUsage(AnalysisUsage& AU) const
{
    AU.addRequired<LoopInfo>();
    AU.addRequired<DominatorTree>();
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
