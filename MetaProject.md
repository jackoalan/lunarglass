# Introduction #

Use this page to discuss how the project itself is administered.

# Details #

**Desired Features**
  * Continuous integration system (regression testing on commit, ...)
    1. Set up TeamCity on a server
    1. Use google's post-commit hooks to send data to the TeamCity server
    1. Have a TeamCity agent run tests, email developers, etc.
  * Finer-tuned emailing on commit
    * Handle such emailing through TeamCity
  * Robust build system (simpler instructions, LunarGLASS build tools re-located to the LunarGLASS directory, ...)
    1. Separate out LunarGLASS code from LLVM
    1. Separate out LunarGLASS code from Mesa
    1. Build tool than handles fetching, applying LunarGLASS changes, and building