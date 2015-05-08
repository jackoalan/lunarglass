# Passed Milestones #

## First Milestone - Passed on 2011-03-11 ([r207](https://code.google.com/p/lunarglass/source/detail?r=207)) ##

Achieve the following functionality

  * 1.2 desktop shaders

But not yet able to do

  * ES-specific features (e.g. precision qualifiers)
  * Loops
  * Function calls
  * Structures
  * Vertex shaders
  * outputs other than `gl_FragColor`
  * matrices (uncommon in fragment shaders)
  * new-style texturing; only 1.2 texturing is supported
  * arrays
  * bitwise assignments e.g. `|=` and the logical operator `^^`
  * a few built-ins: trunc(), not(), ...

### Tasks completed ###

  * + Correct location of declarations for split control flow (JohnK)
  * + Have regression tests generating both Mesa IR and GLSL outputs (Michael)
  * + Finish generating Top IR texturing calls (Cody)
  * + Reconstruct texturing calls (Cody)
  * + Correct symbol names for external linkage (JohnK)
  * + Handle bool types in bottom (JohnK)
  * + Install an exception handling model so we can wrap failed runs and report the right error to the user. (Michael)
  * + Check all existing asserts and unsupported prints for correct usage in the exception model. (JohnK)
  * + Ensure all unsupported functionality has an exception raised, preferably going into Top IR, but somewhere
  * + Reduce output to just the shader (Michael)
  * + handle sampler types top to bottom (JohnK)
  * + broader set of tests for what is in this checkpoint (JohnK)
  * + Mixing scalars and vectors across operators (Cody)
  * + Top IR constructors, for simple common cases (Cody)
  * + Top IR L-value swizzling (Cody)
  * + GLSL back end-support for extract/insert (JohnK)
  * + Handle the (non-texturing) built-in functions (Cody)
  * + Run-time #version control of features: this is expected to be done through proper front-end translation to Top IR based on #version
  * + Have a better switch than gla::UseTgsiBackend for controlling what back end is used (Michael)
  * + handle built-in state?: `gl_*` for uniforms, varyings, attributes:  revisit/fix the built-in stuff Cody and John turned off when bootstrapping the front-end adapter
  * + get constants working top to bottom (JohnK)
  * + Run-time tests for what mode it's in; simple, obfuscating, or optimizing (JohnK)
  * + latest version of GLSL2 (Cody)

# Upcoming Milestones #

## Second Milestone ##

  * Pretty much all normal shader stuff works for version 1.3.
  * All three modes work:  simplification, optimization, and obfuscation

### Tasks Completed ###

  * Middle-end pass to coalesce multiple inserts into a single L-value swizzle (Michael, [r219](https://code.google.com/p/lunarglass/source/detail?r=219))
  * Output revision number, in linux at least (Michael, [r230](https://code.google.com/p/lunarglass/source/detail?r=230))
  * destructuring of swizzles in the Top IR into inserts/extracts (Michael, [r236](https://code.google.com/p/lunarglass/source/detail?r=236))
  * Command-line argument for handling of optimizations (Michael, [r240](https://code.google.com/p/lunarglass/source/detail?r=240))
  * Handle all the logical vs. bitwise operators in the back end `^, ^^, !, ~, not()` (JohnK, [r248](https://code.google.com/p/lunarglass/source/detail?r=248))
  * bitwise assignments e.g. `|=` (JohnK, [r248](https://code.google.com/p/lunarglass/source/detail?r=248))
  * refactor back-end into llvm pass so that analyses are available (Michael, [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
  * original variable names (JohnK, [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
  * control the output version of GLSL (JohnK, Michael [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
  * llvm and gla namespace independent place to put in shared utility functions (JohnK, [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
  * fix the the 00 vs. 000 linux/windows output differences (JohnK, [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
  * support for arrays of compile-time constant indexes for uniforms and inputs (JohnK, Cody, [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
  * fix problem of initializing constants (JohnK, [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
  * collection of bugs fixed for the website (Cody, Michael, JohnK, [r318](https://code.google.com/p/lunarglass/source/detail?r=318))
    * inline all functions, until we handle them
    * vectors of constants
    * others
  * loop detection and support of normal/typical loops (Michael [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * Catch flow control we can't handle in Bottom IR (Michael [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * Turn on more existing LLVM optimizations and examples (Michael [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * Lossy top-IR matrix conversion to vectors through GLSL2 (JohnK [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * transform LLVM CFG to structured flow control (Michael [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * structure support in Top IR (Cody [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * `gl_FragDepth`, `gl_FragData`, and fragment shader user-defined outputs (JohnK [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * `gl_Position` et. al., pipeline outputs, and attributes for vertex shader support (JohnK  [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * Simultaneously support structured flow control and optimizations LLVM doesn't leave flow control as structured, e.g., replace LLVM's simplify-CFG pass (Michael [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * support for arrays with variable indexes for uniforms and inputs (Cody [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * handle complex r-values and l-values (`like a[3].m[2][++z]`) (Cody  [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * loop support for nested loops and recognizing/handling inductive loops (Michael [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * emit ".0" for whole number floating point values; e.g., "2" -> "2.0" (JohnK [r465](https://code.google.com/p/lunarglass/source/detail?r=465))
  * structure printing (Cody [r465](https://code.google.com/p/lunarglass/source/detail?r=465))

### Things to do (who's currently working on it) in priority order ###

  * 1.3 texturing (Cody)
  * support for writable arrays (Cody)
  * fix the `[0] vs. [1]` linux/windows output differences
  * function calls; see FunctionCallPlan (Cody, JohnK)
  * Figure out how to keep matrices through the stack for re manifestation in LunarGOO output(JohnK)
  * single-use expression/copy propagation during code generation (Michael)

## Third Milestone ##

  * Post 1.3.
  * OpenGL ES
  * multiple compilation units in a stage
  * Cross-stage interactions
  * All non-goto flow control works

### Tasks Completed ###

### Things to do (who's currently working on it) in priority order ###

  * find/enhance a parser that can do glsl 3.x, possibly 4.x

---

  * track ES precisions through LLVM and print them back out
  * investigate cross-stage optimizations, obfuscations, simplifications