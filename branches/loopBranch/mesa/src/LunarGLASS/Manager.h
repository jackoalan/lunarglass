//===- Manager.h - private implementation of public manager ---------------===//
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
// Author: John Kessenich, LunarG
//
// Private to LunarGLASS implementation, see LunarGLASSTopIR.h for public
// interface.
//
//===----------------------------------------------------------------------===//

#pragma once
#ifndef GlaManager_H
#define GlaManager_H

// LLVM includes
#include "llvm/Module.h"

// LunarGLASS includes
#include "LunarGLASSTopIR.h"

namespace gla {
    enum LoopExitType { ELETTopExit, ELETBottomExit, ELETNeither };

    class BackEndTranslator {
    public:
        BackEndTranslator() { }
        virtual ~BackEndTranslator() { }
        virtual void addGlobal(const llvm::GlobalVariable*) { }
        virtual void startFunctionDeclaration(const llvm::Type*, const std::string&) = 0;
        virtual void addArgument(const llvm::Value*, bool last) = 0;
        virtual void endFunctionDeclaration() = 0;
        virtual void startFunctionBody() = 0;
        virtual void endFunctionBody() = 0;
        virtual void add(const llvm::Instruction*, bool lastBlock) = 0;

        //
        // The following set of functions is motivated by need to convert to
        // structured flow control and eliminate phi functions.
        //

        // This must get called soon enough that it is before the split that
        // makes the phi.  It is then referred to in addPhiCopy().
        virtual void declarePhiCopy(const llvm::Value* dst) { }

        // Called for Phi elimination.
        virtual void addPhiCopy(const llvm::Value* dst, const llvm::Value* src) = 0;

        // Called to build structured flow control.
        virtual void addIf(const llvm::Value* cond, bool invert=false) = 0;
        virtual void addElse() = 0;
        virtual void addEndif() = 0;
        virtual void addLoop(LoopExitType, bool, const llvm::BasicBlock*) = 0;
        virtual void addLoopEnd(const llvm::BasicBlock*) = 0;

        // Exit the loop (e.g. break). This may be conditional or unconditional.
        // If conditional, then the backend should exit the loop if the
        // condition succeeds.
        virtual void addLoopExit(const llvm::BasicBlock*, bool invert=false) = 0;

        // Add a loop backedge (e.g. continue). Receives the basic block the
        // loop is in, and a bool donoting whether there is only one latch in
        // the entire loop.
        virtual void addLoopBack(const llvm::BasicBlock*, bool, bool invert=false) = 0;

        virtual void print() = 0;
    };

    class BackEnd;

    class PrivateManager : public gla::Manager {
    public:
        PrivateManager();
        virtual ~PrivateManager();

        virtual void setModule(llvm::Module* m) { module = m; }
        virtual void translateTopToBottom();
        virtual void translateBottomToTarget();
        virtual void setTarget(gla::BackEndTranslator* t) { backEndTranslator = t; }

    protected:
        virtual void runLLVMOptimizations1();
        llvm::Module* module;
        gla::BackEndTranslator* backEndTranslator;
        gla::BackEnd* backEnd;
    };
}


#endif /* GlaManager_H */