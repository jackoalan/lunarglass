# List of possible uses #
  * obfuscation for apps that ship shader source (very easy, so high ROI, even if not a big use)
  * optimization for embedded platforms that have wimpy compilers
  * problem detection for WebGL (big thing now that it's announced and this is the center of controversy)
  * translation from very lax-all-inclusive form of GLSL to a form that runs everywhere and conforms to the specification
  * support of extra features like #include
  * translating HLSL to GLSL (harder/different problem than much of the above)
  * simplifying (especially for uber-shaders) to make a shader more readable for specific settings of switches
  * an analysis of where there might be portability issues across platforms, in the areas of
    * performance
    * compilation/linking
    * quality
  * cross-shader versions of all of these
  * cross-stage versions of all of these
  * cross-program versions of all of these