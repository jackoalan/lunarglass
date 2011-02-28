//===- Options.cpp - Help Translate GLSL IR to LunarGLASS Top IR -===//
//
// LunarGLASS: An Open Modular Shader Compiler Architecture
// Copyright © 2011, LunarG, Inc.
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


