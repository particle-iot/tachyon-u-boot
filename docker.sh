#!/usr/bin/env bash
set -euo pipefail

# Resolve absolute path of current working directory
PROJECT_DIR="$(pwd)"
TMP_DIR="${PROJECT_DIR}/.tmp"

# Ensure .tmp exists
mkdir -p "${TMP_DIR}"

docker run --rm -it --privileged \
  -v "${PROJECT_DIR}:/project" \
  -v "${TMP_DIR}:/tmp/work" \
  -v /dev:/dev \
  -w /project \
  tachyon-system-image-builder:1.3 \
  bash
