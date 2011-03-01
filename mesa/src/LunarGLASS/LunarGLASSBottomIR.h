//===- LunarGLASSBottomIR.h - GLA functions for BottomIR ------------------===//
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
// Author: Cody Northrop, LunarG
//===----------------------------------------------------------------------===//

#pragma once
#ifndef LunarGLASSBottomIR_H
#define LunarGLASSBottomIR_H

#include "LunarGLASSTopIR.h"
#include "llvm/IntrinsicInst.h"

namespace gla {

    // Texture op constants, for mapping operands
    const int SamplerLocAOS = 1;
    const int FlagLocAOS    = 2;
    const int CoordLocAOS   = 3;
    const int BiasLocAOS    = 4;
    const int DdxLocAOS     = 6;
    const int DdyLocAOS     = 7;

    int GetConstantValue(const llvm::Value*);
    int IsGradientTexInst(const llvm::IntrinsicInst*);
    int GetComponentCount(const llvm::Type*);
    int GetComponentCount(const llvm::Value*);

};

#endif /* LunarGLASSBottomIR_H */