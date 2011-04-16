//===- LunarGLASSLlvmInterface.h - Help build/query LLVM for LunarGLASS -=====//
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
//
//===----------------------------------------------------------------------===//

#pragma once
#ifndef LunarGLASSLlvmInterface_H
#define LunarGLASSLlvmInterface_H

// LLVM includes
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IntrinsicInst.h"

// Forward decls
namespace llvm {
    class BasicBlock;
    class Loop;
    class DominanceFrontier;
} // end namespace llvm

namespace gla {

    //
    // Builder is an interface to help build (along with LLVM) the top IR.
    // These structures are not part of the definition of the top IR;
    // just helpers to build the Top IR.
    //

    class Builder {
    public:

        //
        // Matrix is a structure to encapsulate a set of LLVM values
        // comprising a source-level (above TopIR) matrix.
        //
        class Matrix {
        public:
            Matrix(int c, int r, llvm::Value* vectors[]);
            Matrix(int c, int r, Matrix*);

            int getNumRows() const { return numRows; }
            int getNumColumns() const { return numColumns; }

            void setColumn(int column, const llvm::Value* vector) { columns[column] = vector; }
            const llvm::Value* getMatrixColumn(int c) const { return columns[c]; }

        protected:
            int numColumns;
            int numRows;
            const llvm::Value* columns[4];
        };

        class SuperValue {
        public:
            SuperValue() : type(ELlvm) { value.llvm = 0; }

            // These are both constructors and implicit conversions
            SuperValue(llvm::Value* llvm) : type(ELlvm) { value.llvm = llvm; } // implicitly make a SuperValue out of a Value
            SuperValue(Matrix* m) : type(EMatrix) { value.matrix = m; }        // implicitly make a SuperValue out of a Matrix

            // implicitly make a Value out of a SuperValue
            operator llvm::Value*() const
            {
                assert(type == ELlvm);
                return value.llvm;
            }

            // make a Value when derefencing a SuperValue
            llvm::Value* operator->() const
            {
                assert(type == ELlvm);
                return value.llvm;
            }

            void clear()
            {
                type = ELlvm;
                value.llvm = 0;
            }

            bool isMatrix() const { return type == EMatrix; }
            bool isValue() const { return type == ELlvm; }

            llvm::Value* getValue() const
            {
                assert(type == ELlvm);
                return value.llvm;
            }

            Matrix* getMatrix() const
            {
                assert(type == EMatrix);
                return value.matrix;
            }

        protected:
            enum {
                EMatrix,
                ELlvm
            } type;

            union {
                Matrix* matrix;
                llvm::Value* llvm;
            } value;
        };

        // handle component-wise matrix operations for either a
        // pair of matrices or a matrix and a scalar
        static SuperValue createMatrixOp(llvm::IRBuilder<>&, unsigned llvmopcode, SuperValue left, SuperValue right);

        // handle all the possible matrix-related multiply operations
        // (non component wise; linear algebraic) for all combinations
        // of matrices, scalars, and vectors that either consume or
        // create a matrix
        static SuperValue createMatrixMultiply(llvm::IRBuilder<>&, SuperValue left, SuperValue right);

        // handle matrix to matrix operations
        static Matrix* createMatrixTranspose  (llvm::IRBuilder<>&, Matrix*);
        static Matrix* createMatrixInverse    (llvm::IRBuilder<>&, Matrix*);
        static Matrix* createMatrixDeterminant(llvm::IRBuilder<>&, Matrix*);

    protected:
        static llvm::Value* createMatrixTimesVector(llvm::IRBuilder<>&, Matrix*, llvm::Value*);
        static llvm::Value* createVectorTimesMatrix(llvm::IRBuilder<>&, llvm::Value*, Matrix*);
        static llvm::Value* createSmearedMatrixOp  (llvm::IRBuilder<>&, unsigned llvmopcode, Matrix*, llvm::Value*);
        static llvm::Value* createSmearedMatrixOp  (llvm::IRBuilder<>&, unsigned llvmopcode, llvm::Value*, Matrix*);

        static Matrix* createMatrixTimesMatrix(llvm::IRBuilder<>&, Matrix*, Matrix*);
        static Matrix* createOuterProduct     (llvm::IRBuilder<>&, llvm::Value* lvector, llvm::Value* rvector);
    };

    //
    // some utility query functions
    //

    class Util {
    public:

        // get integer value or assert trying
        static int getConstantInt(const llvm::Value*);

        // get floating point value or assert trying
        static float GetConstantFloat(const llvm::Value*);

        static int isGradientTexInst(const llvm::IntrinsicInst* instruction)
        {
            return (instruction->getIntrinsicID() == llvm::Intrinsic::gla_fTextureSampleLodOffsetGrad);
        }

        static int getComponentCount(const llvm::Type*);
        static int getComponentCount(const llvm::Value*);
        static bool isConsecutiveSwizzle(int glaSwizzle, int width);

        // Whether the argument is undefined or defined (an undef in llvm)
        static bool isUndef(const llvm::Value* val) { return llvm::isa<llvm::UndefValue>(val); }
        static bool isDefined(const llvm::Value* val) { return !isUndef(val); }

        // true if a scalar Boolean or vector of Boolean
        static bool isGlaBoolean(const llvm::Type*);

        static bool isGlaScalar(const llvm::Type* type) { return llvm::Type::VectorTyID != type->getTypeID(); }
        static bool isGlaScalar(const llvm::Value* value) { return isGlaScalar(value->getType()); }

        static bool isVector(const llvm::Type* type) { return type->getTypeID() == llvm::Type::VectorTyID; }
        static bool isVector(const llvm::Value* value) { return isVector(value->getType()); }

        // true if all bits in the argument are set
        static bool hasAllSet(const llvm::Value*);

        // is the name something like "%42"?
        static bool isTempName(const std::string& name)
        {
            return name.length() < 2 || (name[1] >= '0' && name[1] <= '9');
        }

        // Whether the block terminates in a conditional branch
        static bool isConditional(const llvm::BasicBlock* bb)
        {
            if (const llvm::BranchInst* bi = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator()))
                return bi->isConditional();

            return false;
        }

        // Whether the block terminates in a unconditional branch
        static bool isUnConditional(const llvm::BasicBlock* bb)
        {
            return !isConditional(bb);
        }

        // Whether block A is a predecessor of B
        static bool isPredecessor(const llvm::BasicBlock* pred, const llvm::BasicBlock* succ)
        {
            for (llvm::const_pred_iterator i = llvm::pred_begin(succ), e = llvm::pred_end(succ); i != e; ++i) {
                if (*i == pred)
                    return true;
            }

            return false;
        }


        // true if provided basic block is one of the (possibly many) latches in
        // the provided loop
        static bool isLatch(const llvm::BasicBlock* bb, llvm::Loop* loop);

        // Return the number of latches in a loop
        static int getNumLatches(llvm::Loop* loop);

        // Whether a basic block has no constituent instructions, other than
        // it's phi-nodes and terminator.
        static bool isEmptyBB(const llvm::BasicBlock* bb)
        {
            return bb->getFirstNonPHIOrDbg() == bb->getTerminator();
        }

        // Whether a SmallVector contains the given element
        template<typename T>
        static bool smallVectorContains(llvm::SmallVectorImpl<T>& vec, T val)
        {
            // We need to typedef it (with a typename) to access its iterator
            for (typename llvm::SmallVectorImpl<T>::iterator i = vec.begin(), e = vec.end(); i != e; ++i) {
                if (&**i == &*val)
                    return true;
            }

            return false;
        }

        // Return the single merge point of the given conditional basic block. Returns
        // null if there is no merge point, or if there are more than 1 merge
        // points. Note that the presense of backedges or exitedges in the then and else
        // branchs' subgraphs may cause there to be multiple potential merge points.
        static llvm::BasicBlock* getSingleMergePoint(const llvm::BasicBlock* condBB, llvm::DominanceFrontier& domFront);

    };
};

#endif // LunarGLASSLlvmInterface_H
