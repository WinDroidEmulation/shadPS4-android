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
