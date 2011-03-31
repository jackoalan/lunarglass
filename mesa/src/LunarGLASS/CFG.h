//===- CFG.h - CFG helpers ------------------------------------------------===//
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
// Provide some Control Flow Graph helpers
//
//===----------------------------------------------------------------------===//

#ifndef CFG_H
#define CFG_H

namespace llvm {
    class BasicBlock;
} // end namespace llvm

namespace gla {

    // Find and return the earliest confluence point in the CFG of the given
    // basic blocks
    llvm::BasicBlock* FindEarliestConfluencePoint(llvm::BasicBlock*, llvm::BasicBlock*);

} // end namespace gla

#endif // CFG_H
