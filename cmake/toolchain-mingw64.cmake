# Cross-compile JazzGPT for 64-bit Windows from Linux with MinGW-w64.
# JUCE 8 dropped MinGW support, so pair this with -DJAZZGPT_JUCE_TAG=7.0.12:
#
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#         -DJAZZGPT_JUCE_TAG=7.0.12 -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-win --target JazzGPT_VST3

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Case-fix wrappers (e.g. Windows.h -> windows.h) for case-sensitive hosts.
set(CMAKE_C_FLAGS_INIT   "-isystem ${CMAKE_CURRENT_LIST_DIR}/mingw-compat")
set(CMAKE_CXX_FLAGS_INIT "-isystem ${CMAKE_CURRENT_LIST_DIR}/mingw-compat")

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
