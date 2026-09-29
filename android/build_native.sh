#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Drives the complete native build of the shadPS4 Android port:
#
#   1. locates the Android SDK / NDK,
#   2. builds a host `protoc` from the vendored protobuf (+ abseil),
#   3. cross-compiles FFmpeg 7.1.1 for arm64-v8a (static, hermetic),
#   4. configures and builds the emulator with the NDK CMake toolchain
#      (libSDL3.so + libmain.so),
#   5. stages the shared libraries into android/app/src/main/jniLibs.
#
# Every stage is individually skippable and cached, see the environment
# variables at the top of the file. After this script succeeds, run
# ./gradlew assembleRelease (or assembleDebug) inside android/ to produce
# the APK.
#
# Usage:
#   bash android/build_native.sh [Release|Debug|RelWithDebInfo]

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration (environment overrides)
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

: "${SHADPS4_ANDROID_SDK:=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-auto}}}"
: "${SHADPS4_ANDROID_NDK:=auto}"
: "${SHADPS4_NDK_VERSION:=28.1.13356709}"
: "${SHADPS4_ABI:=arm64-v8a}"
: "${SHADPS4_PLATFORM:=android-29}"
: "${FFMPEG_VERSION:=7.1.1}"
: "${FFMPEG_SRC_DIR:=}"                  # optional pre-extracted FFmpeg source
: "${SHADPS4_SKIP_HOSTPROTOC:=0}"
: "${GLSLANG_HOST_BIN:=}"               # set by build_host_protoc; may be pre-set when skipping
: "${SHADPS4_SKIP_FFMPEG:=0}"
: "${SHADPS4_SKIP_CMAKE:=0}"
: "${SHADPS4_SKIP_JNILIBS:=0}"

BUILD_TYPE="${1:-Release}"
BUILD_DIR="${ROOT_DIR}/android/build"
FFMPEG_PREFIX="${BUILD_DIR}/ffmpeg-install"
JNILIBS_DIR="${ROOT_DIR}/android/app/src/main/jniLibs/${SHADPS4_ABI}"

JOBS="${SHADPS4_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

log()   { printf '\033[1;32m[build_native]\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m[build_native]\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31m[build_native] FATAL:\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Locate SDK / NDK
# ---------------------------------------------------------------------------

detect_sdk() {
    local candidate
    local -a candidates=(
        "${SHADPS4_ANDROID_SDK}"
        "${ANDROID_HOME:-}"
        "${ANDROID_SDK_ROOT:-}"
        "$HOME/Android/Sdk"
        "/usr/local/lib/android/sdk"          # GitHub Actions ubuntu runners
        "/opt/android-sdk"
    )
    for candidate in "${candidates[@]}"; do
        if [[ -n "${candidate}" && "${candidate}" != "auto" &&
              -d "${candidate}/platform-tools" ]]; then
            SHADPS4_ANDROID_SDK="${candidate}"
            return 0
        fi
    done
    die "Android SDK not found. Set SHADPS4_ANDROID_SDK=/path/to/sdk (must contain platform-tools/)."
}

detect_ndk() {
    if [[ -n "${SHADPS4_ANDROID_NDK}" && "${SHADPS4_ANDROID_NDK}" != "auto" &&
          -d "${SHADPS4_ANDROID_NDK}" ]]; then
        return 0
    fi
    local base="${SHADPS4_ANDROID_SDK}/ndk"
    [[ -d "${base}" ]] || die "No NDK found under ${base}. Install NDK ${SHADPS4_NDK_VERSION} via sdkmanager."
    if [[ -d "${base}/${SHADPS4_NDK_VERSION}" ]]; then
        SHADPS4_ANDROID_NDK="${base}/${SHADPS4_NDK_VERSION}"
        return 0
    fi
    # Fall back to the newest installed NDK.
    local newest
    newest="$(ls -1 "${base}" 2>/dev/null | sort -V | tail -n 1 || true)"
    [[ -n "${newest}" ]] || die "No NDK revisions installed under ${base}."
    warn "NDK ${SHADPS4_NDK_VERSION} not found, falling back to ${newest}."
    SHADPS4_ANDROID_NDK="${base}/${newest}"
}

detect_host_tag() {
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64)  ANDROID_HOST_TAG="linux-x86_64" ;;
        Linux-aarch64) ANDROID_HOST_TAG="linux-arm64" ;;
        Darwin-*)      ANDROID_HOST_TAG="darwin-x86_64" ;;
        *) die "Unsupported host OS for the NDK prebuilt toolchain." ;;
    esac
}

[[ "${SHADPS4_ANDROID_SDK}" != "auto" ]] || detect_sdk
detect_ndk
detect_host_tag

NDK_TOOLCHAIN="${SHADPS4_ANDROID_NDK}/build/cmake/android.toolchain.cmake"
[[ -f "${NDK_TOOLCHAIN}" ]] || die "NDK toolchain file missing: ${NDK_TOOLCHAIN}"

log "SDK : ${SHADPS4_ANDROID_SDK}"
log "NDK : ${SHADPS4_ANDROID_NDK}"
log "ABI : ${SHADPS4_ABI}  platform: ${SHADPS4_PLATFORM}  type: ${BUILD_TYPE}"

# ---------------------------------------------------------------------------
# 2. Host protoc (vendored protobuf + abseil)
# ---------------------------------------------------------------------------

PROTOC_BIN="${BUILD_DIR}/host-protoc/protoc"

build_host_protoc() {
    if [[ "${SHADPS4_SKIP_HOSTPROTOC}" == "1" ]]; then
        log "Skipping host protoc build (SHADPS4_SKIP_HOSTPROTOC=1)."
        [[ -x "${PROTOC_BIN}" ]] || die "protoc missing at ${PROTOC_BIN} but the build was skipped."
        return 0
    fi
    log "Building host tools (protoc + font embedder + glslang)..."
    cmake -G Ninja \
        -S "${SCRIPT_DIR}/cmake" \
        -B "${BUILD_DIR}/host-protoc" \
        -DHOST_PROTOC_ROOT="${ROOT_DIR}" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_DIR}/host-protoc" --target protoc Dear_ImGui_FontEmbed glslang-standalone -j"${JOBS}"

    [[ -x "${PROTOC_BIN}" ]] || die "protoc was not produced at ${PROTOC_BIN}."
    log "Host protoc OK: ${PROTOC_BIN}"
    [[ -x "${BUILD_DIR}/host-protoc/Dear_ImGui_FontEmbed" ]] \
        || die "Dear_ImGui_FontEmbed was not produced."
    log "Host font embed tool OK: ${BUILD_DIR}/host-protoc/Dear_ImGui_FontEmbed"
    GLSLANG_HOST_BIN=$(find "${BUILD_DIR}/host-protoc" -type f -name 'glslang*' -perm -u+x \
        | grep -vE '\.(so|a)$' | head -n 1 || true)
    [[ -n "${GLSLANG_HOST_BIN}" ]] || die "glslang standalone compiler was not produced."
    log "Host glslang compiler OK: ${GLSLANG_HOST_BIN}"
}

# ---------------------------------------------------------------------------
# 3. FFmpeg cross-compile (static, arm64-v8a)
# ---------------------------------------------------------------------------

# The four decoders the emulator actually asks avcodec for.
readonly FFMPEG_REQUIRED_LIBS=(libavformat.a libavcodec.a libswscale.a libavutil.a libavfilter.a libswresample.a)

ffmpeg_needs_build() {
    local lib
    for lib in "${FFMPEG_REQUIRED_LIBS[@]}"; do
        [[ -f "${FFMPEG_PREFIX}/lib/${lib}" ]] || return 0
    done
    return 1
}

fetch_ffmpeg_sources() {
    if [[ -n "${FFMPEG_SRC_DIR}" && -f "${FFMPEG_SRC_DIR}/configure" ]]; then
        FFMPEG_BUILD_SRC="${FFMPEG_SRC_DIR}"
        return 0
    fi
    local src="${BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}"
    if [[ ! -f "${src}/configure" ]]; then
        local tarball="${BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}.tar.xz"
        mkdir -p "${BUILD_DIR}"
        if [[ ! -f "${tarball}" ]]; then
            log "Downloading FFmpeg ${FFMPEG_VERSION}..."
            curl -fL --retry 3 -o "${tarball}" \
                "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
        fi
        log "Extracting FFmpeg..."
        rm -rf "${src}"
        tar -xJf "${tarball}" -C "${BUILD_DIR}"
    fi
    FFMPEG_BUILD_SRC="${src}"
}

build_ffmpeg() {
    if [[ "${SHADPS4_SKIP_FFMPEG}" == "1" ]]; then
        log "Skipping FFmpeg build (SHADPS4_SKIP_FFMPEG=1)."
        ffmpeg_needs_build || die "FFmpeg prefix incomplete at ${FFMPEG_PREFIX} but the build was skipped."
        return 0
    fi
    if ! ffmpeg_needs_build; then
        log "FFmpeg already installed at ${FFMPEG_PREFIX}."
        return 0
    fi

    fetch_ffmpeg_sources
    log "Cross-compiling FFmpeg ${FFMPEG_VERSION} for ${SHADPS4_ABI}..."

    local host_prebuilt="${SHADPS4_ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}"
    # Use the NDK's API-level wrapper compilers (aarch64-linux-android29-clang).
    # Bare `clang` carries no --target and defaults to the host (x86_64) triple,
    # so every configure probe fails: `-march=armv8-a` is not a valid x86 CPU
    # and the Android sysroot has no crt objects for the host triple. The
    # wrapper embeds the right --target and --sysroot for us.
    local api="${SHADPS4_PLATFORM#android-}"
    local target="aarch64-linux-android${api}"
    local cc="${host_prebuilt}/bin/${target}-clang"
    local cxx="${host_prebuilt}/bin/${target}-clang++"
    [[ -x "${cc}" && -x "${cxx}" ]] \
        || die "NDK API-level wrapper compiler not found: ${cc}"

    # Trim the vcpkg-style feature set used by the desktop builds down to a
    # hermetic, fully-internal configuration: every bundled decoder stays on
    # (h264/hevc/aac/mp3 included), all external dependencies are off.
    #
    # --disable-asm is mandatory on Android/arm64:
    # FFmpeg's NEON FFT asm (libavutil/aarch64/tx_float_neon.S) uses
    # `adrp + add` to reference the global `ff_tx_tab_*_float` tables defined
    # in tx_float.o. FFmpeg marks those tables with *default* visibility
    # (no -fvisibility=hidden, no version-script export gate because we
    # build a static lib). When ld.lld links libavutil.a into libmain.so,
    # it rejects ADRP against a default-visibility cross-object symbol with
    # "relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against symbol
    # 'ff_tx_tab_32_float'; recompile with -fPIC".
    # The C fallback (tx_template.c compiled without asm) is plenty fast
    # for shadPS4's media-decode workload; the NEON path is only a
    # micro-optimization that's incompatible with the static-lib -> .so
    # linking model.
    local common_flags=(
        --prefix="${FFMPEG_PREFIX}"
        --enable-cross-compile
        --target-os=android
        --arch=aarch64
        --cpu=armv8-a
        --cc="${cc}"
        --cxx="${cxx}"
        --ar="${host_prebuilt}/bin/llvm-ar"
        --nm="${host_prebuilt}/bin/llvm-nm"
        --ranlib="${host_prebuilt}/bin/llvm-ranlib"
        --strip="${host_prebuilt}/bin/llvm-strip"
        --sysroot="${host_prebuilt}/sysroot"
        --extra-cflags="-O2 -fPIC"
        --enable-static
        --disable-shared
        --enable-pic
        --disable-asm
        --disable-programs
        --disable-doc
        --disable-avdevice
        --disable-network
        --disable-autodetect
        --disable-debug
    )

    local tier_a_flags=(
        "${common_flags[@]}"
    )

    # Minimal fallback in case the full internal feature set trips over an
    # assembler/toolchain hiccup: keep exactly the demuxers, parsers and
    # decoders the emulator uses (AV_CODEC_ID_H264/HEVC/AAC/MP3).
    local tier_b_flags=(
        "${common_flags[@]}"
        --disable-everything
        --enable-protocol=file
        --enable-protocol=pipe
        --enable-demuxer=mov
        --enable-demuxer=mpegts
        --enable-demuxer=mp3
        --enable-demuxer=aac
        --enable-parser=h264
        --enable-parser=hevc
        --enable-parser=aac
        --enable-parser=mp3
        --enable-decoder=h264
        --enable-decoder=hevc
        --enable-decoder=aac
        --enable-decoder=mp3
        --enable-swscale
        --enable-swresample
        --enable-avfilter
        --enable-filter=aresample
        --enable-filter=scale
    )

    local build_dir="${BUILD_DIR}/ffmpeg-build"
    rm -rf "${build_dir}"
    mkdir -p "${build_dir}"

    (
        cd "${build_dir}"
        if "${FFMPEG_BUILD_SRC}/configure" "${tier_a_flags[@]}"; then
            log "FFmpeg configure (full internal set) OK."
        else
            warn "FFmpeg configure failed, retrying with the minimal feature set..."
            rm -rf ./* ./.[!.]*
            "${FFMPEG_BUILD_SRC}/configure" "${tier_b_flags[@]}"
            log "FFmpeg configure (minimal set) OK."
        fi
        make -j"${JOBS}"
        make install
    )

    local lib
    for lib in "${FFMPEG_REQUIRED_LIBS[@]}"; do
        [[ -f "${FFMPEG_PREFIX}/lib/${lib}" ]] || die "FFmpeg install missing ${lib}."
    done
    log "FFmpeg OK: ${FFMPEG_PREFIX}"
}

# ---------------------------------------------------------------------------
# 4. Main CMake build (libSDL3.so + libmain.so)
# ---------------------------------------------------------------------------

run_cmake() {
    if [[ "${SHADPS4_SKIP_CMAKE}" == "1" ]]; then
        log "Skipping CMake build (SHADPS4_SKIP_CMAKE=1)."
        [[ -f "${BUILD_DIR}/cmake-out/libmain.so" ]] || die "libmain.so missing but the CMake build was skipped."
        return 0
    fi

    log "Configuring shadPS4 for Android (${BUILD_TYPE})..."
    # The NDK's libc++ hides std::jthread/stop_token behind its
    # "experimental" gate even though the implementation (and the prebuilt
    # libc++ runtime) is complete. Re-enable it for the emulator sources,
    # which rely on cooperative cancellation.
    cmake -G Ninja \
        -S "${ROOT_DIR}" \
        -B "${BUILD_DIR}/cmake-out" \
        -DCMAKE_TOOLCHAIN_FILE="${NDK_TOOLCHAIN}" \
        -DANDROID_ABI="${SHADPS4_ABI}" \
        -DANDROID_PLATFORM="${SHADPS4_PLATFORM}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_CXX_FLAGS="-D_LIBCPP_ENABLE_EXPERIMENTAL" \
        -DPROTOC_HOST_EXECUTABLE="${PROTOC_BIN}" \
        -DHOST_FONT_EMBED_EXECUTABLE="${BUILD_DIR}/host-protoc/Dear_ImGui_FontEmbed" \
        -DHOST_GLSLANG_EXECUTABLE="${GLSLANG_HOST_BIN}" \
        -DFFMPEG_ANDROID_PREFIX="${FFMPEG_PREFIX}"

    log "Building (this compiles the emulator core, SDL3 and all externals)..."
    cmake --build "${BUILD_DIR}/cmake-out" -j"${JOBS}"

    [[ -f "${BUILD_DIR}/cmake-out/libmain.so" ]] || die "libmain.so was not produced."
    [[ -f "${BUILD_DIR}/cmake-out/libSDL3.so" ]] || die "libSDL3.so was not produced."
    log "Native build OK: libmain.so + libSDL3.so"
}

# ---------------------------------------------------------------------------
# 5. Stage jniLibs
# ---------------------------------------------------------------------------

stage_jnilibs() {
    if [[ "${SHADPS4_SKIP_JNILIBS}" == "1" ]]; then
        log "Skipping jniLibs staging (SHADPS4_SKIP_JNILIBS=1)."
        return 0
    fi
    log "Staging shared libraries into ${JNILIBS_DIR}..."
    mkdir -p "${JNILIBS_DIR}"
    cp -f "${BUILD_DIR}/cmake-out/libmain.so" "${JNILIBS_DIR}/libmain.so"
    cp -f "${BUILD_DIR}/cmake-out/libSDL3.so" "${JNILIBS_DIR}/libSDL3.so"
    # 16 KB page alignment sanity (informational).
    if command -v llvm-readelf >/dev/null 2>&1; then
        llvm-readelf -l "${JNILIBS_DIR}/libmain.so" 2>/dev/null | grep -q "0x4000" \
            && log "16 KB page alignment: OK" \
            || warn "libmain.so LOAD alignment is not 16 KB."
    fi
    log "jniLibs staged."
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

mkdir -p "${BUILD_DIR}"

build_host_protoc
build_ffmpeg
run_cmake
stage_jnilibs

log "Done. Build the APK with:"
log "    cd android && ./gradlew assemble${BUILD_TYPE^}"
