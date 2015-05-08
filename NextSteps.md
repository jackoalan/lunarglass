After the highest priority of getting feedback on architecture and design, here some possible directions to go in actual implementation.

There are several directions that could be gone somewhat independently, possibly all at the same time, or not.

## Direction 1: Mesa Driver ##
Get this working in an actual driver environment.  This would entail
  1. Integrating LunarGLASS into a mesa driver.
  1. Hooking up all the linkages for things like
```
  * constants/uniforms
  * texture units
  * interpolants
  * etc.
```
  1. Picking an application to try
  1. Fleshing out functionality wide enough to support the application (see "Direction 2")
  1. Test and measure performance.

## Direction 2: Wider Functionality ##
This can be done orthogonally WRT "Direction 1"
  1. Select some sample shaders (possibly the same ones needed be the application in "Direction 1")
  1. Flesh out the stack to support the functionality in these shaders:
```
  * GLSL2 -> Top IR
  * Bottom IR -> mesa IR
```

## Direction 3: Real Back-End ##
This can be done orthogonally WRT "Direction 1" and "Direction 2"
  1. Select a target architecture/back-end to target.  The rest of the steps assume this does not take mesa IR (or TGSI).
  1. Write a new back-end translating Bottom IR to the selected existing back-end, or
  1. Write a new back-end translating Bottom IR to the selected target.
```
  * could be either a traditional style back-end, or
  * use native LLVM lowering
```
  1. Do driver integration here as well.  Could be the same thing as "Direction 1" or something different if not Mesa based.