# SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

project(ImGui_Resources)

# Android port: when cross-compiling for arm64-v8a the host-built
# Dear_ImGui_FontEmbed is supplied through HOST_FONT_EMBED_EXECUTABLE
# (see android/build_native.sh + android/cmake/CMakeLists.txt). The
# target-built executable would be an ARM64 binary that cannot run on
# the x86_64 build host, so we use the host executable directly and
# skip building the target copy in that case.
if(CMAKE_CROSSCOMPILING AND HOST_FONT_EMBED_EXECUTABLE)
    if(NOT EXISTS "${HOST_FONT_EMBED_EXECUTABLE}")
        message(FATAL_ERROR
            "HOST_FONT_EMBED_EXECUTABLE is set but does not exist: "
            "${HOST_FONT_EMBED_EXECUTABLE}")
    endif()
    set(IMGUI_FONT_EMBED_TOOL "${HOST_FONT_EMBED_EXECUTABLE}")
    message(STATUS "Using host Dear_ImGui_FontEmbed: ${IMGUI_FONT_EMBED_TOOL}")
else()
    add_executable(Dear_ImGui_FontEmbed ${CMAKE_SOURCE_DIR}/externals/imgui/misc/fonts/binary_to_compressed_c.cpp)
    set(IMGUI_FONT_EMBED_TOOL $<TARGET_FILE:Dear_ImGui_FontEmbed>)
endif()

set(FONT_LIST
    NotoSansArabic-Regular.ttf
    NotoSansCJK-Regular.ttc
    NotoSans-Regular.ttf
    NotoSansSymbols2-Regular.ttf
    NotoSansThai-Regular.ttf
    ProggyVector-Regular.ttf
)

set(OutputList "")
FOREACH (FONT_FILE ${FONT_LIST})
    string(REGEX REPLACE "-" "_" fontname ${FONT_FILE})
    string(TOLOWER ${fontname} fontname)
    string(REGEX REPLACE "\\.(ttf|otf|ttc)$" "" fontname_cpp ${fontname})
    set(fontname_cpp "imgui_font_${fontname_cpp}")

    MESSAGE(STATUS "Embedding font ${FONT_FILE}")
    set(OUTPUT "generated_fonts/imgui_fonts/${fontname}")
    add_custom_command(
        OUTPUT "${OUTPUT}.g.cpp"
        COMMAND ${CMAKE_COMMAND} -E make_directory "generated_fonts/imgui_fonts"
        COMMAND ${IMGUI_FONT_EMBED_TOOL} -nostatic "${CMAKE_CURRENT_SOURCE_DIR}/fonts/${FONT_FILE}" ${fontname_cpp} > "${OUTPUT}.g.cpp"
        DEPENDS "${IMGUI_FONT_EMBED_TOOL}" "fonts/${FONT_FILE}"
        USES_TERMINAL
    )
    list(APPEND OutputList "${OUTPUT}.g.cpp")
ENDFOREACH ()

add_library(ImGui_Resources STATIC ${OutputList})
set(IMGUI_RESOURCES_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/generated_fonts PARENT_SCOPE)
