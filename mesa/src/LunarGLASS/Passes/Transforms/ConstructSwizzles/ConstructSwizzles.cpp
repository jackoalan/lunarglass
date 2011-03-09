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
#include "llvm/DerivedTypes.h"
#include "llvm/Constants.h"
#include "llvm/Intrinsics.h"
#include "llvm/Instructions.h"

#define INSTRUCTION_COUNT 32
#define GROUP_COUNT 8

#define DEBUG_P 1
#define DEBUG(exp) if (DEBUG_P) exp;

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

        // Add the value to the provided set and vector if it's a candidate
        // instruction. Recursively add its operands.
        void addInstructionRec(Value*, InstSet&, InstVec&);
    };
} // End  namespace

typedef Value* Vec;

struct SwizzleOp {
    int x;
    int y;
    int z;
    int w;
    int mask;
    Vec xV;
    Vec yV;
    Vec zV;
    Vec wV;
    int xO;
    int yO;
    int zO;
    int wO;
    Vec inst;
    Vec original;
};

// Predicate for whether the instruction is an insert
inline bool IsInsert(Instruction &i) {
    return strcmp(i.getOpcodeName(), "insertelement") == 0;
}

// Predicate for whether the instruction is an extract
inline bool IsExtract(Instruction &i) {
    return strcmp(i.getOpcodeName(), "extractelement") == 0;
}

// If the Value is a constant int, return it as an unsigned char. Otherwise return -1
unsigned char GetChar(Value *val) {
    if (ConstantInt *c = dyn_cast<ConstantInt>(val))
        return c->getLimitedValue(255);
    return -1;
}

// Set the offsets for the group
void SetOffsets(ConstructSwizzles::InstVec &vec, SwizzleOp &sop) {
    return;
}

// Given a value, if it's an extract return its offset
// Return -1 if not an extract
int GetOffset(Value *v) {
    // If the operand is an extract instruction, then get the offset
    int offset = -1;
    if (Instruction *inst = dyn_cast<Instruction>(v)) {
        if (IsExtract(*inst)) {
            offset = GetChar(inst->getOperand(1));
        }
    }
    return offset;
}

// If the value is an extract instruction, then get the vector
// extraced from, else return v
Value* GetExtractFrom(Value *v) {
    Value *ret = v;
    if (Instruction *inst = dyn_cast<Instruction>(v)) {
        if (IsExtract(*inst)) {
            ret = (inst->getOperand(0));
        }
    }
    return ret;
}

// SwizzleOp initialize
void InitSOp(SwizzleOp &sop, ConstructSwizzles::InstVec &vec) {
    sop.x = sop.y = sop.z = sop.w = 0;
    sop.xO = sop.yO = sop.zO = sop.wO = -1;
    sop.xV = sop.yV = sop.zV = sop.wV = NULL;
    sop.mask = -1;
    sop.inst = *(vec.begin());
    sop.original = NULL;
    return;
}

// Build up the struct representing a writeMask
void BuildSwizzleOp(ConstructSwizzles::InstVec &vec, SwizzleOp &sop) {

    // Initialize
    InitSOp(sop, vec);

    // Find the orignal insert destination. It will be the last one
    // listed, due to the groups being in depth-first order
    sop.original = vec.back()->getOperand(0);

    // For each member of the group, set the relevant fields.
    for (ConstructSwizzles::InstVec::iterator instI = vec.begin(), instE = vec.end(); instI != instE; ++instI) {

        // Only operate on inserts at the top
        if (!IsInsert(**instI))
            continue;

        // The source operand
        Value *src = (*instI)->getOperand(1);

        // Find the access offset of the underlying extract intrinsic
        int offset = GetOffset(src);

        // The value the extract statement is extracting from
        // If it isn't an extract statement, make it be the scalar
        Value *extractFrom = GetExtractFrom(src);

        // Match up the data with the corresponding field specified in
        // the insert
        switch (GetChar((*instI)->getOperand(2))) {
            case 0:
                sop.x = 1;
                sop.xO = offset;
                sop.xV = extractFrom;
                break;
            case 1:
                sop.y = 1;
                sop.yO = offset;
                sop.yV = extractFrom;
                break;
            case 2:
                sop.z = 1;
                sop.zO = offset;
                sop.zV = extractFrom;
                break;
            case 3:
                sop.w = 1;
                sop.wO = offset;
                sop.wV = extractFrom;
                break;
            default:
                assert(!" Unknown access mask found");
        }
    }
    sop.mask = sop.x*8 + sop.y*4 + sop.z*2 + sop.w;
    return;
}

// Print the block
void PrintBlock(BasicBlock &bb) {
    errs() << "processing basic block " << bb.getName() << "\n" << bb;
    return;
}

// Print the candidates
void PrintCandidates(ConstructSwizzles::InstVec &v) {
    errs() << "\nThis block's candidates for intrinsic substitution: \n";
    for (ConstructSwizzles::InstVec::iterator i = v.begin(), e = v.end(); i != e; ++i) {
        errs() << **i << "\n";
    }
    return;
}

// Print the groups
void PrintGroups(ConstructSwizzles::GroupVec &groupVec) {
    errs() << "\nThis block's groups: \n";
    int i = 0;
    for (ConstructSwizzles::GroupVec::iterator gI = groupVec.begin(), gE = groupVec.end(); gI != gE; ++gI) {
        ++i;
        errs() << "Group " << i << ":";
        for (ConstructSwizzles::InstVec::iterator instI = (*gI)->begin(), instE = (*gI)->end(); instI != instE; ++instI) {
            if (instI == ((*gI)->begin()))
                errs() << **instI;
            else
                errs() <<  "   <|> " << **instI;
        }
        errs() << "\n";
    }

}

// Print out a textual representation of the writeMask operand
void PrintVec(Vec v) {
    if (v)
        errs() << "|" << (*v) << " ";
    else
        errs() << "|  null  ";
}

// Print out the struct representing a writeMask
void PrintSwizzleOp(SwizzleOp &sop) {
    errs() << "\nSwizzleOp for" << *sop.inst << ":\n";
    errs() << "  Write mask and offset:\n";
    errs() << "    " << sop.mask;
    errs() << " <| " << sop.xO << "  |  " << sop.yO << "  |  " << sop.zO << "  |  " << sop.wO;
    errs() << "\n       <";
    PrintVec(sop.xV);
    PrintVec(sop.yV);
    PrintVec(sop.zV);
    PrintVec(sop.wV);
    errs() << "\n";
    errs() << "  Original insert destination: " << *sop.original << "\n";
}

// Print out the created intrinsic instruction
void PrintSwizzleIntrinsic(Instruction &inst) {

    errs() << "Swizzle intrinsic: ";

    errs() << inst << "\n";
    errs() << "\n";
}

// Create an intrinsic
Instruction* MakeSwizzleIntrinsic(SwizzleOp &sop, Module *M, LLVMContext &C) {
    // Set up types array
    const llvm::Type* intrinsicTypes[6] = {0};

    // Determine if it's a fWriteMask or writeMask, and set types accordingly
    Intrinsic::ID intrinsicID;
    unsigned vecCount = 4;
    switch (sop.inst->getType()->getContainedType(0)->getTypeID()) {
    case Type::FloatTyID:
        intrinsicID = Intrinsic::gla_fWriteMask;
        intrinsicTypes[0] = VectorType::get(Type::getFloatTy(C), vecCount);
        break;
    case Type::IntegerTyID:
        intrinsicID = Intrinsic::gla_writeMask;
        intrinsicTypes[0] = VectorType::get(Type::getInt32Ty(C), vecCount);
        break;
    default:
        assert(!"Unknown write mask intrinsic type");
    }

    // Set up each of the operand types
    intrinsicTypes[1] = sop.original ? sop.original->getType() : Type::getFloatTy(C);
    intrinsicTypes[2] = sop.xV       ? sop.xV->getType()       : Type::getFloatTy(C);
    intrinsicTypes[3] = sop.yV       ? sop.yV->getType()       : Type::getFloatTy(C);
    intrinsicTypes[4] = sop.zV       ? sop.zV->getType()       : Type::getFloatTy(C);
    intrinsicTypes[5] = sop.wV       ? sop.wV->getType()       : Type::getFloatTy(C);

    int typesCount = 6;

    // Get the function declaration for this intrinsic
    Value* callee = llvm::Intrinsic::getDeclaration(M, intrinsicID, intrinsicTypes, typesCount);

    Value* mask = ConstantInt::get(Type::getInt32Ty(C), sop.mask);
    Value* xO   = ConstantInt::get(Type::getInt32Ty(C), sop.xO);
    Value* yO   = ConstantInt::get(Type::getInt32Ty(C), sop.yO);
    Value* zO   = ConstantInt::get(Type::getInt32Ty(C), sop.zO);
    Value* wO   = ConstantInt::get(Type::getInt32Ty(C), sop.wO);

    Value* xV   = sop.xV ? sop.xV : Constant::getNullValue(Type::getFloatTy(C));
    Value* yV   = sop.yV ? sop.yV : Constant::getNullValue(Type::getFloatTy(C));
    Value* zV   = sop.zV ? sop.zV : Constant::getNullValue(Type::getFloatTy(C));
    Value* wV   = sop.wV ? sop.wV : Constant::getNullValue(Type::getFloatTy(C));

    Value *args[] = { sop.original, mask, xV, xO, yV, yO, zV, zO, wV, wO };
    Instruction *inst = CallInst::Create(callee, args, args+10);
    return inst;
}

// Insert the new instruction
void InsertSwizzleIntrinsic(ConstructSwizzles::InstVec &vec, Instruction *newInst, BasicBlock &bb) {
    BasicBlock::InstListType &instList = bb.getInstList();
    for (BasicBlock::InstListType::iterator instI = instList.begin(), instE = instList.end(); instI != instE; ++instI) {
        if (instI->isIdenticalTo(*vec.begin())) {
            assert(IsInsert(*instI) && "Internal error: group starting with a non-insert");
            instList.insertAfter(instI, newInst);
            instI->replaceAllUsesWith(newInst);
        }
    }
}

// Add the value to the provided set and vector if it's an insert
// instruction. Recursively add its operands. This effectively
// constructs a depth-first traversal, starting with the insertion destinations
void ConstructSwizzles::addInstructionRec(Value* v, InstSet &s, InstVec &vec) {

    // If it's an instruction and an insert, put it and all it's
    // operands that are inserts recursively into s and vec
    if (Instruction* inst = dyn_cast<Instruction>(v)) {
        if (IsInsert(*inst)) {
            s.insert(inst);
            vec.push_back(inst);

            for (User::op_iterator oi = inst->op_begin(), oe = inst->op_end(); oi != oe; ++oi) {
                addInstructionRec(*oi, s, vec);
            }
        }
    }

    // Base case: not a candidate instruction
    return;
}

// Gather all candidate instructions together
ConstructSwizzles::InstVec* ConstructSwizzles::gather(BasicBlock::InstListType &instList) {
    InstVec *vec = new InstVec();
    for (ConstructSwizzles::reverse_iterator i = instList.rbegin(), e = instList.rend(); i != e; ++i){
        for (/*blank*/; (i != e) && IsInsert(*i); ++i) {
            vec->push_back(&*i);
        }
    }
    return vec;
}

// Group instructions into the individual swizzles
ConstructSwizzles::GroupVec* ConstructSwizzles::group(InstVec &vec) {
    GroupVec *groupVec = new GroupVec();
    InstSet *instSet = new InstSet();
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

        InstVec *newGroup = new InstVec();

        addInstructionRec(inst, *instSet, *newGroup);
        groupVec->push_back(newGroup);
    }

    return groupVec;
}

bool ConstructSwizzles::runOnFunction(Function &F) {
    Module* M = F.getParent();
    LLVMContext &C = F.getContext();

    bool wasModified = false;

    for (Function::iterator bb = F.begin(), ebb = F.end(); bb != ebb; ++bb) {

        DEBUG(PrintBlock(*bb));

        // Gather the candidate instructions
        InstVec *v = gather(bb->getInstList());
        DEBUG(PrintCandidates(*v));

        // Group them
        GroupVec *groupVec = group(*v);
        DEBUG(PrintGroups(*groupVec));

        // For each group, make a SwizzleOp
        for (ConstructSwizzles::GroupVec::iterator gI = groupVec->begin(), gE = groupVec->end(); gI != gE; ++gI) {
            SwizzleOp sop;

            BuildSwizzleOp(**gI, sop);

            DEBUG(PrintSwizzleOp(sop));

            Instruction *inst = MakeSwizzleIntrinsic(sop, M, C);

            InsertSwizzleIntrinsic(**gI, inst, *bb);

            wasModified = true;

            DEBUG(PrintSwizzleIntrinsic(*inst));

        }

        DEBUG(PrintBlock(*bb));

    }
    return wasModified;
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

