//===- ConstructSwizzles.cpp - Coalesce insert/extracts into swizzles -----===//
//
//                     The LLVM Compiler Infrastructure
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
// Coalesce insert/extracts into swizzles
//
//===----------------------------------------------------------------------===//


#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "ConstructSwizzles.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;

namespace {
    struct ConstructSwizzles : public FunctionPass {

        // Rest is standard pass stuff
        static char ID;
        ConstructSwizzles() : FunctionPass(ID) {}
        virtual bool runOnFunction(Function&);
        void print(raw_ostream&, const Module* = 0) const;
        virtual void getAnalysisUsage(AnalysisUsage&) const;

    private:
        typedef iplist<Instruction>::reverse_iterator reverse_iterator;
        typedef SmallVector<reverse_iterator,8> InstVec;

        // Gather all contiguous candidate instructions together
        InstVec* gather(reverse_iterator&, reverse_iterator&);

        // Group instructions into the individual swizzles
        SmallVector<InstVec*, 4>* group(InstVec&);
    };
} // End  namespace

// Predicate for whether the instruction is an insert
inline bool IsInsert(Instruction &i) {
    return strcmp(i.getOpcodeName(), "insertelement") == 0;
}

// Predicate for whether the instruction is an extract
inline bool IsExtract(Instruction &i) {
    return strcmp(i.getOpcodeName(), "extractelement") == 0;
}

// Predicate for whether the instruction is the last use in the
// function. TODO: implement

// inline bool IsLastUse(Instruction &i) {
//     return false
// }

// Predicate for whether the instruction is a swizzle component
// candidate
inline bool IsCandidate(Instruction &i) {
    return (IsInsert(i) || IsExtract(i)) && i.hasOneUse();
}

bool ConstructSwizzles::runOnFunction(Function &F) {
    for (Function::iterator bb = F.begin(), ebb = F.end(); bb != ebb; ++bb) {
        errs() << "processing basic block " << bb->getName() << "\n";
        BasicBlock::InstListType &instList = bb->getInstList();
        for (ConstructSwizzles::reverse_iterator i = instList.rbegin(), e = instList.rend(); i != e; ++i){
            if (IsCandidate(*i)) {
                ConstructSwizzles::InstVec *vec = new SmallVector<ConstructSwizzles::reverse_iterator, 8>();
                for (/*blank*/; (i != e) && IsCandidate(*i); ++i) {
                    errs() << "  " << *i;
                    errs() << "\t\t;| " << IsCandidate(*i) << "\n";
                    vec->push_back(i);
                }
            } else
                errs() << "  " << *i << "\n";
        }
    }
    return false;
}

void ConstructSwizzles::getAnalysisUsage(AnalysisUsage& AU) const {
    return;
}

void ConstructSwizzles::print(raw_ostream &out, const Module* M) const {
    return;
}

// Rest is pass registration

char ConstructSwizzles::ID = 0;
INITIALIZE_PASS(ConstructSwizzles,
                "construct-swizzles",
                "Construct swizzles out of multiple insert/extracts",
                true,   // Whether it preserves the CFG
                false); // Whether it is an analysis pass

namespace llvm {
    FunctionPass* createConstructSwizzlesPass();
} // End llvm namespace

FunctionPass* llvm::createConstructSwizzlesPass() {
    return new ConstructSwizzles();
}

