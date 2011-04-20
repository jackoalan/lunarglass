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

#include <stack>

namespace llvm {
    class DominanceFrontier;

    // Loop wrapper providing more queries/information
    class LoopWrapper {
    public:
        LoopWrapper(/*LoopInfo* li,*/ Loop* l, DominanceFrontier* df)
            : loop(l)
            , domFront(df)
            , header(loop->getHeader())
            , latch(loop->getLoopLatch())
            , preservedBackedge(IsConditional(latch) || latch->getSinglePredecessor())
        {
            loop->getUniqueExitBlocks(exits);
            exitMerge = GetSingleMergePoint(exits, *domFront);
        }

        // Accessors
        BasicBlock* getHeader()    const { return header; }
        BasicBlock* getLatch()     const { return latch; }
        BasicBlock* getExitMerge() const { return exitMerge; }
        bool preservesBackedge()   const { return preservedBackedge; }

        // Wrapped functionality
        unsigned getLoopDepth()                  const { return loop->getLoopDepth(); }
        bool isLoopExiting(const BasicBlock* bb) const { return loop->isLoopExiting(bb); }
        bool contains(const BasicBlock* bb)      const { return loop->contains(bb); }
        Loop::block_iterator block_begin()       const { return loop->block_begin(); }
        Loop::block_iterator block_end()         const { return loop->block_end(); }

        // New functionality

        // Whether the loop is a canonical, structured loop.  In a canonical,
        // structured loop there should only be one latch.  Tf the latch is
        // conditional (and thus preserved), the other branch should be an
        // exiting branch (enforces bottom-latching semantics).  If there are
        // multiple exit blocks, they should all eventually merge to a single
        // point that lies in the intersection of each of their dominance
        // frontiers (enforces structured flow control).
        bool isCanonical()
        {
            return header && latch && exitMerge
                && (IsUnconditional(latch) || loop->isLoopExiting(latch));
        }

        // Returns the successor number (0 or 1) of the exiting edge from an exiting
        // block. Returns -1 if none exit, and 2 if they both exit.
        int exitSuccNumber(const BasicBlock* bb)
        {
            if (! isLoopExiting(bb))
                return -1;

            const BranchInst* br = dyn_cast<BranchInst>(bb->getTerminator());

            int count = 0;

            if (contains(br->getSuccessor(0)))
                count++;

            if (contains(br->getSuccessor(1)))
                count++;

            if (count == 2)
                return 2;

            // Else return 0 or 1 indicating which one is not contained
            return contains(br->getSuccessor(0));
        }

        ~LoopWrapper() { }

    protected:
        // LoopInfo* loopInfo;
        Loop* loop;
        DominanceFrontier* domFront;

        BasicBlock* header;
        BasicBlock* latch;

        SmallVector<BasicBlock*, 4> exits;

        BasicBlock* exitMerge;

        bool preservedBackedge;

    private:
        LoopWrapper(const LoopWrapper&);       // do not implement
        void operator=(const LoopWrapper&); // do not implement

    };

    class LoopStack {
    public:
        LoopStack()
        { }

        void newLoop(const BasicBlock* bb) { st.push(new LoopWrapper(loopInfo->getLoopFor(bb), domFront)); }

        int size() { return st.size(); }

        LoopWrapper* top() { return st.top(); }

        void pop()
        {
            LoopWrapper* lw = top();
            st.pop();
            delete lw;
        }

        void clear()
        {
            while (size())
                pop();
        }

        void setDominanceFrontier(DominanceFrontier* df) { domFront = df; }

        void setLoopInfo(LoopInfo* li) { loopInfo = li; }

        ~LoopStack()
        {
            clear();
        }

    protected:
        DominanceFrontier* domFront;
        LoopInfo* loopInfo;

        std::stack<LoopWrapper*> st;

    private:
        LoopStack(const LoopStack&);      // do not implement
        void operator=(const LoopStack&); // do not implement

    };


} // end namespace llvm


#endif // LOOP_UTIL_H
