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

# Proxy for XINPUT1_3.dll. iw4x.dll imports it, so placing it next to iw4x.exe
# gets us loaded before the game initialises. Without the MW2SS_* environment
# variables (set by the launcher) it is a transparent pass-through.
add_library(xinput1_3 SHARED
  src/proxy/dllmain.cpp
  src/proxy/config.cpp
  src/proxy/xinput_proxy.cpp
  src/proxy/window_hooks.cpp
  src/proxy/file_redirect.cpp
  src/proxy/d3d_hooks.cpp
  src/proxy/xinput1_3.def)
target_link_libraries(xinput1_3 PRIVATE minhook user32)
target_compile_definitions(xinput1_3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(xinput1_3 PRIVATE /W4 /wd4100)
