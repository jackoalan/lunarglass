//===- StructuralAnalysis.cpp - region detection for structural analysis --===//
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
// Perform Structural Analysis, as outlined in Muchnick
//
//===----------------------------------------------------------------------===//

#include "StructuralAnalysis.h"
#include <string>

using namespace llvm;

bool StructuralAnalysis::runOnFunction(Function &F) {
    errs() << "running on the function " << F.getName() << "\n";

    // Get analyses
    RI = &getAnalysis<RegionInfo>();
    LI = &getAnalysis<LoopInfo>();
    RI->print(errs(), NULL);
    LI->print(errs());

    initialize();

    tagNaturalLoops();

    printRegionTypes();

    return false;
}

void StructuralAnalysis::tagNaturalLoops() {
    for (SmallVector<Loop*, 5>::iterator i = untaggedLoops.begin(), e = untaggedLoops.end(); i != e; ++i){
        //loop over regions and check equality
    }
}

// Whether the loop's and region's nodes are the same
bool sameNodes(Loop* l, Region* r) {
    return false;
}

void StructuralAnalysis::initialize() {
    // Initialize regionToType and untagged
    regionToType.clear();
    untagged.clear();
    addInImmediateSubRegions(RI->getTopLevelRegion());

    // Initialized untaggedLoops
    untaggedLoops.clear();
    for (std::vector<Loop*>::const_iterator i = LI->begin(), e = LI->end(); i != e; ++i){
        addInImmediateSubLoops(*i);
    }


    return;
}

void StructuralAnalysis::addInImmediateSubLoops(Loop* l) {
    // Base case
    if (!l) return;

    // Add the loop
    untaggedLoops.push_back(l);

    // Add each subloop
    for (std::vector<Loop*>::const_iterator i = l->begin(), e = l->end(); i != e; ++i){
        // Recurse
        addInImmediateSubLoops(*i);
    }
}

void StructuralAnalysis::addInImmediateSubRegions(Region* top) {
    // Base case
    if (!top) return;

    // For each region, add it, and recurse on its subregions
    for (std::vector<Region*>::iterator i = top->begin(), e = top->end(); i != e; ++i){
        regionToType.insert(std::pair<Region*,RegionType>((*i), UNTAGGED));
        untagged.insert(*i);

        // Recurse
        addInImmediateSubRegions(*i);
    }
    return;
}

// Pretty Print the RegionType enum
std::string regionTypeToString(StructuralAnalysis::RegionType t) {
    switch (t){
    case StructuralAnalysis::IfThen:     return "IfThen";
    case StructuralAnalysis::IfThenElse: return "IfThenElse";
    case StructuralAnalysis::FallthroughSwitch: return "FallthroughSwitch";
    case StructuralAnalysis::NaturalLoop: return "NaturalLoop";
    case StructuralAnalysis::Improper: return "Improper";
    case StructuralAnalysis::Proper: return "Proper";
    case StructuralAnalysis::Blocks: return "Blocks";
    case StructuralAnalysis::UNTAGGED:   return "UNTAGGED";
    };
}

void StructuralAnalysis::printRegionTypes() {
    errs() << "Outputting mapping between regions and their types\n";

    for (StructuralAnalysis::RegionToTypeIterator i = regionToType.begin(), e = regionToType.end(); i != e; ++i) {
        errs() << "  Region: " << i->first->getNameStr() << " has type: " << regionTypeToString(i->second) << "\n" ;
    }
}

void StructuralAnalysis::print(raw_ostream &OS, const Module*) const {
    return;
}

void StructuralAnalysis::getAnalysisUsage(AnalysisUsage& AU) const {
    AU.setPreservesAll();
    AU.addRequiredTransitive<RegionInfo>();
    AU.addRequiredTransitive<LoopInfo>();
}


char StructuralAnalysis::ID = 0;
INITIALIZE_PASS(StructuralAnalysis,
                "sa",
                "Detect regions for structural analysis",
                true,   // Whether it preserves the CFG
                true);  // Is an analysis pass




