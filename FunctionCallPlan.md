Done:
  1. top: handle void functions (Cody, [r262](https://code.google.com/p/lunarglass/source/detail?r=262))
  1. bottom: handle generic function prototypes (JohnK, [r264](https://code.google.com/p/lunarglass/source/detail?r=264))
  1. top: handle a list input parameters with an output, no structures (Cody, [r265](https://code.google.com/p/lunarglass/source/detail?r=265))
  1. top: handle local variables, have to get to the bottom of this.  But, I'm sure all locals, once we know what they are, can be declared in the entry block to the function they are in. (JohnK, [r269](https://code.google.com/p/lunarglass/source/detail?r=269))

Todo:
  1. top: early returns in non-main functions
  1. bottom: finish any return support
  1. top: handle arbitrary returns/ins/outs, no structures at the GLSL level
  1. bottom: handle structures needed to do the above
  1. top: handle structures in general
  1. bottom: handle structures in general
  1. top: early returns in main that all must occur before copy out to the pipe (defer)
  1. top: handle name mangling for cross-compilation unit function lookup (defer for now)