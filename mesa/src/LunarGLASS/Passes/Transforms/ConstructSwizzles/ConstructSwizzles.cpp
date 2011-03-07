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
#include "llvm/ADT/SmallSet.h"
#include "llvm/Type.h"

#define INSTRUCTION_COUNT 16
#define GROUP_COUNT 8

using namespace llvm;

namespace {
    class ConstructSwizzles : public FunctionPass {
    public:
        // Typedefs
        typedef iplist<Instruction>::reverse_iterator reverse_iterator;
        typedef SmallVector<Instruction*,INSTRUCTION_COUNT> InstVec;
        typedef SmallSet<Instruction*, INSTRUCTION_COUNT> InstSet;
        typedef SmallVector<InstVec*, GROUP_COUNT> GroupVec;

        // Rest is standard pass stuff
        static char ID;
        ConstructSwizzles() : FunctionPass(ID) {}
        virtual bool runOnFunction(Function&);
        void print(raw_ostream&, const Module* = 0) const;
        virtual void getAnalysisUsage(AnalysisUsage&) const;

    private:
        // Gather all contiguous candidate instructions together
        InstVec* gather(BasicBlock::InstListType&);

        // Group instructions into the individual swizzles
        GroupVec* group(InstVec&);

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

        //print block
        errs() << "processing basic block " << bb->getName() << "\n" << *bb;

        // Gather the candidate instructions
        InstVec *v = gather(bb->getInstList());

        // print candidates
        errs() << "\nThis block's candidates: \n";
        for (InstVec::iterator i = v->begin(), e = v->end(); i != e; ++i) {
            errs() << **i << "\n";
        }

        // Group them
        GroupVec *groupVec = group(*v);

        // print groups
        errs() << "\nThis block's groups: \n";
        int i = 0;
        for (GroupVec::iterator gI = groupVec->begin(), gE = groupVec->end(); gI != gE; ++gI) {
            ++i;
            errs() << "Group " << i << ":";
            for (InstVec::iterator instI = (*gI)->begin(), instE = (*gI)->end(); instI != instE; ++instI) {
                if (instI == ((*gI)->begin()))
                    errs() << **instI;
                else
                    errs() <<  "   <|> " << **instI;
            }
            errs() << "\n";
        }
    }
    return false;
}

// Add the value to the provided set and vector if it's a candidate
// instruction. Recursively add its operands.
void AddInstructionRec(Value* v, ConstructSwizzles::InstSet &s, ConstructSwizzles::InstVec &vec) {

    // If it's an instruction and a candidate, insert it and all it's
    // operands recursively
    if (Instruction* inst = dyn_cast<Instruction>(v)) {
        if (IsCandidate(*inst)) {
            s.insert(inst);
            vec.push_back(inst);
            for (User::op_iterator oi = inst->op_begin(), oe = inst->op_end(); oi != oe; ++oi) {
                AddInstructionRec(*oi, s, vec);
            }
        }
    }

    // Base case: not a candidate instruction
    return;
}


// Gather all candidate instructions together
ConstructSwizzles::InstVec* ConstructSwizzles::gather(BasicBlock::InstListType &instList) {
    ConstructSwizzles::InstVec *vec = new ConstructSwizzles::InstVec();
    for (ConstructSwizzles::reverse_iterator i = instList.rbegin(), e = instList.rend(); i != e; ++i){
        if (IsCandidate(*i)) {
            for (/*blank*/; (i != e) && IsCandidate(*i); ++i) {
                vec->push_back(&*i);
            }
        }
    }
    return vec;
}

// Group instructions into the individual swizzles
ConstructSwizzles::GroupVec* ConstructSwizzles::group(InstVec &vec) {
    ConstructSwizzles::GroupVec *groupVec = new ConstructSwizzles::GroupVec();
    ConstructSwizzles::InstSet *instSet = new ConstructSwizzles::InstSet();
    for (InstVec::iterator i = vec.begin(), e = vec.end(); i != e; ++i) {

        // Convert to an instruction (should not fail)
        Instruction *inst = dyn_cast<Instruction>(&**i);
        if (!inst) {
            assert(!"attempting to gather non-instructions");
        }

        // If we've already seen it, continue
        if (instSet->count(inst)) {
            continue;
        }

        // Else this is a new group

        ConstructSwizzles::InstVec *newGroup = new ConstructSwizzles::InstVec();

        AddInstructionRec(inst, *instSet, *newGroup);
        groupVec->push_back(newGroup);
    }

    return groupVec;
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

