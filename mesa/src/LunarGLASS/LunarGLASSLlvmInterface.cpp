//===- LunarGLASSLlvmInterface.cpp - Help build/query LLVM for LunarGLASS -===//
//
// LunarGLASS: An Open Modular Shader Compiler Architecture
// Copyright (c) 2011, LunarG, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice (including the next
// paragraph) shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//===----------------------------------------------------------------------===//
//
// Author: John Kessenich, LunarG
// Author: Cody Northrop, LunarG
// Author: Michael Ilseman, LunarG
//
//===----------------------------------------------------------------------===//

#include "LunarGLASSLlvmInterface.h"

// LLVM includes
#include "llvm/BasicBlock.h"
#include "llvm/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CFG.h"

namespace gla {

llvm::Value* Top::buildMatrixTimesVector(llvm::Value* lmatrix, llvm::Value* rvector)
{
    return 0;
}

llvm::Value* Top::buildVectorTimesMatrix(llvm::Value* lvector, llvm::Value* rmatrix)
{
    return 0;
}

llvm::Value* Top::buildMatrixTimesMatrix(llvm::Value* lmatrix, llvm::Value* rmatrix)
{
    return 0;
}

llvm::Value* Top::buildOuterProduct(llvm::Value* lvector, llvm::Value* rvector)
{
    return 0;
}

llvm::Value* Top::buildMatrixTranspose(llvm::Value*  matrix)
{
    return 0;
}

llvm::Value* Top::buildMatrixInverse(llvm::Value*  matrix)
{
    return 0;
}

int Util::getConstantInt(const llvm::Value* value)
{
    const llvm::Constant* constant = llvm::dyn_cast<llvm::Constant>(value);
    assert(constant);
    const llvm::ConstantInt *constantInt = llvm::dyn_cast<llvm::ConstantInt>(constant);
    assert(constantInt);
    return constantInt->getValue().getSExtValue();
}

float Util::GetConstantFloat(const llvm::Value* value)
{
    const llvm::Constant* constant = llvm::dyn_cast<llvm::Constant>(value);
    assert(constant);
    const llvm::ConstantFP *constantFP = llvm::dyn_cast<llvm::ConstantFP>(constant);
    assert(constantFP);
    return constantFP->getValueAPF().convertToFloat();
}

int Util::getComponentCount(const llvm::Type* type)
{
    const llvm::VectorType *vectorType = llvm::dyn_cast<llvm::VectorType>(type);

    if (vectorType)
        return vectorType->getNumElements();
    else
        return 1;
}

int Util::getComponentCount(const llvm::Value* value)
{
    const llvm::Type* type = value->getType();

    return Util::getComponentCount(type);
}

bool Util::isConsecutiveSwizzle(int glaSwizzle, int width)
{
    for (int i = 0; i < width; ++i) {
        if (((glaSwizzle >> i*2) & 0x3) != i)
            return false;
    }

    return true;
}

bool Util::isGlaBoolean(const llvm::Type* type)
{
    if (llvm::Type::VectorTyID == type->getTypeID()) {
        if (type->getContainedType(0) == type->getInt1Ty(type->getContext()))
            return true;
    } else {
        if (type == type->getInt1Ty(type->getContext()))
            return true;
    }

    return false;
}

bool Util::hasAllSet(const llvm::Value* value)
{
    if (! llvm::isa<llvm::Constant>(value))
        return false;

    if (isGlaScalar(value->getType())) {
        return Util::getConstantInt(value) == -1;
    } else {
        const llvm::ConstantVector* vector = llvm::dyn_cast<llvm::ConstantVector>(value);
        assert(vector);

        for (int op = 0; op < vector->getNumOperands(); ++op) {
            if (Util::getConstantInt(vector->getOperand(op)) != -1)
                return false;
        }

        return true;
    }
}

// true if provided basic block is one of the (possibly many) latches in the provided loop
bool Util::isLatch(const llvm::BasicBlock* bb, llvm::Loop* loop)
{
    if (!loop)
        return false;

    llvm::BasicBlock* header = loop->getHeader();
    for (llvm::succ_const_iterator sI = succ_begin(bb), sE = succ_end(bb); sI != sE; ++sI) {
        if (*sI == header)
            return true;
    }

    return false;
}

// Return the number of latches in a loop
int Util::getNumLatches(llvm::Loop* loop)
{
    if (!loop)
        return 0;

    int count = 0;
    for (llvm::Loop::block_iterator bbI = loop->block_begin(), bbE = loop->block_end(); bbI != bbE; ++bbI) {
        if (isLatch(*bbI, loop)) {
            count++;
        }
    }

    return count;
}


}; // end gla namespace

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

// Do a breadth-first search until some common successor is found. Return that
// successor.
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
llvm::BasicBlock* gla::Util::findEarliestConfluencePoint(llvm::BasicBlock* leftBB, llvm::BasicBlock* rightBB)
{
    BFSCFG bfsCfg;

    bfsCfg.addToVisit(leftBB);
    bfsCfg.addToVisit(rightBB);

    return bfsCfg.findCommon();
}
