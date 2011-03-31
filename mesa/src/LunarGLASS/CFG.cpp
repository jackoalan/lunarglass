//===- CFG.cpp - CFG helpers ----------------------------------------------===//
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
// Provide some Control Flow Graph helpers
//
//===----------------------------------------------------------------------===//

#include "CFG.h"

#include "llvm/Support/CFG.h"
#include "llvm/BasicBlock.h"
#include "llvm/Instructions.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace  {

    typedef llvm::SmallPtrSet<llvm::BasicBlock*, 8> BBSet;
    typedef llvm::SmallVector<llvm::BasicBlock*, 8> BBVector;

    // Class for breadth first searches of a control flow graph
    class BFSCFG {
    public:

        BFSCFG() { }

        // Do a breadth-first search until some common successor is
        // found. Return that successor.
        llvm::BasicBlock* findCommon();

        void addToVisit(llvm::BasicBlock* bb) { toVisit.push_back(bb); }

    private:
        BBSet visited;
        BBVector toVisit;

    };
} // end namespace


llvm::BasicBlock* BFSCFG::findCommon() {
    BBVector children;
    for (BBVector::iterator i = toVisit.begin(), e = toVisit.end(); i != e; ++i) {
        if (visited.count(*i))
            return *i;
        visited.insert(*i);

        // Add all the children of each bb, unless the bb ends in a return
        if (llvm::isa<llvm::ReturnInst>((*i)->getTerminator()))
            continue;
        for (llvm::succ_iterator sI = succ_begin(*i), sE = succ_end(*i); sI != sE; ++sI) {
            children.push_back(*sI);
        }
    }

    toVisit.clear();
    toVisit = children;
    return findCommon();
}




// Find and return the earliest confluence point in the CFG
llvm::BasicBlock* gla::FindEarliestConfluencePoint(llvm::BasicBlock* leftBB, llvm::BasicBlock* rightBB)
{
    BFSCFG bfsCfg;

    bfsCfg.addToVisit(leftBB);
    bfsCfg.addToVisit(rightBB);

    return bfsCfg.findCommon();
}

