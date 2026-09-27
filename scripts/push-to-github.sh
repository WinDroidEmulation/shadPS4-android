#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# One-shot helper: turn this inlined-source tree into a fresh git repository
# and push it to GitHub. All 45 dependency trees are committed as plain files
# (they were materialized from submodules), so the resulting repository is
# self-contained.
#
# Usage:
#   bash scripts/push-to-github.sh <github-username> [repo-name] [remote]
#
# Examples:
#   bash scripts/push-to-github.sh alice                 # -> alice/shadPS4
#   bash scripts/push-to-github.sh alice shadPS4-android
#   bash scripts/push-to-github.sh alice shadPS4 git@github.com:alice/shadPS4.git
#
# Prerequisites:
#   - git >= 2.30
#   - SSH key configured for GitHub (or edit REMOTE below to use HTTPS + PAT)
#   - The target repository must NOT already exist with unrelated history.

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <github-username> [repo-name] [remote-url]" >&2
    exit 1
fi

USERNAME="$1"
REPO_NAME="${2:-shadPS4}"
REMOTE_URL="${3:-git@github.com:${USERNAME}/${REPO_NAME}.git}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -f "${ROOT_DIR}/android/build_native.sh" ]]; then
    echo "error: run this script from the shadPS4 Android port tree" >&2
    exit 1
fi

if [[ -d "${ROOT_DIR}/.git" ]]; then
    echo "info: .git already exists, reusing the current repository."
else
    echo "==> Initializing repository at ${ROOT_DIR}"
    git -C "${ROOT_DIR}" init -b main
fi

echo "==> Configuring identity (fallbacks only if unset)"
git -C "${ROOT_DIR}" config user.name  >/dev/null 2>&1 || \
    git -C "${ROOT_DIR}" config user.name  "shadPS4 Android port"
git -C "${ROOT_DIR}" config user.email >/dev/null 2>&1 || \
    git -C "${ROOT_DIR}" config user.email "port@localhost"

echo "==> Staging files (submodule sources are plain files)"
# .gitmodules was removed when the sources were inlined; double-check so a
# stray file cannot resurrect submodule semantics on GitHub's side.
rm -f "${ROOT_DIR}/.gitmodules"
git -C "${ROOT_DIR}" add -A

echo "==> Committing"
if git -C "${ROOT_DIR}" diff --cached --quiet; then
    echo "info: nothing new to commit."
else
    git -C "${ROOT_DIR}" commit -m "shadPS4 Android port (inlined dependencies, manual-trigger CI)

- android/ Gradle project (AGP 8.7.3, minSdk 29, arm64-v8a, SDL3 Big Picture)
- android/build_native.sh native build driver (host protoc + FFmpeg + CMake)
- CMake: SHADPS4_TARGET, host protoc, x86-only source exclusion, 16KB alignment
- sources: SDL_main entry, SHADPS4_DATA_DIR, logcat sink, ARM64 signal ctx,
  ARM64 run guard, urandom UUID
- CI: android-build.yml (workflow_dispatch only); desktop build.yml manual-only
- docs: ANDROID_PORT.md (capability matrix + ARM64 roadmap)"
fi

echo "==> Adding remote ${REMOTE_URL}"
if git -C "${ROOT_DIR}" remote get-url origin >/dev/null 2>&1; then
    git -C "${ROOT_DIR}" remote set-url origin "${REMOTE_URL}"
else
    git -C "${ROOT_DIR}" remote add origin "${REMOTE_URL}"
fi

echo "==> Pushing (first push of ~1.5 GB of sources may take a while)"
git -C "${ROOT_DIR}" push -u origin main

echo
echo "Done. Next steps:"
echo "  1. Open https://github.com/${USERNAME}/${REPO_NAME}/actions"
echo "  2. 'Android Build' -> Run workflow (Release) - first run ~40-80 min"
echo "  3. Optional: add ANDROID_KEYSTORE_BASE64/PASSWORD/KEY_ALIAS/KEY_PASSWORD"
echo "     secrets for signed release APKs."
