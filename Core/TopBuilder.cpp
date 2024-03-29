//===- TopBuilder.cpp - Generic Top IR builders -=============================//
//
// LunarGLASS: An Open Modular Shader Compiler Architecture
// Copyright (C) 2010-2014 LunarG, Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
//     Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
// 
//     Redistributions in binary form must reproduce the above
//     copyright notice, this list of conditions and the following
//     disclaimer in the documentation and/or other materials provided
//     with the distribution.
// 
//     Neither the name of LunarG Inc. nor the names of its
//     contributors may be used to endorse or promote products derived
//     from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//===----------------------------------------------------------------------===//
//
// Author: John Kessenich, LunarG
// Author: Cody Northrop, LunarG
// Author: Michael Ilseman, LunarG
//
//===----------------------------------------------------------------------===//

// LLVM includes
#pragma warning(push, 1)
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CFG.h"
#pragma warning(pop)

// LunarGLASS includes
#include "Exceptions.h"
#include "TopBuilder.h"
#include "PrivateManager.h"
#include "Backend.h"

#ifndef _WIN32
    #include <cstdio>
#endif

namespace gla {

bool Manager::startMultithreaded()
{
    return LLVMStartMultithreaded() != 0;
}

Builder::Builder(llvm::IRBuilder<>& b, gla::Manager* m, Metadata md) :
    insertNoPredecessorBlocks(true),
    accessRightToLeft(true),
    builder(b),
    manager(m),
    module(manager->getModule()),
    context(builder.getContext()),
    metadata(md),
    mainFunction(0),
    stageEpilogue(0),
    stageExit(0),
    explicitPipelineCopyout(false)
{
    clearAccessChain();
    int cacheSize = maxMatrixSize - minMatrixSize + 1;
    for (int c = 0; c < cacheSize; ++c)
        for (int r = 0; r < cacheSize; ++r)
            matrixTypeCache[c][r] = 0;
}

Builder::~Builder()
{
}

void Builder::clearAccessChain()
{
    accessChain.base = 0;
    accessChain.indexChain.clear();
    accessChain.gep = 0;
    accessChain.swizzle.clear();
    accessChain.component = 0;
    accessChain.swizzleResultType = 0;
    accessChain.swizzleTargetWidth = 0;
    accessChain.isRValue = false;
    accessChain.trackActive = false;
    accessChain.mdNode = 0;
    accessChain.metadataKind = 0;
}

// Comments in header
void Builder::accessChainPushSwizzleLeft(llvm::ArrayRef<int> swizzle, llvm::Type* type, int width)
{
    // if needed, propagate the swizzle for the current access chain
    if (accessChain.swizzle.size()) {
        for (unsigned int i = 0; i < accessChain.swizzle.size(); ++i) {
            accessChain.swizzle[i] = swizzle[accessChain.swizzle[i]];
        }
    } else {
        accessChain.swizzleResultType = type;
        accessChain.swizzle = swizzle;
    }

    // track width the swizzle operates on
    accessChain.swizzleTargetWidth = width;

    // determine if we need to track this swizzle anymore
    simplifyAccessChainSwizzle();
}

// Comments in header
void Builder::accessChainPushSwizzleRight(llvm::ArrayRef<int> swizzle, llvm::Type* type, int width)
{
    // if needed, propagate the swizzle for the current access chain
    if (accessChain.swizzle.size()) {
        std::vector<int> oldSwizzle = accessChain.swizzle;
        accessChain.swizzle.resize(0);
        for (unsigned int i = 0; i < swizzle.size(); ++i) {
            accessChain.swizzle.push_back(oldSwizzle[swizzle[i]]);
        }
    } else {
        accessChain.swizzle = swizzle;
    }

    // track the final type, which always changes with each push
    accessChain.swizzleResultType = type;

    // track width the swizzle operates on; once known, it does not change
    if (accessChain.swizzleTargetWidth == 0)
        accessChain.swizzleTargetWidth = width;

    // determine if we need to track this swizzle anymore
    simplifyAccessChainSwizzle();
}

// clear out swizzle if it is redundant
void Builder::simplifyAccessChainSwizzle()
{
    // if swizzle has fewer components than our target, it is a writemask
    if (accessChain.swizzleTargetWidth > (int)accessChain.swizzle.size())
        return;

    // if components are out of order, it is a swizzle
    for (unsigned int i = 0; i < accessChain.swizzle.size(); ++i) {
        if (i != accessChain.swizzle[i])
            return;
    }

    // otherwise, there is no need to track this swizzle
    accessChain.swizzle.clear();
    accessChain.swizzleTargetWidth = 0;
}


// Comments in header
void Builder::setAccessChainRValue(llvm::Value* rValue)
{
    // We don't support exposed pointers, so no r-value should be a pointer.
    // If code is calling this with a pointer, it should probably be calling
    // setAccessChainLValue() instead.
    assert(! llvm::isa<llvm::PointerType>(rValue->getType()));

    accessChain.isRValue = true;
    accessChain.base = rValue;

    // Because we might later turn an r-value into an l-value, just
    // to use the GEP mechanism for complex r-value dereferences,
    // push a pointer dereference now.
    accessChain.indexChain.push_back(MakeIntConstant(context, 0));
}

// Comments in header
void Builder::setAccessChainLValue(llvm::Value* lValue)
{
    // l-values need to be allocated somewhere, so we expect a pointer.
    assert(llvm::isa<llvm::PointerType>(lValue->getType()));

    // Pointers need to push a 0 on the gep chain to dereference them.
    accessChain.indexChain.push_back(MakeIntConstant(context, 0));

    accessChain.base = lValue;
}

// Comments in header
void Builder::setAccessChainPipeValue(llvm::Value* val)
{
    // evolve the accessChain
    accessChain.indexChain.clear();

    setAccessChainRValue(val);
}

// Comments in header
llvm::Value* Builder::collapseAccessChain()
{
    assert(accessChain.isRValue == false);

    if (accessChain.indexChain.size() > 1) {
        if (accessChain.gep == 0) {
            if (accessRightToLeft)
                std::reverse(accessChain.indexChain.begin(), accessChain.indexChain.end());
            accessChain.gep = createGEP(accessChain.base, accessChain.indexChain);

            if (accessChain.trackActive)
                setActiveOutput(accessChain.base, accessChain.indexChain);
        }

        return accessChain.gep;
    } else {
        if (accessChain.trackActive)
            setActiveOutput(accessChain.base, accessChain.indexChain);

        return accessChain.base;
    }
}

// Comments in header
llvm::Value* Builder::collapseInputAccessChain()
{
    if (accessChain.indexChain.size() == 1) {
        // no need to reverse a single index
        return accessChain.indexChain.front();

    } else if (accessChain.indexChain.size() > 1) {
        if (accessRightToLeft)
            std::reverse(accessChain.indexChain.begin(), accessChain.indexChain.end());
        UnsupportedFunctionality("More than one dimension on input");
    }

    // if no indexChain, we have nothing to add to input slot
    return MakeIntConstant(context, 0);
}

// Comments in header
void Builder::accessChainStore(llvm::Value* value)
{
    assert(accessChain.isRValue == false);

    llvm::Value* base = collapseAccessChain();
    llvm::Value* source = value;

    // if swizzle exists, it is out-of-order or not full, we must load the target vector,
    // extract and insert elements to perform writeMask and/or swizzle
    if (accessChain.swizzle.size()) {

        llvm::Value* shadowVector = createLoad(base);

        // walk through the swizzle
        for (unsigned int i = 0; i < accessChain.swizzle.size(); ++i) {

            // extract scalar if needed
            llvm::Value* component = value;
            if (IsVector(component))
                component = builder.CreateExtractElement(value, MakeIntConstant(context, i));

            assert(IsScalar(component));

            // insert to our target at swizzled index
            shadowVector = builder.CreateInsertElement(shadowVector, component, MakeIntConstant(context, accessChain.swizzle[i]));
        }

        source = shadowVector;
    } else if (accessChain.component) {
        llvm::Value* shadowVector = createLoad(base);
        source = builder.CreateInsertElement(shadowVector, value, accessChain.component);
    }

    createStore(source, base);
}

// Comments in header
llvm::Value* Builder::accessChainLoad(EMdPrecision precision)
{
    llvm::Value* value;

    if (accessChain.isRValue) {
        if (accessChain.indexChain.size() > 1) {

            // create space for our r-value on the stack
            llvm::Value* lVal = createVariable(ESQLocal, 0, accessChain.base->getType(), 0, 0, "indexable");

            // store into it
            createStore(accessChain.base, lVal);

            // move base to the new alloca
            accessChain.base = lVal;
            accessChain.isRValue = false;

            // GEP from local alloca
            value = createLoad(collapseAccessChain());
        } else {
            value = accessChain.base;
        }
    } else {
        value = createLoad(collapseAccessChain(), accessChain.metadataKind, accessChain.mdNode);
    }

    if (accessChain.component)
        value = builder.CreateExtractElement(value, accessChain.component);
    else if (accessChain.swizzle.size())
        value = createSwizzle(precision, value, accessChain.swizzle, accessChain.swizzleResultType);

    return value;
}

// Comments in header
void Builder::leaveFunction(bool main)
{
    llvm::BasicBlock* BB = builder.GetInsertBlock();
    llvm::Function* F = builder.GetInsertBlock()->getParent();
    assert(BB && F);

    // If our function did not contain a return,
    // return void now
    if (0 == BB->getTerminator()) {

        // Whether we're in an unreachable (non-entry) block
        bool unreachable = &*F->begin() != BB && pred_begin(BB) == pred_end(BB);

        if (main && !unreachable) {
            // If we're leaving main and it is not terminated,
            // generate our pipeline writes
            makeMainReturn(true);
        } else if (unreachable)
            // If we're not the entry block, and we have no predecessors, we're
            // unreachable, so don't bother adding a return instruction in
            // (e.g. we're in a post-return block). Otherwise add a return.
            builder.CreateUnreachable();
        else {
            // Another flavor of unreachable, or an exit from a void function.
            // Return, but with a value of the function return type.  When this
            // is non-void, we should be in unreachable code.
            if (F->getReturnType()->isVoidTy())
                makeReturn(true);
            else {
                llvm::Value* retStorage = createVariable(ESQLocal, 0, F->getReturnType(), 0, 0, "dummyReturn");
                llvm::Value* retValue = createLoad(retStorage);
                makeReturn(true, retValue);
            }
        }
    }

    if (main)
        closeMain();
}

// Comments in header
llvm::BasicBlock* Builder::makeMain()
{
    assert(! mainFunction);

    llvm::BasicBlock* entry;
    llvm::SmallVector<llvm::Type*, 1> params;

    stageEpilogue = llvm::BasicBlock::Create(context, "stage-epilogue");
    stageExit     = llvm::BasicBlock::Create(context, "stage-exit");

    mainFunction = makeFunctionEntry(gla::GetVoidType(context), "main", params, &entry, true /* needs external visibility */);

    return entry;
}

// Comments in header
void Builder::closeMain()
{
    // Add our instructions to stageEpilogue, and stageExit
    builder.SetInsertPoint(stageEpilogue);
    if (! explicitPipelineCopyout)
        copyOutPipeline();
    builder.CreateBr(stageExit);

    builder.SetInsertPoint(stageExit);
    builder.CreateRet(0);

    mainFunction->getBasicBlockList().push_back(stageEpilogue);
    mainFunction->getBasicBlockList().push_back(stageExit);
}

// Comments in header
void Builder::makeReturn(bool implicit, llvm::Value* retVal, bool isMain)
{
    if (builder.GetInsertBlock()->getTerminator())
        return;

    if (isMain && retVal)
        gla::UnsupportedFunctionality("return value from main()");

    if (isMain)
        builder.CreateBr(stageEpilogue);
    else if (retVal)
        builder.CreateRet(retVal);
    else
        builder.CreateRetVoid();

    if (! implicit && insertNoPredecessorBlocks)
        createAndSetNoPredecessorBlock("post-return");
}

// Comments in header
void Builder::makeDiscard(bool isMain)
{
    if (builder.GetInsertBlock()->getTerminator())
        return;

    createIntrinsicCall(EMpNone, llvm::Intrinsic::gla_discard);

    if (isMain) {
        builder.CreateBr(stageExit);
        if (insertNoPredecessorBlocks)
            createAndSetNoPredecessorBlock("post-discard");
    } else {
        // A discard in a function cannot branch to the exit block of main.
        // TODO: generated code quality: Would it help DCE of code following a discard in a function to label the discard intrinsic with IntrNoReturn?
    }
}

// Utility method for creating a new block and setting the insert point to
// be in it. This is useful for flow-control operations that need a "dummy"
// block proceeding them (e.g. instructions after a discard, etc).
void Builder::createAndSetNoPredecessorBlock(llvm::StringRef name)
{
    llvm::BasicBlock* block = llvm::BasicBlock::Create(context, name, builder.GetInsertBlock()->getParent());
    builder.SetInsertPoint(block);
    builder.CreateUnreachable();
    builder.SetInsertPoint(&block->back());
}

// Comments in header
llvm::Function* Builder::makeFunctionEntry(llvm::Type* type, const char* name, llvm::ArrayRef<llvm::Type*> paramTypes, llvm::BasicBlock** entry, bool external)
{
    llvm::FunctionType *functionType = llvm::FunctionType::get(type, paramTypes, false);
    llvm::Function *function = llvm::Function::Create(functionType, external ? llvm::Function::ExternalLinkage : llvm::Function::InternalLinkage,
                                                      name ? name : "", module);

    // For shaders, we want everything passed in registers
    function->setCallingConv(llvm::CallingConv::Fast);

    if (entry)
        *entry = llvm::BasicBlock::Create(context, "entry", function);

    return function;
}

// Comments in header
llvm::Constant* Builder::getConstant(llvm::ArrayRef<llvm::Constant*> constants, llvm::Type* type)
{
    assert(type);

    switch (type->getTypeID()) {
    case llvm::Type::IntegerTyID:
    case llvm::Type::FloatTyID:
        return constants[0];
    case llvm::Type::VectorTyID:
        return llvm::ConstantVector::get(constants);
    case llvm::Type::ArrayTyID:
        return llvm::ConstantArray::get(llvm::dyn_cast<llvm::ArrayType>(type), constants);
    case llvm::Type::StructTyID:
        return llvm::ConstantStruct::get(llvm::dyn_cast<llvm::StructType>(type), constants);
    default:
        assert(0 && "Constant type in TopBuilder");
        break;
    }

    return 0;
}

// Comments in header
llvm::Value* Builder::createVariable(EStorageQualifier storageQualifier, int storageInstance,
                                     llvm::Type* type, llvm::Constant* initializer, const std::string* annotation,
                                     llvm::StringRef name)
{
    std::string annotatedName;
    if (annotation != 0) {
        annotatedName = *annotation;
        annotatedName.append(" ");
        annotatedName.append(name);
    } else
        annotatedName = name;

    // Set some common default values, which the switch will override
    // Internal linkage helps with global optimizations,
    // so does having an initializer.
    unsigned int addressSpace = mapAddressSpace(storageQualifier, storageInstance);
    llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalVariable::InternalLinkage;
    bool global = false;
    bool readOnly = false;

    switch (storageQualifier) {
    case ESQResource:
        linkage = llvm::GlobalVariable::ExternalLinkage;
        global = true;
        readOnly = true;
        break;

    case ESQUniform:
        linkage = llvm::GlobalVariable::ExternalLinkage;
        global = true;
        readOnly = true;
        break;

    case ESQInput:
        // This isn't for the actual pipeline input, but for the variable
        // the pipeline input is read into.

        // fall through, manipulate same as output

    case ESQOutput:
        // This isn't for the actual pipeline output, but for the variable
        // holding the value up until when the epilogue writes out to the pipe.

        if (useLogicalIo())
            linkage = llvm::GlobalVariable::ExternalLinkage;
        else {
            // both input and output will be shadowed
            if (annotatedName.substr(0, 3) == "gl_")
                annotatedName.erase(0, 3);
            annotatedName.append("_shadow");
        }

        // fall through, the shadows will be globals

    case ESQGlobal:
        global = true;
        if (initializer == 0)
            initializer = llvm::Constant::getNullValue(type);
        break;

    case ESQConst:
        global = true;
        readOnly = true;
        assert(initializer);
        break;

    case ESQLocal:
        break;

    default:
        assert(! "unhandled storage qualifier");
        break;
    }

    llvm::Value* value;
    if (global) {
        value = new llvm::GlobalVariable(*module, type, readOnly, linkage, initializer, annotatedName, 0, llvm::GlobalVariable::NotThreadLocal, addressSpace);

        if (storageQualifier == ESQOutput && ! useLogicalIo()) {
            // Track the value that must be copied out to the pipeline at
            // the end of the shader.
            copyOut co = { value };    // the missing fields need to be filled in by calling setOutputMetadata()
            copyOuts.push_back(co);
        }

    } else {
        // LLVM's promote memory to registers only works when
        // alloca is in the entry block.
        llvm::BasicBlock* entryBlock = &builder.GetInsertBlock()->getParent()->getEntryBlock();
        llvm::IRBuilder<> entryBuilder(entryBlock, entryBlock->begin());
        value = createEntryAlloca(type, annotatedName);
    }

    return value;
}

llvm::Type* Builder::getPointerType(llvm::Type* elementType, EStorageQualifier storageQualifier, int instance)
{
    return llvm::PointerType::get(elementType, mapAddressSpace(storageQualifier, instance));
}

int Builder::mapAddressSpace(EStorageQualifier qualifier, int instance) const
{
    switch (qualifier) {
    case ESQResource:
        return gla::ResourceAddressSpace;
    case ESQUniform:
        return gla::ConstantAddressSpaceBase + instance;
    case ESQInput:
    case ESQOutput:
    case ESQGlobal:
    case ESQConst:
    case ESQLocal:  // should be a don't care (valid to pass in, but result unused)
        return gla::GlobalAddressSpace;
    default:
        assert(! "unhandled storage qualifier");
        return gla::GlobalAddressSpace;
    }
}

// Comments in header
llvm::Value* Builder::createEntryAlloca(llvm::Type* type, llvm::StringRef name)
{
    // LLVM's promote memory to registers only works when alloca is in the entry block.
    llvm::BasicBlock* entryBlock = &builder.GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(entryBlock, entryBlock->begin());

    return entryBuilder.CreateAlloca(type, 0, name);
}

// Comments in header
llvm::Value* Builder::createStore(llvm::Value* rValue, llvm::Value* lValue)
{
    // Retroactively change the name of the last-value temp to the name of the
    // l-value, to help debuggability, if it's just an llvm temp name.
    if (rValue->getName().size() < 2 || (rValue->getName()[1] >= '0' && rValue->getName()[1] <= '9'))
        rValue->setName(lValue->getName());

    builder.CreateStore(rValue, lValue);

    return lValue;
}

// Comments in header
llvm::Value* Builder::createLoad(llvm::Value* lValue, const char* metadataKind, llvm::MDNode* mdNode)
{
    if (llvm::isa<llvm::PointerType>(lValue->getType())) {
        llvm::Instruction* load = builder.CreateLoad(lValue);
        if (metadataKind)
            load->setMetadata(metadataKind, mdNode);

        return load;
    } else
        return lValue;
}

// Comments in header
llvm::Value* Builder::createGEP(llvm::Value* gepValue, llvm::ArrayRef<llvm::Value*> gepIndexChain)
{
    return builder.CreateGEP(gepValue, gepIndexChain);
}

// Comments in header
llvm::Value* Builder::createInsertValue(llvm::Value* target, llvm::Value* source, unsigned* indices, int indexCount)
{
    return builder.CreateInsertValue(target, source,  llvm::ArrayRef<unsigned>(indices, indices + indexCount));
}

// To be used when dereferencing an access chain that is for an
// output variable.  The exposed method for making this happen is
// accessChainTrackActive().
void Builder::setActiveOutput(llvm::Value* base, std::vector<llvm::Value*>& gepChain)
{
    if (useLogicalIo())
        return;

    // find the output
    for (unsigned int out = 0; out < copyOuts.size(); ++out) {
        if (copyOuts[out].value == base) {
            llvm::Type* type = copyOuts[out].value->getType();
            if (! llvm::isa<llvm::PointerType>(type))
                UnsupportedFunctionality("non-pointer in setActiveOutput()");

            type = type->getContainedType(0);
            int activeIndex = 0;
            setActiveOutputSubset(copyOuts[out], type, activeIndex, gepChain, 1, true);

            break;
        }
    }
}

// Recursively walk the particular copyOut object to set activeIndex on the subset satisfying the gepChain,
// where the gepChain could select a whole aggregate, or a specific leaf, or some combination based on 
// which entries are constant and which are variable.
void Builder::setActiveOutputSubset(copyOut& out, llvm::Type* type, int& activeIndex, const std::vector<llvm::Value*>& gepChain, int gepIndex, bool active)
{
    // Look at the next gepChain entry to see if it is present and specific about 
    // the next level down.
    int constantIndex = -1;   // -1 means entire subtree is needed; either because refining indexes are missing or variable
    if (gepIndex < (int)gepChain.size()) {
        const llvm::ConstantInt *index = llvm::dyn_cast<llvm::ConstantInt>(gepChain[gepIndex]);
        if (index)
            constantIndex = (int)index->getValue().getSExtValue();
    }

    if (const llvm::ArrayType* arrayType = llvm::dyn_cast<llvm::ArrayType>(type)) {
        // Handle array and matrix (one slot per column)

        for (int index = 0; index < arrayType->getNumElements(); ++index) {
            // Go from active to not active if there is still a gepChain entry covering
            // the new depth, and it is a constant that does not match the current
            // offset.
            bool nextActive = active;
            if (constantIndex >= 0 && index != constantIndex)
                nextActive = false;

            setActiveOutputSubset(out, type->getContainedType(0), activeIndex, gepChain, gepIndex + 1, nextActive);
        }
    } else if (const llvm::StructType* structType = llvm::dyn_cast<llvm::StructType>(type)) {
        // Handle structure

        for (int index = 0; index < (int)structType->getNumElements(); ++index) {
            // Can only have a constant constantIndex here, unless the entire
            // subobject was selected.
            bool nextActive = active;
            if (constantIndex >= 0 && index != constantIndex)
                nextActive = false;

            setActiveOutputSubset(out, type->getContainedType(index), activeIndex, gepChain, gepIndex + 1, nextActive);
        }
    } else {
        // Handle scalar/vector; whatever is single-slot sized

        if (active)
            out.active[activeIndex] = true;
        ++activeIndex;
    }
}

// Comments in header
void Builder::setOutputMetadata(llvm::Value* value, llvm::MDNode* mdNode, int baseSlot, int numSlots)
{
    if (useLogicalIo())
        return;

    // it's most likely the last one pushed...
    for (unsigned int out = copyOuts.size() - 1; out >= 0; ++out) {
        if (copyOuts[out].value == value) {
            // this should only be done once
            assert(copyOuts[out].numSlots == 0);
            if (copyOuts[out].numSlots)
                return;

            copyOuts[out].mdNode = mdNode;
            copyOuts[out].baseSlot = baseSlot;
            copyOuts[out].numSlots = numSlots;
            for (int a = 0; a < numSlots; ++a)
                copyOuts[out].active.push_back(false);

            break;
        }
    }
}

// Comments in header
void Builder::copyOutPipeline()
{
    if (useLogicalIo())
        return;

    std::vector<llvm::Value*> gepChain;
    for (unsigned int out = 0; out < copyOuts.size(); ++out) {
        llvm::Type* type = copyOuts[out].value->getType();
        if (llvm::isa<llvm::PointerType>(type)) {
            type = type->getContainedType(0);
            gepChain.push_back(MakeIntConstant(context, 0));
        }

        int slot = copyOuts[out].baseSlot; // because the recursion modifies the slot argument
        int activeIndex = 0;
        copyOutOnePipeline(copyOuts[out], type, copyOuts[out].mdNode, slot, activeIndex, gepChain);

        // pop the pointer push if there was one
        if (gepChain.size())
            gepChain.pop_back();
    }
}

// Recursively walk the particular copyOut to do all its slot write-outs to the pipeline.
void Builder::copyOutOnePipeline(const copyOut& out, llvm::Type* type, llvm::MDNode* mdNode, int& slot, int& activeIndex, std::vector<llvm::Value*>& gepChain)
{
    if (const llvm::ArrayType* arrayType = llvm::dyn_cast<llvm::ArrayType>(type)) {
        // Handle array and matrix (one slot per column)

        for (int index = 0; index < arrayType->getNumElements(); ++index) {
            gepChain.push_back(MakeIntConstant(context, index));
            copyOutOnePipeline(out, type->getContainedType(0), mdNode, slot, activeIndex, gepChain);
            gepChain.pop_back();
        }
    } else if (const llvm::StructType* structType = llvm::dyn_cast<llvm::StructType>(type)) {
        // Handle structure

        for (int index = 0; index < (int)structType->getNumElements(); ++index) {
            gepChain.push_back(MakeIntConstant(context, index));
            copyOutOnePipeline(out, type->getContainedType(index), mdNode, slot, activeIndex, gepChain);
            gepChain.pop_back();
        }
    } else {
        // Handle scalar/vector

        if (out.active[activeIndex]) {
            llvm::Value* loadVal;
            if (gepChain.size() > 1)
                loadVal = builder.CreateLoad(createGEP(out.value, gepChain));
            else
                loadVal = builder.CreateLoad(out.value);
            writePipeline(loadVal, MakeUnsignedConstant(context, slot), -1, mdNode);
        }
        ++slot;
        ++activeIndex;
    }
}

// Comments in header
void Builder::writePipeline(llvm::Value* outValue, llvm::Value* slot, int mask, llvm::MDNode* mdNode, EInterpolationMethod method, EInterpolationLocation location)
{
    llvm::Constant *maskConstant = MakeIntConstant(context, mask);

    if (! llvm::isa<llvm::IntegerType>(slot->getType()))
        gla::UnsupportedFunctionality("Pipeline write using non-integer index");

    // This correction is necessary for some front ends, which might allow
    // "interpolated" integers or Booleans.
    if (! GetBasicType(outValue)->isFloatTy())
        method = EIMNone;

    EInterpolationMode mode = gla::MakeInterpolationMode(method, location);

    llvm::Instruction* write;
    llvm::Function *intrinsic;
    if (method == EIMNone) {
        llvm::Intrinsic::ID intrinsicID;
        switch (GetBasicTypeID(outValue)) {
        case llvm::Type::IntegerTyID:   intrinsicID = llvm::Intrinsic::gla_writeData;   break;
        case llvm::Type::FloatTyID:     intrinsicID = llvm::Intrinsic::gla_fWriteData;  break;
        default:                        assert(! "Unsupported type in writePipeline");  break;
        }

        intrinsic = getIntrinsic(intrinsicID, outValue->getType());
        write = builder.CreateCall3(intrinsic, slot, maskConstant, outValue);
    } else {
        llvm::Constant *modeConstant = MakeUnsignedConstant(context, mode);
        intrinsic = getIntrinsic(llvm::Intrinsic::gla_fWriteInterpolant, outValue->getType());
        write = builder.CreateCall4(intrinsic, slot, maskConstant, modeConstant, outValue);
    }

    write->setMetadata(OutputMdName, mdNode);
}

// Comments in header
llvm::Value* Builder::readPipeline(gla::EMdPrecision precision, 
                                   llvm::Type* type, llvm::StringRef name, int slot, 
                                   llvm::MDNode* inputMd,
                                   int mask,
                                   EInterpolationMethod method, EInterpolationLocation location,
                                   llvm::Value* offset, llvm::Value* sampleIdx)
{
    llvm::Constant *slotConstant = MakeUnsignedConstant(context, slot);
    llvm::Constant *maskConstant = MakeIntConstant(context, mask);

    // This correction is necessary for some front ends, which might allow
    // "interpolated" integers or Booleans.
    if (! GetBasicType(type)->isFloatTy())
        method = EIMNone;

    EInterpolationMode mode = gla::MakeInterpolationMode(method, location);

    llvm::Function *intrinsic;
    llvm::Instruction* readInstr;
    if (method != EIMNone) {
        llvm::Constant *modeConstant = MakeUnsignedConstant(context, mode);

        if (sampleIdx) {
            assert(0 == offset);
            intrinsic = getIntrinsic(llvm::Intrinsic::gla_fReadInterpolantSample, type);
            readInstr = builder.CreateCall4(intrinsic, slotConstant, maskConstant, modeConstant, sampleIdx, name);
        } else if (offset) {
            assert(0 == sampleIdx);
            intrinsic = getIntrinsic(llvm::Intrinsic::gla_fReadInterpolantOffset, type, offset->getType());
            readInstr = builder.CreateCall4(intrinsic, slotConstant, maskConstant, modeConstant, offset, name);
        } else {
            intrinsic = getIntrinsic(llvm::Intrinsic::gla_fReadInterpolant, type);
            readInstr = builder.CreateCall3(intrinsic, slotConstant, maskConstant, modeConstant, name);
        }
    } else {
        switch (GetBasicTypeID(type)) {
        case llvm::Type::IntegerTyID:   intrinsic = getIntrinsic(llvm::Intrinsic::gla_readData,  type); break;
        case llvm::Type::FloatTyID:     intrinsic = getIntrinsic(llvm::Intrinsic::gla_fReadData, type); break;
        default: break;
        }

        readInstr = builder.CreateCall2(intrinsic, slotConstant, maskConstant, name);
    }

    readInstr->setMetadata(InputMdName, inputMd);
    setInstructionPrecision(readInstr, precision);

    return readInstr;
}

llvm::Value* Builder::createSwizzle(gla::EMdPrecision precision, llvm::Value* source, int swizzleMask, llvm::Type* finalType)
{
    const int numComponents = gla::GetComponentCount(finalType);

    // If we are dealing with a scalar, just put it in a register and return
    if (numComponents == 1) {
        llvm::Value* result = builder.CreateExtractElement(source, gla::MakeIntConstant(context, gla::GetSwizzle(swizzleMask, 0)));
        setInstructionPrecision(result, precision);

        return result;
    }

    // Else we are dealing with a vector

    // We start out with an undef to insert into
    llvm::Value* target = llvm::UndefValue::get(finalType);

    for (int i = 0; i < numComponents; ++i) {

        // If we're constructing a vector from a scalar, then just
        // make inserts. Otherwise make insert/extract pairs
        if (IsScalar(source)) {
            target = builder.CreateInsertElement(target, source, gla::MakeIntConstant(context, i));
            setInstructionPrecision(target, precision);
        } else {
            // Extract an element to a scalar, then immediately insert to our target
            llvm::Value* extractInst = builder.CreateExtractElement(source, gla::MakeIntConstant(context, gla::GetSwizzle(swizzleMask, i)));
            setInstructionPrecision(extractInst, precision);
            target = builder.CreateInsertElement(target, extractInst, gla::MakeIntConstant(context, i));
            setInstructionPrecision(target, precision);
        }
    }

    return target;
}

llvm::Value* Builder::createSwizzle(gla::EMdPrecision precision, llvm::Value* source, llvm::ArrayRef<int> channels, llvm::Type* finalType)
{
    int swizMask = 0;
    for (unsigned int i = 0; i < channels.size(); ++i) {
        swizMask |= channels[i] << i*2;
    }

    return createSwizzle(precision, source, swizMask, finalType);
}

//
// Builder::Matrix definitions
//

// Comments in header
llvm::Type* Builder::getMatrixType(llvm::Type* elementType, int numColumns, int numRows)
{
    assert(numColumns >= minMatrixSize && numRows >= minMatrixSize);
    assert(numColumns <= maxMatrixSize && numRows <= maxMatrixSize);

    llvm::Type** type = &matrixTypeCache[numColumns-minMatrixSize][numRows-minMatrixSize];
    if (*type == 0) {
        // a matrix is an array of vectors
        llvm::VectorType* columnType = llvm::VectorType::get(elementType, numRows);
        *type = llvm::ArrayType::get(columnType, numColumns);
    }

    return *type;
}

// Comments in header
llvm::Value* Builder::createMatrixOp(gla::EMdPrecision precision, llvm::Instruction::BinaryOps llvmOpcode, llvm::Value* left, llvm::Value* right)
{
    assert(IsAggregate(left) || IsAggregate(right));

    // component-wise matrix operations on same-shape matrices
    if (IsAggregate(left) && IsAggregate(right)) {
        assert(GetNumColumns(left) == GetNumColumns(right));
        assert(GetNumRows(left) == GetNumRows(right));

        return createComponentWiseMatrixOp(precision, llvmOpcode, left, right);
    }

    // matrix <op> smeared scalar
    if (IsAggregate(left)) {
        assert(IsScalar(right));

        return createSmearedMatrixOp(precision, llvmOpcode, left, right, false);
    }

    // smeared scalar <op> matrix
    if (IsAggregate(right)) {
        assert(IsScalar(left));

        return createSmearedMatrixOp(precision, llvmOpcode, right, left, true);
    }

    assert(! "nonsensical matrix operation");

    return 0;
}

// Comments in header
llvm::Value* Builder::createMatrixMultiply(gla::EMdPrecision precision, llvm::Value* left, llvm::Value* right)
{
    // Note: IsAggregate() is assumed to be true iff the value is a matrix.
    // This is safe because the front end can only call this for operands
    // that are part of a multrix multiply operation.
    // (More generally, all matrices are aggregates, but not vice versa.)

    // outer product
    if (IsVector(left) && IsVector(right))
        return createOuterProduct(precision, left, right);

    assert(IsAggregate(left) || IsAggregate(right));

    // matrix times matrix
    if (IsAggregate(left) && IsAggregate(right)) {
        assert(GetNumRows(left)    == GetNumColumns(right));
        assert(GetNumColumns(left) == GetNumRows(right));

        return createMatrixTimesMatrix(precision, left, right);
    }

    // matrix times vector
    if (IsAggregate(left) && IsVector(right)) {
        assert(GetNumColumns(left) == GetComponentCount(right));

        return createMatrixTimesVector(precision, left, right);
    }

    // vector times matrix
    if (IsVector(left) && IsAggregate(right)) {
        assert(GetNumRows(right) == GetComponentCount(left));

        return createVectorTimesMatrix(precision, left, right);
    }

    // matrix times scalar
    if (IsAggregate(left) && IsScalar(right))
        return createSmearedMatrixOp(precision, llvm::Instruction::FMul, left, right, true);

    // scalar times matrix
    if (IsScalar(left) && IsAggregate(right))
        return createSmearedMatrixOp(precision, llvm::Instruction::FMul, right, left, false);

    assert(! "nonsensical matrix multiply");

    return 0;
}

// Comments in header
llvm::Value* Builder::createMatrixCompare(EMdPrecision precision, llvm::Value* left, llvm::Value* right, bool allEqual)
{
    assert(IsAggregate(left) && IsAggregate(right));
    assert(GetNumColumns(left) == GetNumColumns(right));
    assert(GetNumRows(left) == GetNumRows(right));

    return createCompare(precision, left, right, allEqual);
}

// Comments in header
llvm::Value* Builder::createMatrixTranspose(EMdPrecision precision, llvm::Value* matrix)
{
    // Will use a two step process
    // 1. make a compile-time C++ 2D array of element values
    // 2. copy it, transposed

    // Step 1, copy out
    llvm::Value* elements[maxMatrixSize][maxMatrixSize];
    for (int col = 0; col < GetNumColumns(matrix); ++col) {
        llvm::Value* column = builder.CreateExtractValue(matrix, col, "column");
        for (int row = 0; row < GetNumRows(matrix); ++row) {
            elements[col][row] = builder.CreateExtractElement(column, MakeUnsignedConstant(context, row), "element");
            setInstructionPrecision(elements[col][row], precision);
        }
    }

    // make a new variable to hold the result
    llvm::Type* resultType = getMatrixType(GetMatrixElementType(matrix->getType()), GetNumRows(matrix), GetNumColumns(matrix));
    llvm::Value* result = createEntryAlloca(resultType);
    result = builder.CreateLoad(result);

    // Step 2, copy in while transposing
    for (int col = 0; col < GetNumColumns(result); ++col) {
        llvm::Value* column = builder.CreateExtractValue(result, col, "column");
        setInstructionPrecision(column, precision);
        for (int row = 0; row < GetNumRows(result); ++row) {
            column = builder.CreateInsertElement(column, elements[row][col], MakeIntConstant(context, row), "column");
            setInstructionPrecision(column, precision);
        }
        result = builder.CreateInsertValue(result, column, col, "matrix");
        setInstructionPrecision(result, precision);
    }

    return result;
}

// Comments in header
llvm::Value* Builder::createMatrixInverse(gla::EMdPrecision precision, llvm::Value* matrix)
{
    assert(GetNumColumns(matrix) == GetNumRows(matrix));
    int size = GetNumColumns(matrix);

    // Copy the elements out, switching notation to [row][col], to match normal mathematic treatment
    llvm::Value* elements[maxMatrixSize][maxMatrixSize];
    for (int col = 0; col < size; ++col) {
        llvm::Value* column = builder.CreateExtractValue(matrix, col, "column");
        setInstructionPrecision(column, precision);
        for (int row = 0; row < size; ++row) {
            elements[row][col] = builder.CreateExtractElement(column, MakeUnsignedConstant(context, row), "element");
            setInstructionPrecision(elements[row][col], precision);
        }
    }

    // Create the adjugate (the transpose of the cofactors)
    llvm::Value* adjugate[maxMatrixSize][maxMatrixSize];
    for (int row = 0; row < size; ++row) {
       for (int col = 0; col < size; ++col) {

           // compute the cofactor
           llvm::Value* minor[maxMatrixSize][maxMatrixSize];
           makeMatrixMinor(elements, minor, row, col, size);
           llvm::Value* cofactor = createMatrixDeterminant(precision, minor, size-1);
           if ((row + col) & 0x1) {
               cofactor = builder.CreateFNeg(cofactor);
               setInstructionPrecision(cofactor, precision);
           }

           // put into transposed location
           adjugate[col][row] = cofactor;
       }
    }

    // get the determinant:  this will replicate some of the above, but relying
    // on LLVM optimization to notice that and fix it
    llvm::Value* det = createMatrixDeterminant(precision, elements, size);

    // Divide the adjugate by the determinant
    llvm::Value* detInverse = createRecip(precision, det);
    for (int row = 0; row < size; ++row) {
       for (int col = 0; col < size; ++col) {
            adjugate[row][col] = builder.CreateFMul(adjugate[row][col], detInverse);
            setInstructionPrecision(adjugate[row][col], precision);
       }
    }

    // build up a result matrix
    llvm::Value* result = createEntryAlloca(matrix->getType());
    result = builder.CreateLoad(result);

    for (int col = 0; col < size; ++col) {
        llvm::Value* column = builder.CreateExtractValue(result, col, "column");
        setInstructionPrecision(column, precision);
        for (int row = 0; row < size; ++row) {
            column = builder.CreateInsertElement(column, adjugate[row][col], MakeIntConstant(context, row), "column");
            setInstructionPrecision(column, precision);
        }
        result = builder.CreateInsertValue(result, column, col, "matrix");
        setInstructionPrecision(result, precision);
    }

    return result;
}

// Comments in header
llvm::Value* Builder::createMatrixDeterminant(gla::EMdPrecision precision, llvm::Value* matrix)
{
    assert(GetNumColumns(matrix) == GetNumRows(matrix));
    int size = GetNumColumns(matrix);

    llvm::Value* elements[maxMatrixSize][maxMatrixSize];
    for (int col = 0; col < size; ++col) {
        llvm::Value* column = builder.CreateExtractValue(matrix, col, "column");
        setInstructionPrecision(column, precision);
        for (int row = 0; row < size; ++row) {
            elements[row][col] = builder.CreateExtractElement(column, MakeUnsignedConstant(context, row), "element");
            setInstructionPrecision(elements[row][col], precision);
        }
    }

    // Compute the determinant from the copied out values
    return createMatrixDeterminant(precision, elements, size);
}

// Comments in header
llvm::Value* Builder::createMatrixDeterminant(gla::EMdPrecision precision, llvm::Value* (&matrix)[maxMatrixSize][maxMatrixSize], int size)
{
    if (size == 1)
        return matrix[0][0];
    if (size == 2) {
        llvm::Value* term1 = builder.CreateFMul(matrix[0][0], matrix[1][1]);
        setInstructionPrecision(term1, precision);
        llvm::Value* term2 = builder.CreateFMul(matrix[0][1], matrix[1][0]);
        setInstructionPrecision(term2, precision);
        llvm::Value* result = builder.CreateFSub(term1, term2);
        setInstructionPrecision(result, precision);

        return result;
    } else {
        llvm::Value* result;

        for (int cofactor = 0; cofactor < size; ++cofactor) {

            // make the minor matrix
            llvm::Value* minor[maxMatrixSize][maxMatrixSize]; // will hold only 2x2 and 3x3 matrices
            makeMatrixMinor(matrix, minor, 0, cofactor, size);

            // accumulate the term into the result (alternating +,-,+,...)
            llvm::Value* minorDet = createMatrixDeterminant(precision, minor, size - 1);
            llvm::Value* term = builder.CreateFMul(matrix[0][cofactor], minorDet);
            setInstructionPrecision(term, precision);
            if (cofactor == 0)
                result = term;
            else if (cofactor & 0x1)
                result = builder.CreateFSub(result, term);
            else
                result = builder.CreateFAdd(result, term);
            setInstructionPrecision(result, precision);
        }

        return result;
    }
}

// 'size' is the size of the input matrix, not the output matrix
void Builder::makeMatrixMinor(llvm::Value* (&matrix)[maxMatrixSize][maxMatrixSize], llvm::Value* (&minor)[maxMatrixSize][maxMatrixSize], int mRow, int mCol, int size)
{
    int resRow = 0;
    for (int row = 0; row < size; ++row) {
        if (row == mRow)
            continue;
        
        int resCol = 0;
        for (int col = 0; col < size; ++col) {
            if (col == mCol)
                continue;
            minor[resRow][resCol] = matrix[row][col];
            ++resCol;
        }

        ++resRow;
    }
}

llvm::Value* Builder::createMatrixTimesVector(gla::EMdPrecision precision, llvm::Value* matrix, llvm::Value* rvector)
{
    assert(GetNumColumns(matrix) == GetComponentCount(rvector));

    llvm::Type* resultType = llvm::VectorType::get(rvector->getType()->getContainedType(0), GetNumRows(matrix));

    if (useColumnBasedMatrixIntrinsics()) {
        // Keep the matrix operation as an intrinsic.

        llvm::Type* columnType = matrix->getType()->getContainedType(0);

        llvm::Value* columns[maxMatrixSize];
        for (int col = 0; col < GetNumColumns(matrix); ++col)
            columns[col] = builder.CreateExtractValue(matrix, col, "column");

        // TODO: ES functionality: matrix precision            setInstructionPrecision(result, precision);
        llvm::Function* mul;
        switch (GetNumColumns(matrix)) {
        case 2: 
            mul = getIntrinsic(llvm::Intrinsic::gla_fMatrix2TimesVector, resultType, columnType, columnType, rvector->getType());
            return builder.CreateCall3(mul, columns[0], columns[1], rvector, "mat2vec");
        case 3: 
            mul = getIntrinsic(llvm::Intrinsic::gla_fMatrix3TimesVector, resultType, columnType, columnType, columnType, rvector->getType());
            return builder.CreateCall4(mul, columns[0], columns[1], columns[2], rvector, "mat3vec");
        case 4:
            mul = getIntrinsic(llvm::Intrinsic::gla_fMatrix4TimesVector, resultType, columnType, columnType, columnType, columnType, rvector->getType());
            return builder.CreateCall5(mul, columns[0], columns[1], columns[2], columns[3], rvector, "mat4vec");
        default: 
            UnsupportedFunctionality("matrix size");
            break;
        }
    }

    // Decompose the matrix operation into native LLVM operations.

    // Allocate a vector to build the result in
    llvm::Value* result = createEntryAlloca(resultType);
    result = builder.CreateLoad(result);

    // Cache the components of the vector; they'll be revisited multiple times
    llvm::Value* components[maxMatrixSize];
    for (int comp = 0; comp < GetComponentCount(rvector); ++comp) {
        components[comp] = builder.CreateExtractElement(rvector,  MakeUnsignedConstant(context, comp), "component");
        setInstructionPrecision(components[comp], precision);
    }

    // Go row by row, manually forming the cross-column "dot product"
    for (int row = 0; row < GetNumRows(matrix); ++row) {
        llvm::Value* dotProduct;
        for (int col = 0; col < GetNumColumns(matrix); ++col) {
            llvm::Value* column = builder.CreateExtractValue(matrix, col, "column");
            setInstructionPrecision(column, precision);
            llvm::Value* element = builder.CreateExtractElement(column, MakeUnsignedConstant(context, row), "element");
            setInstructionPrecision(element, precision);
            llvm::Value* product = builder.CreateFMul(element, components[col], "product");
            setInstructionPrecision(product, precision);
            if (col == 0)
                dotProduct = product;
            else {
                dotProduct = builder.CreateFAdd(dotProduct, product, "dotProduct");
                setInstructionPrecision(dotProduct, precision);
            }
        }
        result = builder.CreateInsertElement(result, dotProduct, MakeUnsignedConstant(context, row));
        setInstructionPrecision(result, precision);
    }

    return result;
}

llvm::Value* Builder::createVectorTimesMatrix(gla::EMdPrecision precision, llvm::Value* lvector, llvm::Value* matrix)
{
    if (useColumnBasedMatrixIntrinsics()) {
        // Keep the matrix operation as an intrinsic.

        llvm::Type* resultType = llvm::VectorType::get(lvector->getType()->getContainedType(0), GetNumColumns(matrix));
        llvm::Type* columnType = matrix->getType()->getContainedType(0);

        llvm::Value* columns[maxMatrixSize];
        for (int col = 0; col < GetNumColumns(matrix); ++col)
            columns[col] = builder.CreateExtractValue(matrix, col, "column");

        // TODO: ES functionality: matrix precision            setInstructionPrecision(result, precision);
        llvm::Function* mul;
        switch (GetNumColumns(matrix)) {
        case 2: 
            mul = getIntrinsic(llvm::Intrinsic::gla_fVectorTimesMatrix2, resultType, lvector->getType(), columnType, columnType);
            return builder.CreateCall3(mul, lvector, columns[0], columns[1], "vec2mat");
        case 3: 
            mul = getIntrinsic(llvm::Intrinsic::gla_fVectorTimesMatrix3, resultType, lvector->getType(), columnType, columnType, columnType);
            return builder.CreateCall4(mul, lvector, columns[0], columns[1], columns[2], "vec3mat");
        case 4:
            mul = getIntrinsic(llvm::Intrinsic::gla_fVectorTimesMatrix4, resultType, lvector->getType(), columnType, columnType, columnType, columnType);
            return builder.CreateCall5(mul, lvector, columns[0], columns[1], columns[2], columns[3], "vec4mat");
        default: 
            UnsupportedFunctionality("matrix size");
            break;
        }
    }

    // Decompose the matrix operation into native LLVM operations.

    // Get the dot product intrinsic for these operands
    llvm::Intrinsic::ID dotIntrinsic;
    switch (GetNumRows(matrix)) {
    case 2:
        dotIntrinsic = llvm::Intrinsic::gla_fDot2;
        break;
    case 3:
        dotIntrinsic = llvm::Intrinsic::gla_fDot3;
        break;
    case 4:
        dotIntrinsic = llvm::Intrinsic::gla_fDot4;
        break;
    default:
        assert(! "bad matrix size in createVectorTimesMatrix");
        break;
    }

    llvm::Function *dot = Builder::getIntrinsic(dotIntrinsic, GetBasicType(lvector), lvector->getType(), lvector->getType());

    // Allocate a vector to build the result in
    llvm::Value* result = createEntryAlloca(GetVectorOrScalarType(lvector->getType(), GetNumColumns(matrix)));
    result = builder.CreateLoad(result);

    // Compute the dot products for the result
    for (int c = 0; c < GetNumColumns(matrix); ++c) {
        llvm::Value* column = builder.CreateExtractValue(matrix, c, "column");
        setInstructionPrecision(column, precision);
        llvm::Instruction* comp = builder.CreateCall2(dot, lvector, column, "dotres");
        setInstructionPrecision(comp, precision);
        result = builder.CreateInsertElement(result, comp, MakeUnsignedConstant(context, c));
        setInstructionPrecision(result, precision);
    }

    return result;
}

llvm::Value* Builder::createComponentWiseMatrixOp(gla::EMdPrecision precision, llvm::Instruction::BinaryOps op, llvm::Value* left, llvm::Value* right)
{
    // Allocate a matrix to hold the result in
    llvm::Value* result = createEntryAlloca(left->getType());
    result = builder.CreateLoad(result);

    // Compute the component-wise operation per column vector
    for (int c = 0; c < GetNumColumns(left); ++c) {
        llvm::Value*  leftColumn = builder.CreateExtractValue( left, c,  "leftColumn");
        setInstructionPrecision(leftColumn, precision);
        llvm::Value* rightColumn = builder.CreateExtractValue(right, c, "rightColumn");
        setInstructionPrecision(rightColumn, precision);
        llvm::Value* column = builder.CreateBinOp(op, leftColumn, rightColumn, "column");
        setInstructionPrecision(column, precision);
        result = builder.CreateInsertValue(result, column, c);
        setInstructionPrecision(result, precision);
    }

    return result;
}

llvm::Value* Builder::createSmearedMatrixOp(gla::EMdPrecision precision, llvm::Instruction::BinaryOps op, llvm::Value* matrix, llvm::Value* scalar, bool reverseOrder)
{
    // TODO: generated code performance: better to smear the scalar to a column-like vector, and apply that vector multiple times
    // Allocate a matrix to build the result in
    llvm::Value* result = createEntryAlloca(matrix->getType());
    result = builder.CreateLoad(result);

    // Compute per column vector
    for (int c = 0; c < GetNumColumns(matrix); ++c) {
        llvm::Value* column = builder.CreateExtractValue(matrix, c, "column");
        setInstructionPrecision(column, precision);

        for (int r = 0; r < GetNumRows(matrix); ++r) {
            llvm::Value* element = builder.CreateExtractElement(column, MakeUnsignedConstant(context, r), "row");
            setInstructionPrecision(element, precision);
            if (reverseOrder)
                element = builder.CreateBinOp(op, scalar, element);
            else
                element = builder.CreateBinOp(op, element, scalar);
            setInstructionPrecision(element, precision);
            column = builder.CreateInsertElement(column, element, MakeUnsignedConstant(context, r));
            setInstructionPrecision(column, precision);
        }

        result = builder.CreateInsertValue(result, column, c);
        setInstructionPrecision(result, precision);
    }

    return result;
}

llvm::Value* Builder::createMatrixTimesMatrix(gla::EMdPrecision precision, llvm::Value* left, llvm::Value* right)
{
    // Allocate a matrix to hold the result in
    int rows = GetNumRows(left);
    int columns =  GetNumColumns(right);
    llvm::Value* result = createEntryAlloca(getMatrixType(GetMatrixElementType(left->getType()), columns, rows));
    result = builder.CreateLoad(result, "resultMatrix");

    if (useColumnBasedMatrixIntrinsics()) {
        // Keep the matrix operation as an intrinsic.

        llvm::Type*  leftColumnType =  left->getType()->getContainedType(0);
        llvm::Type* rightColumnType = right->getType()->getContainedType(0);

        // Set up the intrinsic's types for overloading
        std::vector<llvm::Type*> intrinsicTypes;
        // return values
        for (int col = 0; col < GetNumColumns(right); ++col)
            intrinsicTypes.push_back(leftColumnType);
        // left matrix
        for (int col = 0; col < GetNumColumns(left); ++col)
            intrinsicTypes.push_back(leftColumnType);
        // right matrix
        for (int col = 0; col < GetNumColumns(right); ++col)
            intrinsicTypes.push_back(rightColumnType);

        // Get the intrinsic ID and get the right declaration
        llvm::Intrinsic::ID matTimesMatID;
        switch (GetNumColumns(left)) {
        case 2:
            switch (GetNumColumns(right)) {
            case 2:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix2TimesMatrix2; break;
            case 3:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix2TimesMatrix3; break;
            case 4:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix2TimesMatrix4; break;
            default: 
                UnsupportedFunctionality("matrix size");
                break;
            }
            break;
        case 3:
            switch (GetNumColumns(right)) {
            case 2:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix3TimesMatrix2; break;
            case 3:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix3TimesMatrix3; break;
            case 4:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix3TimesMatrix4; break;
            default: 
                UnsupportedFunctionality("matrix size");
                break;
            }
            break;
        case 4:
            switch (GetNumColumns(right)) {
            case 2:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix4TimesMatrix2; break;
            case 3:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix4TimesMatrix3; break;
            case 4:                           matTimesMatID = llvm::Intrinsic::gla_fMatrix4TimesMatrix4; break;
            default: 
                UnsupportedFunctionality("matrix size");
                break;
            }
            break;
        default: 
            UnsupportedFunctionality("matrix size");
            break;
        }
        llvm::Function* mulFunction = llvm::Intrinsic::getDeclaration(module, matTimesMatID, intrinsicTypes);

        // Put together the arguments
        std::vector<llvm::Value*> args;
        // left matrix
        for (int col = 0; col < GetNumColumns(left); ++col)
            args.push_back(builder.CreateExtractValue(left, col, "lcolumn"));
        // right matrix
        for (int col = 0; col < GetNumColumns(right); ++col)
            args.push_back(builder.CreateExtractValue(right, col, "rcolumn"));

        // Make the call
        llvm::Value* mulCall = builder.CreateCall(mulFunction, args);
        setInstructionPrecision(mulCall, precision);

        // Fill in the new matrix (which is an array) from the result of the call (which is as struct)
        for (int col = 0; col < GetNumColumns(result); ++col) {
            llvm::Value* column = builder.CreateExtractValue(mulCall, col, "memberColumn");
            result = builder.CreateInsertValue(result, column, col, "resultMatrix");
            setInstructionPrecision(result, precision);
        }

        return result;
    }

    // Allocate a column for intermediate results
    llvm::Value* column = createEntryAlloca(llvm::VectorType::get(GetMatrixElementType(left->getType()), rows));
    column = builder.CreateLoad(column, "tempColumn");

    for (int col = 0; col < columns; ++col) {
        llvm::Value* rightColumn = builder.CreateExtractValue(right, col, "rightColumn");
        setInstructionPrecision(rightColumn, precision);
        for (int row = 0; row < rows; ++row) {
            llvm::Value* dotProduct;

            for (int dotRow = 0; dotRow < GetNumRows(right); ++dotRow) {
                llvm::Value* leftColumn = builder.CreateExtractValue(left, dotRow,  "leftColumn");
                setInstructionPrecision(leftColumn, precision);
                llvm::Value* leftComp = builder.CreateExtractElement(leftColumn, MakeUnsignedConstant(context, row), "leftComp");
                setInstructionPrecision(leftComp, precision);
                llvm::Value* rightComp = builder.CreateExtractElement(rightColumn, MakeUnsignedConstant(context, dotRow), "rightComp");
                setInstructionPrecision(rightComp, precision);
                llvm::Value* product = builder.CreateFMul(leftComp, rightComp, "product");
                setInstructionPrecision(product, precision);
                if (dotRow == 0)
                    dotProduct = product;
                else {
                    dotProduct = builder.CreateFAdd(dotProduct, product, "dotProduct");
                    setInstructionPrecision(dotProduct, precision);
                }
            }
            column = builder.CreateInsertElement(column, dotProduct, MakeUnsignedConstant(context, row), "column");
            setInstructionPrecision(column, precision);
        }

        result = builder.CreateInsertValue(result, column, col, "resultMatrix");
        setInstructionPrecision(result, precision);
    }

    return result;
}

llvm::Value* Builder::createOuterProduct(gla::EMdPrecision precision, llvm::Value* left, llvm::Value* right)
{
    // Allocate a matrix to hold the result in
    int rows = GetComponentCount(left);
    int columns =  GetComponentCount(right);
    llvm::Value* result = createEntryAlloca(getMatrixType(left->getType()->getContainedType(0), columns, rows));
    result = builder.CreateLoad(result);

    // Allocate a column for intermediate results
    llvm::Value* column = createEntryAlloca(left->getType());
    column = builder.CreateLoad(column);

    // Build it up column by column, element by element
    for (int col = 0; col < columns; ++col) {
        llvm::Value* rightComp = builder.CreateExtractElement(right, MakeUnsignedConstant(context, col), "rightComp");
        setInstructionPrecision(rightComp, precision);
        for (int row = 0; row < rows; ++row) {
            llvm::Value*  leftComp = builder.CreateExtractElement( left, MakeUnsignedConstant(context, row),  "leftComp");
            setInstructionPrecision(leftComp, precision);
            llvm::Value* element = builder.CreateFMul(leftComp, rightComp, "element");
            setInstructionPrecision(element, precision);
            column = builder.CreateInsertElement(column, element, MakeUnsignedConstant(context, row), "column");
            setInstructionPrecision(column, precision);
        }
        result = builder.CreateInsertValue(result, column, col, "matrix");
        setInstructionPrecision(result, precision);
    }

    return result;
}

// Get intrinsic declarations
llvm::Function* Builder::getIntrinsic(llvm::Intrinsic::ID ID)
{
    // Look up the intrinsic
    return llvm::Intrinsic::getDeclaration(module, ID);
}

llvm::Function* Builder::getIntrinsic(llvm::Intrinsic::ID ID, llvm::Type* type1)
{
    llvm::Type* intrinsicTypes[] = {
        type1 };

    // Look up the intrinsic
    return llvm::Intrinsic::getDeclaration(module, ID, intrinsicTypes);
}

llvm::Function* Builder::getIntrinsic(llvm::Intrinsic::ID ID, llvm::Type* type1, llvm::Type* type2)
{
    llvm::Type* intrinsicTypes[] = {
        type1,
        type2 };

    // Look up the intrinsic
    return llvm::Intrinsic::getDeclaration(module, ID, intrinsicTypes);
}

llvm::Function* Builder::getIntrinsic(llvm::Intrinsic::ID ID, llvm::Type* type1, llvm::Type* type2, llvm::Type* type3)
{
    llvm::Type* intrinsicTypes[] = {
        type1,
        type2,
        type3 };

    // Look up the intrinsic
    return llvm::Intrinsic::getDeclaration(module, ID, intrinsicTypes);
}

llvm::Function* Builder::getIntrinsic(llvm::Intrinsic::ID ID, llvm::Type* type1, llvm::Type* type2, llvm::Type* type3, llvm::Type* type4)
{
    llvm::Type* intrinsicTypes[] = {
        type1,
        type2,
        type3,
        type4 };

    // Look up the intrinsic
    return llvm::Intrinsic::getDeclaration(module, ID, intrinsicTypes);
}

llvm::Function* Builder::getIntrinsic(llvm::Intrinsic::ID ID, llvm::Type* type1, llvm::Type* type2, llvm::Type* type3, llvm::Type* type4, llvm::Type* type5)
{
    llvm::Type* intrinsicTypes[] = {
        type1,
        type2,
        type3,
        type4,
        type5};

    // Look up the intrinsic
    return llvm::Intrinsic::getDeclaration(module, ID, intrinsicTypes);
}

llvm::Function* Builder::getIntrinsic(llvm::Intrinsic::ID ID, llvm::Type* type1, llvm::Type* type2, llvm::Type* type3, llvm::Type* type4, llvm::Type* type5, llvm::Type* type6)
{
    llvm::Type* intrinsicTypes[] = {
        type1,
        type2,
        type3,
        type4,
        type5,
        type6};

    // Look up the intrinsic
    return llvm::Intrinsic::getDeclaration(module, ID, intrinsicTypes);
}

// Comments in header
void Builder::promoteScalar(gla::EMdPrecision precision, llvm::Value*& left, llvm::Value*& right)
{
    int direction;
    if (const llvm::PointerType* pointer = llvm::dyn_cast<const llvm::PointerType>(left->getType()))
        direction = GetComponentCount(right) - GetComponentCount(pointer->getContainedType(0));
    else
        direction = GetComponentCount(right) - GetComponentCount(left);

    if (direction > 0)
        left = gla::Builder::smearScalar(precision, left, right->getType());
    else if (direction < 0)
        right = gla::Builder::smearScalar(precision, right, left->getType());

    return;
}

// Comments in header
llvm::Value* Builder::smearScalar(gla::EMdPrecision precision, llvm::Value* scalar, llvm::Type* vectorType)
{
    assert(gla::IsScalar(scalar->getType()));

    return createSwizzle(precision, scalar, 0x00, vectorType);
}

// Accept all parameters needed to create LunarGLASS texture intrinsics
// Select the correct intrinsic based on the inputs, and make the call
llvm::Value* Builder::createTextureCall(gla::EMdPrecision precision, llvm::Type* resultType, gla::ESamplerType samplerType, int texFlags, const TextureParameters& parameters, const char* name)
{
    bool floatReturn = gla::GetBasicType(resultType)->isFloatTy();

    // Max args based on LunarGLASS TopIR, no SOA
    static const int maxTextureArgs = 10;
    llvm::Value* texArgs[maxTextureArgs] = {};

    // Base case: First texture arguments are fixed for most intrinsics
    int numArgs = 4;

    texArgs[GetTextureOpIndex(ETOSamplerType)] = MakeIntConstant(context, samplerType);
    texArgs[GetTextureOpIndex(ETOSamplerLoc)]  = parameters.ETPSampler;
    texArgs[GetTextureOpIndex(ETOFlag)]        = MakeUnsignedConstant(context, *(int*)&texFlags);
    texArgs[GetTextureOpIndex(ETOCoord)]       = parameters.ETPCoords;

    llvm::Intrinsic::ID intrinsicID = llvm::Intrinsic::not_intrinsic;

    // Look at feature flags to determine which intrinsic is needed
    if (texFlags & ETFFetch) {
        intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTexelFetchOffset
                                    : llvm::Intrinsic::gla_texelFetchOffset;
    } else if (texFlags & ETFGather) {
        if (texFlags & ETFOffsetArg) {
            if (texFlags & ETFOffsets) {
                intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTexelGatherOffsets
                                            : llvm::Intrinsic::gla_texelGatherOffsets;
            } else
                intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTexelGatherOffset
                                            : llvm::Intrinsic::gla_texelGatherOffset;
        } else {
            intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTexelGather
                                        : llvm::Intrinsic::gla_texelGather;
        }
    } else if (parameters.ETPGradX || parameters.ETPGradY) {
        intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTextureSampleLodRefZOffsetGrad
                                    : llvm::Intrinsic::gla_textureSampleLodRefZOffsetGrad;
    } else if (texFlags & ETFOffsetArg) {
        intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTextureSampleLodRefZOffset
                                    : llvm::Intrinsic::gla_textureSampleLodRefZOffset;
    } else if (texFlags & ETFBias || texFlags & ETFLod || texFlags & ETFShadow) {
        intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTextureSampleLodRefZ
                                    : llvm::Intrinsic::gla_textureSampleLodRefZ;
    } else {
        intrinsicID = (floatReturn) ? llvm::Intrinsic::gla_fTextureSample
                                    : llvm::Intrinsic::gla_textureSample;
    }

    // Set fields based on argument flags
    if (texFlags & ETFBiasLodArg || texFlags & ETFComponentArg)
        texArgs[GetTextureOpIndex(ETOBiasLod)] = parameters.ETPBiasLod;

    if (texFlags & ETFRefZArg)
        texArgs[GetTextureOpIndex(ETORefZ)] = parameters.ETPShadowRef;

    if (texFlags & ETFOffsetArg) {
        if (texFlags & ETFOffsets) {
            llvm::ArrayType* offsets = llvm::dyn_cast<llvm::ArrayType>(parameters.ETPOffset->getType());
            assert(offsets->getNumElements() == 4);
            for (int i = 0; i < 4; ++i)
                texArgs[GetTextureOpIndex(ETOOffset) + i] = builder.CreateExtractValue(parameters.ETPOffset, i);
        } else
            texArgs[GetTextureOpIndex(ETOOffset)] = parameters.ETPOffset;
    }

    llvm::Function* intrinsic = 0;

    // Initialize required operands based on intrinsic
    switch (intrinsicID) {
    case llvm::Intrinsic::gla_textureSample:
    case llvm::Intrinsic::gla_fTextureSample:
        // Base case
        break;

    case llvm::Intrinsic::gla_textureSampleLodRefZ:
    case llvm::Intrinsic::gla_fTextureSampleLodRefZ:

        numArgs = 6;

        if (! texArgs[GetTextureOpIndex(ETOBiasLod)])
            texArgs[GetTextureOpIndex(ETOBiasLod)] = llvm::UndefValue::get(GetFloatType(context));

        if (! texArgs[GetTextureOpIndex(ETORefZ)])
            texArgs[GetTextureOpIndex(ETORefZ)]    = llvm::UndefValue::get(GetFloatType(context));

        // Texcoords are the only flexible parameter for this intrinsic, no need to getIntrinsic here

        break;

    case llvm::Intrinsic::gla_textureSampleLodRefZOffset:
    case llvm::Intrinsic::gla_fTextureSampleLodRefZOffset:

        numArgs = 7;

        if (! texArgs[GetTextureOpIndex(ETOBiasLod)])
            texArgs[GetTextureOpIndex(ETOBiasLod)] = llvm::UndefValue::get(GetFloatType(context));

        if (! texArgs[GetTextureOpIndex(ETORefZ)])
            texArgs[GetTextureOpIndex(ETORefZ)]    = llvm::UndefValue::get(GetFloatType(context));

        if (! texArgs[GetTextureOpIndex(ETOOffset)])
            texArgs[GetTextureOpIndex(ETOOffset)]  = llvm::UndefValue::get(GetIntType(context));
        
        // We know our flexible types when looking at the intrinsicID, so create our intrinsic here
        intrinsic = getIntrinsic(intrinsicID, resultType, texArgs[GetTextureOpIndex(ETOCoord)]->getType(),
                                                          texArgs[GetTextureOpIndex(ETOOffset)]->getType());

        break;

    case llvm::Intrinsic::gla_textureSampleLodRefZOffsetGrad:
    case llvm::Intrinsic::gla_fTextureSampleLodRefZOffsetGrad:

        numArgs = 9;

        if (! texArgs[GetTextureOpIndex(ETOBiasLod)])
            texArgs[GetTextureOpIndex(ETOBiasLod)] = llvm::UndefValue::get(GetFloatType(context));

        if (! texArgs[GetTextureOpIndex(ETORefZ)])
            texArgs[GetTextureOpIndex(ETORefZ)]    = llvm::UndefValue::get(GetFloatType(context));

        if (! texArgs[GetTextureOpIndex(ETOOffset)])
            texArgs[GetTextureOpIndex(ETOOffset)]  = llvm::UndefValue::get(GetIntType(context));

        assert(parameters.ETPGradX);
        assert(parameters.ETPGradY);

        texArgs[GetTextureOpIndex(ETODPdx)] = parameters.ETPGradX;
        texArgs[GetTextureOpIndex(ETODPdy)] = parameters.ETPGradY;

        // We know our flexible types when looking at the intrinsicID, so create our intrinsic here
        intrinsic = getIntrinsic(intrinsicID, resultType, texArgs[GetTextureOpIndex(ETOCoord)]->getType(),
                                                          texArgs[GetTextureOpIndex(ETOOffset)]->getType(),
                                                          texArgs[GetTextureOpIndex(ETODPdx)]->getType(),
                                                          texArgs[GetTextureOpIndex(ETODPdy)]->getType());

        break;

    case llvm::Intrinsic::gla_texelFetchOffset:
    case llvm::Intrinsic::gla_fTexelFetchOffset:

        // LOD and sample can share the BiasLod field
        // RefZ is provided so our operand order matches every other texture op
        numArgs = 7;

        if (! texArgs[GetTextureOpIndex(ETOBiasLod)])
            texArgs[GetTextureOpIndex(ETOBiasLod)] = llvm::UndefValue::get(GetIntType(context));

        if (! texArgs[GetTextureOpIndex(ETORefZ)])
            texArgs[GetTextureOpIndex(ETORefZ)]    = llvm::UndefValue::get(GetFloatType(context));

        if (! texArgs[GetTextureOpIndex(ETOOffset)])
            texArgs[GetTextureOpIndex(ETOOffset)]  = llvm::UndefValue::get(GetIntType(context));

        // We know our flexible types when looking at the intrinsicID, so create our intrinsic here
        intrinsic = getIntrinsic(intrinsicID, resultType, texArgs[GetTextureOpIndex(ETOCoord)]->getType(),
                                                          texArgs[GetTextureOpIndex(ETOBiasLod)]->getType(),
                                                          texArgs[GetTextureOpIndex(ETOOffset)]->getType());

        break;

    case llvm::Intrinsic::gla_texelGather:
    case llvm::Intrinsic::gla_fTexelGather:
    case llvm::Intrinsic::gla_texelGatherOffset:
    case llvm::Intrinsic::gla_fTexelGatherOffset:
    case llvm::Intrinsic::gla_texelGatherOffsets:
    case llvm::Intrinsic::gla_fTexelGatherOffsets:

        // Component select resides in BiasLod field
        if (! texArgs[GetTextureOpIndex(ETOBiasLod)])
            texArgs[GetTextureOpIndex(ETOBiasLod)] = llvm::UndefValue::get(GetIntType(context));

        if (! texArgs[GetTextureOpIndex(ETORefZ)])
            texArgs[GetTextureOpIndex(ETORefZ)]    = llvm::UndefValue::get(GetFloatType(context));

        intrinsic = getIntrinsic(intrinsicID, resultType, texArgs[GetTextureOpIndex(ETOCoord)]->getType());

        switch (intrinsicID) {
        case llvm::Intrinsic::gla_texelGather:
        case llvm::Intrinsic::gla_fTexelGather:
            numArgs = 6;
            break;
        case llvm::Intrinsic::gla_texelGatherOffset:
        case llvm::Intrinsic::gla_fTexelGatherOffset:
            numArgs = 7;
            break;
        case llvm::Intrinsic::gla_texelGatherOffsets:
        case llvm::Intrinsic::gla_fTexelGatherOffsets:
            numArgs = 10;
            break;
        default:
            assert(0);
            break;                
        }
        break;

    default:
        gla::UnsupportedFunctionality("Texture intrinsic: ", intrinsicID);
        break;
    }

    // If we haven't already set our intrinsic, do so now with coordinates
    if (! intrinsic)
        intrinsic = getIntrinsic(intrinsicID, resultType, texArgs[GetTextureOpIndex(ETOCoord)]->getType());

    assert(intrinsic);

    llvm::Instruction* instr = builder.CreateCall(intrinsic, llvm::ArrayRef<llvm::Value*>(texArgs, texArgs + numArgs), name ? name : "txt");
    setInstructionPrecision(instr, precision);

    return instr;
}

// Comments in header
llvm::Value* Builder::createTextureQueryCall(gla::EMdPrecision precision, llvm::Intrinsic::ID intrinsicID, llvm::Type* returnType, llvm::Constant* samplerType, llvm::Value* sampler,
                                             llvm::Value* arg2, const char* name)
{
    llvm::Function* intrinsicName = 0;

    switch (intrinsicID) {
    case llvm::Intrinsic::gla_queryTextureSize:
        intrinsicName = getIntrinsic(intrinsicID, returnType);
        break;
    case llvm::Intrinsic::gla_fQueryTextureLod:
        intrinsicName = getIntrinsic(intrinsicID, returnType, arg2->getType());
        break;
    case llvm::Intrinsic::gla_queryTextureSizeNoLod:
        intrinsicName = getIntrinsic(intrinsicID, returnType);
        break;
    default:
        gla::UnsupportedFunctionality("Texture query intrinsic");
        break;
    }

    assert(intrinsicName);

    llvm::Instruction* instr;
    if (arg2)
        instr = builder.CreateCall3(intrinsicName, samplerType, sampler, arg2, name ? name : "txtQ");
    else
        instr = builder.CreateCall2(intrinsicName, samplerType, sampler, name ? name : "txtQ");

    setInstructionPrecision(instr, precision);

    return instr;
}

// Comments in header
llvm::Value* Builder::createSamplePositionCall(gla::EMdPrecision precision, llvm::Type* returnType, llvm::Value* sampleIdx)
{
    // Return type is only flexible type
    llvm::Function* intrinsicName = getIntrinsic(llvm::Intrinsic::gla_fSamplePosition, returnType);

    llvm::Instruction* instr = builder.CreateCall(intrinsicName, sampleIdx, "samPos");
    setInstructionPrecision(instr, precision);

    return instr;
}

// Comments in header
llvm::Value* Builder::createBitFieldExtractCall(gla::EMdPrecision precision, llvm::Value* value, llvm::Value* offset, llvm::Value* bits, bool isSigned)
{
    llvm::Intrinsic::ID intrinsicID = isSigned ? llvm::Intrinsic::gla_sBitFieldExtract
                                               : llvm::Intrinsic::gla_uBitFieldExtract;

    if (IsScalar(offset) == false || IsScalar(bits) == false)
        gla::UnsupportedFunctionality("bitFieldExtract operand types");

    // Dest and value are matching flexible types
    llvm::Function* intrinsicName = getIntrinsic(intrinsicID, value->getType(), value->getType());

    assert(intrinsicName);

    llvm::Instruction* instr = builder.CreateCall3(intrinsicName, value, offset, bits, "bitfe");
    setInstructionPrecision(instr, precision);

    return instr;
}

// Comments in header
llvm::Value* Builder::createBitFieldInsertCall(gla::EMdPrecision precision, llvm::Value* base, llvm::Value* insert, llvm::Value* offset, llvm::Value* bits)
{
    llvm::Intrinsic::ID intrinsicID = llvm::Intrinsic::gla_bitFieldInsert;

    if (IsScalar(offset) == false || IsScalar(bits) == false)
        gla::UnsupportedFunctionality("bitFieldInsert operand types");

    // Dest, base, and insert are matching flexible types
    llvm::Function* intrinsicName = getIntrinsic(intrinsicID, base->getType(), base->getType(), base->getType());

    assert(intrinsicName);

    llvm::Instruction* instr = builder.CreateCall4(intrinsicName, base, insert, offset, bits, "bitfi");
    setInstructionPrecision(instr, precision);

    return instr;
}

// Comments in header
llvm::Value* Builder::createRecip(gla::EMdPrecision precision, llvm::Value* operand)
{
    llvm::Type* ty = operand->getType();

    if (! GetBasicType(ty)->isFloatTy()) {
        UnsupportedFunctionality("Unknown type to take reciprocal of: ", ty->getTypeID());
        
        return 0;
    }

    llvm::Value* recip = builder.CreateFDiv(llvm::ConstantFP::get(ty, 1.0), operand, "recip");
    setInstructionPrecision(recip, precision);    

    return recip;
}

// Comments in header
llvm::Value* Builder::createCompare(gla::EMdPrecision precision, llvm::Value* value1, llvm::Value* value2, bool equal, const char* name)
{
    if (llvm::isa<llvm::PointerType>(value1->getType()))
        value1 = builder.CreateLoad(value1);
    if (llvm::isa<llvm::PointerType>(value2->getType()))
        value2 = builder.CreateLoad(value2);

    llvm::Value* result;

    // Directly compare scalars and vectors.

    if (IsScalar(value1) || IsVector(value1)) {
        if (GetBasicTypeID(value1) == llvm::Type::FloatTyID) {
            if (equal)
                result = builder.CreateFCmpOEQ(value1, value2);
            else
                result = builder.CreateFCmpONE(value1, value2);
        } else {
            if (equal)
                result = builder.CreateICmpEQ(value1, value2);
            else
                result = builder.CreateICmpNE(value1, value2);
        }
        setInstructionPrecision(result, precision);
    }

    if (IsScalar(value1))
        return result;

    // Reduce vector compares with any() and all().

    if (IsVector(value1)) {
        llvm::Intrinsic::ID intrinsicID;
        if (equal)
            intrinsicID = llvm::Intrinsic::gla_all;
        else
            intrinsicID = llvm::Intrinsic::gla_any;

        return createIntrinsicCall(precision, intrinsicID, result, name ? name : "cc");
    }

    // Recursively handle aggregates, which include matrices, arrays, and structures
    // and accumulate the results.

    // arrays (includes matrices)
    int numElements;
    const llvm::ArrayType* arrayType = llvm::dyn_cast<llvm::ArrayType>(value1->getType());
    if (arrayType)
        numElements = (int)arrayType->getNumElements();
    else {
        // better be structure
        const llvm::StructType* structType = llvm::dyn_cast<llvm::StructType>(value1->getType());
        assert(structType);
        numElements = structType->getNumElements();
    }

    assert(numElements > 0);

    for (int element = 0; element < numElements; ++element) {
        // Get intermediate comparison values
        llvm::Value* element1 = builder.CreateExtractValue(value1, element, "element1");
        setInstructionPrecision(element1, precision);
        llvm::Value* element2 = builder.CreateExtractValue(value2, element, "element2");
        setInstructionPrecision(element2, precision);

        llvm::Value* subResult = createCompare(precision, element1, element2, equal, "comp");

        // Accumulate intermediate comparison
        if (element == 0)
            result = subResult;
        else {
            if (equal)
                result = builder.CreateAnd(result, subResult);
            else
                result = builder.CreateOr(result, subResult);
            setInstructionPrecision(result, precision);
        }
    }

    return result;
}

// Comments in header
llvm::Value* Builder::createIntrinsicCall(llvm::Intrinsic::ID intrinsicID)
{
    llvm::Instruction* instr = builder.CreateCall(getIntrinsic(intrinsicID));

    return instr;
}

// Comments in header
llvm::Value* Builder::createIntrinsicCall(gla::EMdPrecision precision, llvm::Intrinsic::ID intrinsicID)
{
    llvm::Instruction* instr = builder.CreateCall(getIntrinsic(intrinsicID));
    setInstructionPrecision(instr, precision);
    
    return instr;
}

// Comments in header
llvm::Value* Builder::createIntrinsicCall(gla::EMdPrecision precision, llvm::Intrinsic::ID intrinsicID, llvm::Value* operand, const char* name)
{
    llvm::Function* intrinsicName = 0;

    // Handle special return types here.  Things that don't have same result type as parameter
    switch (intrinsicID) {
    case llvm::Intrinsic::gla_fIsNan:
    case llvm::Intrinsic::gla_fIsInf:
        intrinsicName = getIntrinsic(intrinsicID, gla::GetVectorOrScalarType(gla::GetBoolType(context), gla::GetComponentCount(operand)), operand->getType());
        break;
    case llvm::Intrinsic::gla_fFloatBitsToInt:
        intrinsicName = getIntrinsic(intrinsicID, gla::GetVectorOrScalarType(gla::GetIntType(context), gla::GetComponentCount(operand)), operand->getType());
        break;
    case llvm::Intrinsic::gla_fIntBitsTofloat:
        intrinsicName = getIntrinsic(intrinsicID, gla::GetVectorOrScalarType(gla::GetFloatType(context), gla::GetComponentCount(operand)), operand->getType());
        break;
    case llvm::Intrinsic::gla_fPackSnorm2x16:
    case llvm::Intrinsic::gla_fPackUnorm2x16:
    case llvm::Intrinsic::gla_fPackHalf2x16:
        intrinsicName = getIntrinsic(intrinsicID, gla::GetUintType(context), gla::GetVectorOrScalarType(gla::GetFloatType(context), 2));
        break;
    case llvm::Intrinsic::gla_fUnpackUnorm2x16:
    case llvm::Intrinsic::gla_fUnpackSnorm2x16:
    case llvm::Intrinsic::gla_fUnpackHalf2x16:
        intrinsicName = getIntrinsic(intrinsicID, gla::GetVectorOrScalarType(gla::GetFloatType(context), 2), gla::GetUintType(context));
        break;

    case llvm::Intrinsic::gla_fFrexp:
    case llvm::Intrinsic::gla_fLdexp:
    case llvm::Intrinsic::gla_fPackUnorm4x8:
    case llvm::Intrinsic::gla_fPackSnorm4x8:
    case llvm::Intrinsic::gla_fUnpackUnorm4x8:
    case llvm::Intrinsic::gla_fUnpackSnorm4x8:
    case llvm::Intrinsic::gla_fPackDouble2x32:
    case llvm::Intrinsic::gla_fUnpackDouble2x32:
        // TODO: desktop functionality: Hook these up
        gla::UnsupportedFunctionality("unary intrinsic", intrinsicID);
        break;
    case llvm::Intrinsic::gla_fLength:
       // scalar result type
       intrinsicName = getIntrinsic(intrinsicID, GetBasicType(operand->getType()), operand->getType());
       break;
    case llvm::Intrinsic::gla_any:
    case llvm::Intrinsic::gla_all:
        // fixed result type
        intrinsicName = getIntrinsic(intrinsicID, operand->getType());
        break;
    case llvm::Intrinsic::gla_fModF:
        // modf() will return a struct that the caller must decode
        intrinsicName = getIntrinsic(intrinsicID, operand->getType(), operand->getType(), operand->getType());
        break;
    default:
        // Unary intrinsics that have operand and dest with same flexible type
        intrinsicName = getIntrinsic(intrinsicID,  operand->getType(), operand->getType());
        break;
    }

    assert(intrinsicName);

    llvm::Instruction* instr;
    if (name)
        instr = builder.CreateCall(intrinsicName, operand, name);
    else
        instr = builder.CreateCall(intrinsicName, operand);

    setInstructionPrecision(instr, precision);

    return instr;
}

// Comments in header
llvm::Value* Builder::createIntrinsicCall(gla::EMdPrecision precision, llvm::Intrinsic::ID intrinsicID, llvm::Value* operand0, llvm::Value* operand1, const char* name)
{
    llvm::Function* intrinsicName = 0;

    // Handle special return types here.  Things that don't have same result type as parameter
    switch (intrinsicID) {
    case llvm::Intrinsic::gla_fDistance:
    case llvm::Intrinsic::gla_fDot2:
    case llvm::Intrinsic::gla_fDot3:
    case llvm::Intrinsic::gla_fDot4:
        // scalar result type
        intrinsicName = getIntrinsic(intrinsicID, GetBasicType(operand0), operand0->getType(), operand1->getType());
        break;
    case llvm::Intrinsic::gla_fStep:
        // first argument can be scalar, return and second argument match
        intrinsicName = getIntrinsic(intrinsicID, operand1->getType(), operand0->getType(), operand1->getType());
        break;
    case llvm::Intrinsic::gla_fSmoothStep:
        // first argument can be scalar, return and second argument match
        intrinsicName = getIntrinsic(intrinsicID, operand1->getType(), operand0->getType(), operand1->getType());
        break;
    default:
        // Binary intrinsics that have operand and dest with same flexible type
        intrinsicName = getIntrinsic(intrinsicID,  operand0->getType(), operand0->getType(), operand1->getType());
        break;
    }

    assert(intrinsicName);

    llvm::Instruction* instr;
    if (name)
        instr = builder.CreateCall2(intrinsicName, operand0, operand1, name);
    else
        instr = builder.CreateCall2(intrinsicName, operand0, operand1);

    setInstructionPrecision(instr, precision);

    return instr;
}

llvm::Value* Builder::createIntrinsicCall(gla::EMdPrecision precision, llvm::Intrinsic::ID intrinsicID, llvm::Value* operand0, llvm::Value* operand1, llvm::Value* operand2, const char* name)
{
    llvm::Function* intrinsicName;

    // Handle special return types here.  Things that don't have same result type as parameter
    switch (intrinsicID) {
    case llvm::Intrinsic::gla_fSmoothStep:
        // first argument can be scalar, return and second argument match
        intrinsicName = getIntrinsic(intrinsicID, operand2->getType(), operand0->getType(), operand1->getType(), operand2->getType());
        break;
    default:
        // Use operand0 type as result type
        intrinsicName =  getIntrinsic(intrinsicID, operand0->getType(), operand0->getType(), operand1->getType(), operand2->getType());
        break;
    }

    assert(intrinsicName);

    llvm::Instruction* instr;
    if (name)
        instr = builder.CreateCall3(intrinsicName, operand0, operand1, operand2, name);
    else
        instr = builder.CreateCall3(intrinsicName, operand0, operand1, operand2);
    setInstructionPrecision(instr, precision);

    return instr;
}

// Vector constructor
llvm::Value* Builder::createConstructor(gla::EMdPrecision precision, const std::vector<llvm::Value*>& sources, llvm::Value* constructee)
{
    unsigned int numTargetComponents = GetComponentCount(constructee);
    unsigned int targetComponent = 0;

    // Special case: when calling a vector constructor with a single scalar
    // argument, smear the scalar
    if (sources.size() == 1 && IsScalar(sources[0]) && numTargetComponents > 1) {
        return smearScalar(precision, sources[0], constructee->getType());
    }

    for (unsigned int i = 0; i < sources.size(); ++i) {
        if (IsAggregate(sources[i]))
            gla::UnsupportedFunctionality("aggregate in vector constructor");

        unsigned int sourceSize = GetComponentCount(sources[i]);

        unsigned int sourcesToUse = sourceSize;
        if (sourcesToUse + targetComponent > numTargetComponents)
            sourcesToUse = numTargetComponents - targetComponent;

        for (unsigned int s = 0; s < sourcesToUse; ++s) {
            llvm::Value* arg = sources[i];
            if (sourceSize > 1) {
                arg = builder.CreateExtractElement(arg, MakeIntConstant(context, s));
                setInstructionPrecision(arg, precision);
            }
            if (numTargetComponents > 1) {
                constructee = builder.CreateInsertElement(constructee, arg, MakeIntConstant(context, targetComponent));
                setInstructionPrecision(constructee, precision);
            } else
                constructee = arg;
            ++targetComponent;
        }

        if (targetComponent >= numTargetComponents)
            break;
    }

    return constructee;
}

// Comments in header
llvm::Value* Builder::createMatrixConstructor(gla::EMdPrecision precision, const std::vector<llvm::Value*>& sources, llvm::Value* constructee)
{
    llvm::Value* matrixee = constructee;

    // Will use a two step process
    // 1. make a compile-time 2D array of values
    // 2. copy it into the run-time constructee

    // Step 1.

    // initialize the array to the identity matrix
    llvm::Value* values[maxMatrixSize][maxMatrixSize];
    llvm::Value*  one = gla::MakeFloatConstant(context, 1.0);
    llvm::Value* zero = gla::MakeFloatConstant(context, 0.0);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (col == row)
                values[col][row] = one;
            else
                values[col][row] = zero;
        }
    }

    // modify components as dictated by the arguments
    if (sources.size() == 1 && IsScalar(sources[0])) {
        // a single scalar; resets the diagonals
        for (int col = 0; col < 4; ++col)
            values[col][col] = sources[0];
    } else if (IsAggregate(sources[0])) {
        // a matrix; copy over the parts that exist in both the argument and constructee
        llvm::Value* matrix = sources[0];
        int minCols = std::min(GetNumColumns(matrixee), GetNumColumns(matrix));
        int minRows = std::min(GetNumRows(matrixee), GetNumRows(matrix));
        for (int col = 0; col < minCols; ++col) {
            llvm::Value* column = builder.CreateExtractValue(matrix, col, "column");
            setInstructionPrecision(column, precision);
            for (int row = 0; row < minRows; ++row) {
                values[col][row] = builder.CreateExtractElement(column, MakeUnsignedConstant(context, row), "element");
                setInstructionPrecision(values[col][row], precision);
            }
        }
    } else {
        // fill in the matrix in column-major order with whatever argument components are available
        int row = 0;
        int col = 0;

        for (int arg = 0; arg < (int)sources.size(); ++arg) {
            llvm::Value* argComp = sources[arg];
            for (int comp = 0; comp < GetComponentCount(sources[arg]); ++comp) {
                if (GetComponentCount(sources[arg]) > 1) {
                    argComp = builder.CreateExtractElement(sources[arg], MakeUnsignedConstant(context, comp), "element");
                    setInstructionPrecision(argComp, precision);
                }
                values[col][row++] = argComp;
                if (row == GetNumRows(matrixee)) {
                    row = 0;
                    col++;
                }
            }
        }
    }

    // Step 2:  Copy into run-time result.
    for (int col = 0; col < GetNumColumns(matrixee); ++col) {
        llvm::Value* column = builder.CreateExtractValue(matrixee, col, "column");
        setInstructionPrecision(column, precision);
        for (int row = 0; row < GetNumRows(matrixee); ++row) {
            column = builder.CreateInsertElement(column, values[col][row], MakeIntConstant(context, row), "column");
            setInstructionPrecision(column, precision);
        }
        constructee = builder.CreateInsertValue(constructee, column, col, "matrix");
        setInstructionPrecision(constructee, precision);
    }

    return constructee;
}

// Comments in header
Builder::If::If(llvm::Value* cond, Builder* gb)
    : glaBuilder(gb)
    , condition(cond)
    , elseBB(0)
{
    function = glaBuilder->builder.GetInsertBlock()->getParent();

    // make the blocks, but only put the then-block into the function,
    // the else-block and merge-block will be added later, in order, after
    // earlier code is emitted
    thenBB = llvm::BasicBlock::Create(glaBuilder->context, "then", function);
    mergeBB = llvm::BasicBlock::Create(glaBuilder->context, "ifmerge");

    // Save the current block, so that we can add in the flow control split when
    // makeEndIf is called.
    headerBB = glaBuilder->builder.GetInsertBlock();

    glaBuilder->builder.SetInsertPoint(thenBB);
}

// Comments in header
void Builder::If::makeBeginElse()
{
    if (! glaBuilder->builder.GetInsertBlock()->getTerminator()) {
        // Close out the "then" by having it jump to the mergeBB
        glaBuilder->builder.CreateBr(mergeBB);
    }

    // Make the else
    elseBB = llvm::BasicBlock::Create(glaBuilder->context, "else");

    // add else block to the function
    function->getBasicBlockList().push_back(elseBB);
    glaBuilder->builder.SetInsertPoint(elseBB);
}

// Comments in header
void Builder::If::makeEndIf()
{
    if (! glaBuilder->builder.GetInsertBlock()->getTerminator()) {
        // jump to the merge block
        glaBuilder->builder.CreateBr(mergeBB);
    }

    if (! headerBB->getTerminator()) {
        // Go back the the headerBB and make the flow control split
        glaBuilder->builder.SetInsertPoint(headerBB);
        if (elseBB)
            glaBuilder->builder.CreateCondBr(condition, thenBB, elseBB);
        else
            glaBuilder->builder.CreateCondBr(condition, thenBB, mergeBB);
    }

    // add the merge block to the function
    function->getBasicBlockList().push_back(mergeBB);
    glaBuilder->builder.SetInsertPoint(mergeBB);
}

// Comments in header
void Builder::makeSwitch(llvm::Value* condition, int numSegments, std::vector<llvm::ConstantInt*> caseValues, std::vector<int> valueToSegment, int defaultSegment,
                         std::vector<llvm::BasicBlock*>& segmentBB)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    // make all the blocks
    for (int s = 0; s < numSegments; ++s)
        segmentBB.push_back(llvm::BasicBlock::Create(context, "switch-segment", function));

    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(context, "switch-merge", function);

    if (! builder.GetInsertBlock()->getTerminator()) {
        // make the switch instruction
        llvm::SwitchInst* switchInst = builder.CreateSwitch(condition, defaultSegment >= 0 ? segmentBB[defaultSegment] : mergeBlock, caseValues.size());
        for (int i = 0; i < (int)caseValues.size(); ++i)
            switchInst->addCase(caseValues[i], segmentBB[valueToSegment[i]]);
    }

    // push the merge block
    switches.push(mergeBlock);
}

// Comments in header
void Builder::addSwitchBreak()
{
    if (! builder.GetInsertBlock()->getTerminator()) {
        // branch to the top of the merge block stack
        builder.CreateBr(switches.top());
    }

    // subsequent code is unreachable    
    if (insertNoPredecessorBlocks)
        createAndSetNoPredecessorBlock("post-switch-break");
}

// Comments in header
void Builder::nextSwitchSegment(std::vector<llvm::BasicBlock*>& segmentBB, int nextSegment)
{
    int lastSegment = nextSegment - 1;
    if (lastSegment >= 0) {
        // Close out previous segment by jumping, if necessary, to next segment
        if (! builder.GetInsertBlock()->getTerminator())
            builder.CreateBr(segmentBB[nextSegment]);
    }
    builder.SetInsertPoint(segmentBB[nextSegment]);
}

// Comments in header
void Builder::endSwitch(std::vector<llvm::BasicBlock*>& segmentBB)
{
    // Close out previous segment by jumping, if necessary, to next segment
    if (! builder.GetInsertBlock()->getTerminator())
        addSwitchBreak();

    builder.SetInsertPoint(switches.top());

    switches.pop();
}

// Comments in header
void Builder::makeNewLoop()
{
    makeNewLoop(NULL, NULL, NULL, NULL, false);
}

// Comments in header
void Builder::makeNewLoop(llvm::Value* inductiveVariable, llvm::Constant* from, llvm::Constant* finish,
                          llvm::Constant* increment,  bool builderDoesIncrement)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    llvm::BasicBlock *headerBB = llvm::BasicBlock::Create(context, "loop-header", function);
    llvm::BasicBlock *mergeBB  = llvm::BasicBlock::Create(context, "loop-merge");

    LoopData ld = { };
    ld.exit   = mergeBB;
    ld.header = headerBB;
    ld.counter = inductiveVariable;
    ld.finish = finish;
    ld.increment = increment;
    ld.function = function;
    ld.builderDoesIncrement = builderDoesIncrement;

    // If we were passed a non-null inductive variable, then we're inductive
    if (inductiveVariable) {
        ld.isInductive = true;
        builder.CreateStore(from, inductiveVariable);
    }

    // If we were passed an inductive variable, all other arguments should be defined
    assert(! ld.isInductive || (inductiveVariable && from && finish && increment));

    loops.push(ld);

    if (! builder.GetInsertBlock()->getTerminator()) {
        // Branch into the loop
        builder.CreateBr(headerBB);
    }

    // Set ourselves inside the loop
    builder.SetInsertPoint(headerBB);
}

// Add a back-edge (e.g "continue") for the innermost loop that you're in
void Builder::makeLoopBackEdge(bool implicit)
{
    if (builder.GetInsertBlock()->getTerminator())
        return;

    LoopData ld = loops.top();

    // If we're not inductive, just branch back.
    if (! ld.isInductive) {
        if (builder.GetInsertBlock()->getTerminator())
            return;

        builder.CreateBr(ld.header);
        if (! implicit && insertNoPredecessorBlocks)
            createAndSetNoPredecessorBlock("post-loop-continue");

        return;
    }

    //  Otherwise we have to (possibly) increment the inductive variable, and
    // set up a conditional exit.
    assert(ld.counter && ld.counter->getType()->isPointerTy() && ld.increment && ld.finish);

    llvm::Value* iPrev = builder.CreateLoad(ld.counter);
    llvm::Value* cmp   = NULL;
    llvm::Value* iNext = NULL;

    // iNext is either iPrev if the user did the increment theirselves, or it is
    // the result of the increment if we have to do it ourselves.
    switch (ld.counter->getType()->getContainedType(0)->getTypeID()) {
    case llvm::Type::FloatTyID:
        iNext = ! ld.builderDoesIncrement ? iPrev : builder.CreateFAdd(iPrev, ld.increment);
        cmp   = builder.CreateFCmpOGE(iNext, ld.finish);
        break;
    case llvm::Type::IntegerTyID:
        iNext = ! ld.builderDoesIncrement ? iPrev : builder.CreateAdd(iPrev, ld.increment);
        cmp   = builder.CreateICmpSGE(iNext, ld.finish);
        break;
    default:
        gla::UnsupportedFunctionality("unknown type in inductive variable");
        break;
    }

    // Store the new value for the inductive variable back in
    if (ld.builderDoesIncrement)
        builder.CreateStore(iNext, ld.counter);

    // If iNext exceeds ld.finish, exit the loop, else branch back to
    // the header
    builder.CreateCondBr(cmp, ld.exit, ld.header);

    if (! implicit && insertNoPredecessorBlocks)
        createAndSetNoPredecessorBlock("post-loop-continue");
}

// Add an exit (e.g. "break") for the innermost loop that you're in
void Builder::makeLoopExit()
{
    if (builder.GetInsertBlock()->getTerminator())
        return;

    builder.CreateBr(loops.top().exit);

    if (insertNoPredecessorBlocks)
        createAndSetNoPredecessorBlock("post-loop-break");
}

// Close the innermost loop that you're in
void Builder::closeLoop()
{
    if (! builder.GetInsertBlock()->getTerminator()) {
        // Branch back through the loop
        makeLoopBackEdge(true);
    }

    LoopData ld = loops.top();

    ld.function->getBasicBlockList().push_back(ld.exit);
    builder.SetInsertPoint(ld.exit);

    loops.pop();
}

bool Builder::useColumnBasedMatrixIntrinsics() const
{
    // Note: if we knew all users of managers really derived from PrivateManager,
    // we wouldn't need the first test.
    return manager->getPrivateManager() && manager->getPrivateManager()->getBackEnd()->useColumnBasedMatrixIntrinsics();
}

bool Builder::useLogicalIo() const
{
    // Note: if we knew all users of managers really derived from PrivateManager,
    // we wouldn't need the first test.
    return manager->getPrivateManager() && manager->getPrivateManager()->getBackEnd()->useLogicalIo();
}

}; // end gla namespace
