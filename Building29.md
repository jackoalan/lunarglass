**Note:** This is deprecated; the preferred way to build is using the glslang front end and LLVM 3.2.  See [Building](Building.md).

This project is based on a copy of mesa 7.10.1 from freedesktop.org and LLVM 2.9 from llvm.org.  It makes a "stand alone" compiler stack that outputs text, not integrated with a driver.

## Build instructions for Linux ##

### Automatic instructions ###
```
mkdir LunarGLASS  # or whatever you want to call the directory
cd LunarGLASS
svn checkout https://lunarglass.googlecode.com/svn/trunk/ .
./install
```

That should fetch all the needed code (including Mesa and LLVM), build the project, and put the binary in ./bin.

The install tool assumes you have bash, libffi-dev, flex, bison, git, subversion, and wget installed, in addition to the standard build tools (found in build-essential on Ubuntu).

To test it out, try
```
./tools/LunarGLASS ./test/test.frag
```

### Manual instructions ###

#### Get the Code ####

First, fetch the code following the method presented in [Getting the Code](Building#Getting_the_Code.md)

#### Building ####

  1. Install build dependencies
```
sudo apt-get install build-essential libffi-dev flex bison
```
  1. Build LLVM 2.9 (with LunarGLASS modifications)
```
mkdir Core/LLVM/llvm-2.9/build
cd Core/LLVM/llvm-2.9/build
../configure
make
make install DESTDIR=`pwd`/install
```
  1. Build Mesa 7.10.1 (with LunarGLASS modifications)
```
cd Standalone
make
```
  1. You can execute the compiler on a test in the test directory by
```
./StandAlone ../test/test.frag
```



## Build instructions for Windows ##

It is a bit cumbersome, so far done just for proof of concept purposes.  More streamlined setup and instructions will be forthcoming.

Note: This will overwrite some mesa files, so you will want to do this in a separate location than other mesa development.

### Get the Code ###

First, fetch the code following the method presented in [Getting the Code](Building#Getting_the_Code.md)

### Building ###

#### On Visual Studio 2010 ####

For LLVM, create a visual studio solution that creates all the libs and headers:

  1. Download and install CMake for Windows.  We used version 2.8.3 at http://www.cmake.org/cmake/resources/software.html.
  1. Run CMake as Administrator
  1. Put the path to your LunarGLASS\Core\LLVM\llvm-2.9 location in "Where is the source code:" and add "\build" to it (LunarGLASS\Core\LLVM\llvm-2.9\build) in "Where to build the binaries:"
  1. Press "Configure" button in CMake and say yes to create the build directory.
  1. Select Visual Studio 10 and "Use default native compilers" and "Finish".
  1. Change CMAKE\_INSTALL\_PREFIX to "install" (no path).
  1. Press "Configure" again.
  1. Press "Generate" button in CMake.
  1. Open new LLVM.sln just created in llvm-2.9\build and build the INSTALL project (not the solution, just the INSTALL project for "Debug Win32").  This step takes a while...

For LunarGLASS

  * Open "LunarGLASS/Standalone/StandAlone.sln" and build the "standalone" project.

In the directory "LunarGLASS/test" is a run script that shows how to execute the compiler on a test file.


## Getting the Code ##

Overview:  Mesa and LLVM will be sibling tree structures; both children of your LunarGLASS directory.  They will then be overlaid by LunarGLASS files.

1. Make a directory (e.g., "LunarGLASS", which will be the name used in these instructions) where you want this project to live.  It will correspond to the svn/trunk/ directory in the google SVN repository for LunarGLASS.

2. To get mesa, make a directory "mesa" under "LunarGLASS".  Then get [Mesa 7.10.1](ftp://freedesktop.org/pub/mesa/7.10.1/MesaLib-7.10.1.tar.bz2).  Extract to the LunarGLASS/mesa directory.

Example:
```
cd LunarGLASS
wget ftp://freedesktop.org/pub/mesa/7.10.1/MesaLib-7.10.1.tar.bz2
tar -xjf MesaLib-7.10.1.tar.bz2
mv Mesa-7.10.1 mesa
rm MesaLib-7.10.1*
```

Verify you are left with the directory "LunarGLASS/mesa/src" et. al.

3. To get LLVM, make a directory "LLVM" under "LunarGLASS". Download [LLVM 2.9](http://llvm.org/releases/2.9/llvm-2.9.tgz) and extract it into your "LunarGLASS/Core/LLVM" directory.  Verify this left LLVM in the directory "LunarGLASS/Core/LLVM/llvm-2.9".

Example:
```
cd LunarGLASS
mkdir -p Core/LLVM
cd Core/LLVM
wget http://llvm.org/releases/2.9/llvm-2.9.tgz
tar -zxf llvm-2.9.tgz
rm llvm-2.9.tgz
```

4. To get LunarGLASS, you will need SVN commands or an SVN client like Tortoise.  Use your SVN client to check out from google's code site //lunarglass.googlecode.com/svn/trunk to your directory "LunarGLASS".

This got most the files, but did not yet replace files that need to be replace.  To do that, you must revert all files at the LunarGLASS level.  In Tortoise, right click LunarGLASS|Tortoise SVN|Revert...  Select all files and revert.

If you are using SVN from the command line, these steps look like:
```
# cd to the LunarGLASS directory you made...
cd LunarGLASS
svn checkout --force https://lunarglass.googlecode.com/svn/trunk/ .
svn revert --depth=infinity .
```