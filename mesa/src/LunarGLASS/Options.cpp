//===- Options.cpp - Global run-time options ------------------------------===//
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
// Global run-time options
//
//===----------------------------------------------------------------------===//

#include <Options.h>

// Description and usage information
const std::string Description = "\
Description: The LunarGLASS stand-alone shader compiler\n\
";

const std::string Usage = "\
Usage: ./StandAlone[.exe] [options] file1.frag ...\n\
\n\
       Options:\n\
         -h --help       Print out this Usage info\n\
         --dump-ast      Print out the AST\n\
         --use-glsl      Use the glsl backend (default)\n\
         --use-tgsi      Use the TGSI backed\n\
\n\
       GLSL-backed options:\n\
         -o --obfuscate  Obfuscate the output\n\
";

// Is the string an option/flagged argument?
bool isOption(std::string s) { return !s.compare(0, 1, "-"); }

namespace gla {

    OptionsType Options = { false   // Dump the ast
                          , false   // Obfuscate
                          , GLSL    // Backend
                          };


    // Print out description and help
    void printHelp() { std::cout << Description << Usage; }

    // Returns the index of the first non-flag argument
    // Assumes that all option/flagged arguments come before non-flagged arguments
    int handleArgs(int argc, char **argv) {
        using std::vector;
        using std::string;
        using std::iterator;

        int argIndex = 0;

        // Load up the flagged options
        vector<string> flaggedArgs;
        flaggedArgs.clear();
        for (int i = 1; i < argc; ++i) {
            if (isOption((string) argv[i])) {
                flaggedArgs.push_back(argv[i]);
            } else {
                argIndex = i;
                break;
            }
        }

        // Handle each option
        for (vector<string>::iterator i = flaggedArgs.begin(), e = flaggedArgs.end(); i != e; ++i){
            if (*i == "-h" || *i == "--help") {
                printHelp();
                exit(0);
            } else if (*i == "--dump-ast") {
                Options.dumpAst = true;
            } else if (*i == "--use-glsl") {
                Options.backend = GLSL;
            } else if (*i == "--use-tgsi") {
                Options.backend = TGSI;
            } else {
                std::cout << "Unknown option: " << *i << std::endl;
                printHelp();
                exit(0);
            }
        }

        return argIndex;
    }

}


