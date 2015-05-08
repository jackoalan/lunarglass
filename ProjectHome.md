## Goals ##

The primary goals of the LunarGLASS project are:

  * **Reduce** the developement burden of creating advanced shader compiler stacks.

  * **Increase** the level of optimization achieved by real world applications.

and do all this in a robust and modular approach that works well across a diverse set of hardware architectures.

## Description ##

LunarGLASS is an LLVM-based shader-compiler stack available to open-source developers.  It brings a new approach by splitting the common shared intermediate representation (IR) into two levels; the top level is completely platform independent while the bottom level is dynamically tailorable to different families of architecture.  Both levels still lend themselves to portability and sharing of tools.  Together, they solve the problem of having a standard portable IR without being biased toward a specific class of target architecture.

LunarGLASS is a long-term compiler stack architecture, based on establishing common intermediate representations (IRs) allowing modularity between stack layers. Each source-language front end would benefit from a common set of high- and mid-level optimizations, as would each back end, without the need to invent additional IRs. The short-term goal is to leverage investments in existing IRs while the long-term goal is to reduce the number of IRs and not require optimization difficulties caused by losing information going through an IR.

## Build Instructions ##

For build instructions, see [Building](Building.md)

## Licensing ##

See the [LunarGLASS FAQ](http://www.lunarglass.org/faq).
## Channels of Communication ##

http://groups.google.com/group/lunarglass-devel is the discussion group for anyone to discuss any LunarGLASS topics.

http://code.google.com/p/lunarglass/ is accessible by anyone, and modifiable by those given access.  It has the project source code, wiki, and issues.

http://www.lunarglass.org is read-only, maintained by LunarG.