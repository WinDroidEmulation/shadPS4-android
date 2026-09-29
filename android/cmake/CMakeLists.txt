# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Host protoc wrapper: builds a host `protoc` from the in-tree vendored
# protobuf (with abseil), mirroring the way externals/CMakeLists.txt wires the
# two projects together. The resulting binary is passed to the Android CMake
# configure step via PROTOC_HOST_EXECUTABLE so shadnet protobuf sources can be
# generated while cross-compiling.
#
# Expected invocation (from android/build_native.sh):
#   cmake -G Ninja -DHOST_PROTOC_ROOT=/path/to/shadPS4 \
#     -S /path/to/shadPS4/android/cmake -B <build-dir>
#   cmake --build <build-dir> --target protoc

cmake_minimum_required(VERSION 3.25)
project(host-protoc LANGUAGES C CXX)

if(NOT DEFINED HOST_PROTOC_ROOT)
    message(FATAL_ERROR "HOST_PROTOC_ROOT must point at the shadPS4 repository root")
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

# Land executables (protoc) at the top of the build directory where
# android/build_native.sh expects them: <build>/host-protoc/protoc.
# (Protobuf only overrides this for MSVC + shared builds, never here.)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# abseil first so that protobuf's find_package(absl) short-circuits on the
# already-defined absl::strings target.
#
# Both source directories live OUTSIDE android/cmake (up at the repo root),
# so add_subdirectory() cannot derive a binary directory on its own - an
# explicit binary directory must be given as the second argument.
set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory("${HOST_PROTOC_ROOT}/externals/abseil-cpp"
                 "${CMAKE_CURRENT_BINARY_DIR}/abseil-cpp"
                 EXCLUDE_FROM_ALL SYSTEM)

set(protobuf_BUILD_TESTS OFF)
set(protobuf_BUILD_EXAMPLES OFF)
set(protobuf_BUILD_PROTOC_BINARIES ON)
set(protobuf_BUILD_LIBPROTOC ON)
set(protobuf_BUILD_SHARED_LIBS OFF)
set(protobuf_WITH_ZLIB OFF)
set(protobuf_INSTALL OFF)
set(protobuf_LOCAL_DEPENDENCIES_ONLY ON)
set(protobuf_FORCE_FETCH_DEPENDENCIES OFF)
add_subdirectory("${HOST_PROTOC_ROOT}/externals/protobuf"
                 "${CMAKE_CURRENT_BINARY_DIR}/protobuf"
                 EXCLUDE_FROM_ALL SYSTEM)

# Host build of the ImGui font-embedding tool. The main NDK build executes
# Dear_ImGui_FontEmbed at build time to turn the bundled fonts into C++
# sources; a cross-compiled copy cannot run on the build host, so the Android
# build receives this native executable through HOST_FONT_EMBED_EXECUTABLE.
add_executable(Dear_ImGui_FontEmbed
               "${HOST_PROTOC_ROOT}/externals/imgui/misc/fonts/binary_to_compressed_c.cpp")

# Host build of the glslang standalone shader compiler. glslang disables its
# binaries when ANDROID is set, but the main build's host_shaders step still
# needs to run the compiler on the build host, so build it here natively and
# hand it over through HOST_GLSLANG_EXECUTABLE.
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
add_subdirectory("${HOST_PROTOC_ROOT}/externals/glslang"
                 "${CMAKE_CURRENT_BINARY_DIR}/glslang"
                 EXCLUDE_FROM_ALL)

# The top-level CMAKE_RUNTIME_OUTPUT_DIRECTORY would place the binary next to
# the "glslang" subdirectory (name collision), so give it its own folder.
set_target_properties(glslang-standalone PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host-tools")
