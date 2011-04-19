//===- Loops.h - Utility functions and wrappers for loops -----------------===//
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
// Provide a utility wrapper around llvm::Loop, and utility functions for
// dealing with and analyzing loops
//
//===----------------------------------------------------------------------===//

#ifndef LOOP_UTIL_H
#define LOOP_UTIL_H

#include "llvm/Analysis/LoopInfo.h"

#include "Passes/Util/BasicBlockUtil.h"

namespace llvm {

    // Loop wrapper providing more queries/information
    class LoopUtil {
    public:
        LoopUtil(LoopInfo* li, Loop* l)
            : loopInfo(li)
            , loop(l)
            , header(loop->getHeader())
            , latch(loop->getLoopLatch())
        {
            loop->getUniqueExitBlocks(exits);
            preservedBackedge = IsConditional(latch) || latch->getSinglePredecessor();

        }

        // Whether the loop is a canonical, structured loop.  In a canonical,
        // structured loop there should only be one latch.  Tf the latch is
        // conditional (and thus preserved), the other branch should be an
        // exiting branch (enforces bottom-latching semantics).  If there are
        // multiple exit blocks, they should all eventually merge to a single
        // point that lies in the intersection of each of their dominance
        // frontiers (structured flow control).
        bool isCanonical();

    protected:
        LoopInfo* loopInfo;
        Loop* loop;

        BasicBlock* header;
        BasicBlock* latch;
        SmallVector<BasicBlock*, 4> exits;

        bool preservedBackedge;


    private:
        ~LoopUtil();                     // do not implement
        LoopUtil(const LoopUtil&);       // do not implement
        void operator=(const LoopUtil&); // do not implement

    };



} // end namespace llvm


#endif // LOOP_UTIL_H
