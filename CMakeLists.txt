cmake_minimum_required(VERSION 3.20)
project(mw2_splitscreen C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "MW2 (iw4x) is a 32-bit game: configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

# The mod is built as dsound.dll: iw4x.exe imports DSOUND.dll, so dropping it next
# to iw4x.exe loads us with the game. DirectSound calls are forwarded.
add_library(dsound SHARED
  src/mod/dllmain.cpp
  src/mod/util.cpp
  src/mod/splitscreen.cpp
  src/mod/loopback.cpp
  src/mod/render.cpp
  src/mod/dsound_proxy.cpp
  src/mod/dsound.def)
target_link_libraries(dsound PRIVATE minhook user32)
target_compile_definitions(dsound PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
# /Oy- keeps frame pointers: easier crash analysis inside the game.
target_compile_options(dsound PRIVATE /W4 /wd4100 /Oy- /Zi)
target_link_options(dsound PRIVATE /DEBUG /SAFESEH:NO)
