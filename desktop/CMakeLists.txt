cmake_minimum_required(VERSION 3.18)
project(oblivion_desktop C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LB_GEN_JOBS 3 CACHE STRING "parallel compiles of recompiled game code")
set_property(GLOBAL APPEND PROPERTY JOB_POOLS gen=${LB_GEN_JOBS})

# Use the repository's SDL build so its Cocoa + Vulkan surface support matches
# the DXVK backend used by Android. macOS Vulkan is provided by MoltenVK.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../android/app/jni/SDL2 SDL2)

if(APPLE)
  find_package(Vulkan REQUIRED)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../android/app/jni/dxvk dxvk)

get_filename_component(PORT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
file(GLOB GEN_SRC ${PORT}/gen/*.c)
file(GLOB RT_SRC ${PORT}/runtime/*.c)
file(GLOB RT_CXX ${PORT}/runtime/*.cpp)
if(NOT GEN_SRC)
  message(FATAL_ERROR "No generated code in ${PORT}/gen. Run desktop/build.sh with the game data directory.")
endif()

add_library(lbgen OBJECT ${GEN_SRC})
set_property(TARGET lbgen PROPERTY JOB_POOL_COMPILE gen)
target_include_directories(lbgen PRIVATE ${PORT}/runtime ${PORT}/gen)
target_compile_definitions(lbgen PRIVATE GAME_OBLIVION)
target_compile_options(lbgen PRIVATE -O2 -g0 -fno-strict-aliasing -fwrapv -fsigned-char -w -fPIC)

add_executable(lb ${RT_SRC} ${RT_CXX} $<TARGET_OBJECTS:lbgen>)
target_include_directories(lb PRIVATE
    ${PORT}/runtime ${PORT}/gen
    ${PORT}/android/app/jni/sdlinc
    ${PORT}/android/app/jni/dxvk/include/native/directx
    ${PORT}/android/app/jni/dxvk/include/native/windows
    ${PORT}/android/app/jni/dxvk/include/native)
target_compile_definitions(lb PRIVATE GAME_OBLIVION DXVK_NATIVE)
target_compile_options(lb PRIVATE -O2 -fno-strict-aliasing -fwrapv -fsigned-char -Wno-everything
    "$<$<COMPILE_LANGUAGE:C>:-std=gnu11>")
target_link_libraries(lb PRIVATE SDL2 dxvk_d3d9 m)

set_target_properties(lb PROPERTIES BUILD_RPATH_USE_ORIGIN TRUE)
if(APPLE)
  set_property(TARGET lb APPEND PROPERTY BUILD_RPATH "@executable_path/dxvk")
else()
  set_property(TARGET lb APPEND PROPERTY BUILD_RPATH "$ORIGIN/dxvk")
endif()
