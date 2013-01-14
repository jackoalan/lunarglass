//===- GlslangToTop.cpp - Translate GLSL IR to LunarGLASS Top IR ---------===//
//
// LunarGLASS: An Open Modular Shader Compiler Architecture
// Copyright (C) 2012 LunarG, Inc.
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
//
// Translate glslang to LunarGLASS Top IR.
//
//===----------------------------------------------------------------------===//

// LLVM includes
#include "llvm/DerivedTypes.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IRBuilder.h"

#include <cstdio>
#include <string>
#include <map>
#include <vector>

// Glslang includes
#include "glslang/Include/intermediate.h"

// LunarGLASS includes
#include "LunarGLASSManager.h"

// Adapter includes
#include "GlslangToTopVisitor.h"

void TranslateGlslangToTop(TIntermNode* root, gla::Manager* manager)
{
    llvm::Module* topModule = new llvm::Module("Top", llvm::getGlobalContext());
    gla::PipelineSymbols* pipeOuts = new gla::PipelineSymbols;

    manager->setModule(topModule);
    manager->setPipeOutSymbols(pipeOuts);

    GlslangToTop(root, manager);
}
