The following are for building with LLVM 3.4 and the glslang front end.
It makes a standalone compiler stack, not integrated with a driver, that reads shader files and outputs textual translations.

## Getting the Code ##

Overview:  The glslang front end and LunarGLASS are two separate projects from two SVN servers.  They will be sibling tree structures, which you will put in a location of your choice, i.e.,

```
    PathOfYourChoice/glslang
    PathOfYourChoice/LunarGLASS
```

  1. Make these two directories (above). They will map to the SVN roots of the two projects.
  1. To get glslang
```
cd glslang
svn checkout https://cvs.khronos.org/svn/repos/ogl/trunk/ecosystem/public/sdk/tools/glslang .
```
  1. To get LLVM, download LLVM 3.4 source code from [LLVM 3.4](http://llvm.org/releases/download.html#3.4) and extract it into your "LunarGLASS/Core/LLVM" directory, leaving it in the directory "LunarGLASS/Core/LLVM/llvm-3.4".  For example:
```
cd LunarGLASS
mkdir -p Core/LLVM
cd Core/LLVM
wget http://llvm.org/releases/3.4/llvm-3.4.src.tar.gz
tar --gzip -xf llvm-3.4.src.tar.gz
```
  1. To get LunarGLASS, you will need SVN commands or an SVN client like Tortoise.  Use your SVN client to check out from google's code site //lunarglass.googlecode.com/svn/trunk to your directory "LunarGLASS". This got most the files, but did not yet replace files that need to be replace.  To do that, you must revert all files at the LunarGLASS level.  In Tortoise, right click LunarGLASS|Tortoise SVN|Revert...  Select all files and revert. If you are using SVN from the command line, these steps look like:
```
# cd to the LunarGLASS directory you made...
cd LunarGLASS
svn checkout --force https://lunarglass.googlecode.com/svn/trunk/ .
svn revert --depth=infinity .
```

## Build instructions for Linux ##

Summary:  LLVM uses a configure script while glslang and LunarGLASS use CMake. All three locate the build in a "build" subdirectory and the final results in their respective build/install.

You need bison to build glslang, and bison version 2.7 is the fully tested version, though other versions are likely to work as well.

  1. Install build dependencies
```
sudo apt-get install build-essential libffi-dev flex bison
```
  1. Build glslang: Change to your glslang directory, set up CMake, and build:
```
cd glslang
mkdir build
cd build
cmake ..
cmake .. # run twice to get install directory correctly set
make
make install
```
> > This leaves the command-line executable in the build/install/bin/glslangValidator.exe, and libraries for LunarGLASS in build/install/lib.  This procedure is also captured in the script
```
cd glslang
./SetupLinux.sh
```
> > which will also run a quick test to emit textual AST to verify it worked.
  1. Build LLVM 3.4 (with LunarGLASS modifications)
```
# first time only
cd LunarGLASS/Core/LLVM/llvm-3.4
mkdir build
cd build
../configure
```
```
# build or rebuild
cd LunarGLASS/Core/LLVM/llvm-3.4/build
make -j 8
make install DESTDIR=`pwd`/install
```
  1. Build LunarGOO: Change to your directory for LunarGLASS, set up CMake, and build
```
cd LunarGLASS
mkdir build
cd build
cmake ..
cmake .. # run twice to get install directory correctly set
make
make install
```
> > This leaves the command-line executable in build/install/bin/LunarGOO.exe.  This procedure is also captured in the script
```
cd LunarGLASS
./SetupLinux.sh
```
> > which will also run a quick test to emit GLSL to verify it worked (translated GLSL to GLSL).

## Build instructions for Windows ##

Summary:  LLVM, glslang, and LunarGLASS all use CMake to generate Visual Studio solutions, with the build located in a "build" subdirectory (for each of the three) and the final results in build/install.

For all three components:

  1. Install version 2.7x of Python (version 3 causes a CMake error).  Get it from http://www.python.org/download/releases/2.7.6/.
  1. Use CMake 2.8.  Get CMake at http://www.cmake.org/cmake/resources/software.html.

To build LLVM:

Get it from http://www.python.org/download/releases/2.7.6/.
  1. Run CMake.
  1. Put the full path to your LunarGLASS/Core/LLVM/llvm-3.4 location in "Where is the source code:" and add "/build" to it (LunarGLASS/Core/LLVM/llvm-3.4/build) in "Where to build the binaries:"
  1. Press "Configure" button in CMake and say yes to create the build directory.
  1. Select your Visual Studio and "Use default native compilers" and "Finish".
  1. Change CMAKE\_INSTALL\_PREFIX to "install" (no path).
  1. Press "Configure" again.
  1. Press "Generate" button in CMake.
  1. Open the LLVM.sln just created in llvm-3.4/build and build the INSTALL project (not the whole solution, just the INSTALL project, for "Debug Win32").  This step takes a while.

For glslang:

  1. Run CMake.
  1. Similarly for LLVM, run the source from your glslang directory, add "/build" for the binaries, and "install" for the install prefix, though that latter should happen automatically.
  1. After pressing "Configure", press "Generate" until the red goes away.
  1. Open the glslang.sln just created in "build" and build the INSTALL project.

This leaves the command-line executable in build/install/bin/glslangValidator.exe, and libraries for LunarGLASS in
build/install/lib.

For LunarGLASS:

  1. Run CMake.
  1. Similarly for the others, run with source from your LunarGLASS directory, add "/build" for the binaries, and "install" for the install prefix, though that latter should happen automatically.
  1. After pressing "Configure", press "Generate" until the red goes away.
  1. Open the LunarGLASS.sln just created in "build" and build the INSTALL project.

This leaves the command-line executable in build/install/bin/LunarGOO.exe.

Here is an example of running the full stack:
```
cd LunarGLASS/test
../build/install/bin/LunarGOO test.frag
```

There is also a "runtests" script that runs the stack against a set of tests.