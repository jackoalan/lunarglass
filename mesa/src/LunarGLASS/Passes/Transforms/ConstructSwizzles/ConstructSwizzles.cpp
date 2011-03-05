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

using namespace llvm;

bool ConstructSwizzles::runOnFunction(Function& F) {
    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i){
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

