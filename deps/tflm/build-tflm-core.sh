#!/usr/bin/env bash
# Build a standalone tflm-core.a: TensorFlow Lite for Microcontrollers (with
# CMSIS-NN kernels) for Cortex-M55, using arm-none-eabi-gcc -- the toolchain
# tflite-micro's own build system defaults to. Depends only on upstream,
# open-source tensorflow/tflite-micro -- no Docker and no other part of this
# repo's build system is involved.
#
# Used to (re)generate the prebuilt archive committed at
# deps/tflm/prebuilt/tflm-core.a -- re-run and commit the result whenever
# TFLM_REF below is bumped.
#
# Flags below are taken from tflite-micro's own official reference for this
# exact scenario -- tensorflow/lite/micro/tools/project_generation/Makefile's
# BUILD_TYPE=cmsis_nn block -- plus -mcpu=cortex-m55/-mfloat-abi=hard, which
# that reference Makefile deliberately omits (it's CPU-agnostic; those are
# ours to pick for this specific target). Optimization levels (-Os core /
# -O2 kernels) match the main Makefile's CORE_OPTIMIZATION_LEVEL /
# KERNEL_OPTIMIZATION_LEVEL for TOOLCHAIN=gcc.
#
# Requires git, python3, patch, unzip, wget, and arm-none-eabi-gcc on PATH
# (patch/unzip/wget are needed by tflite-micro's own third-party
# download/patch scripts for CMSIS/CMSIS-NN/kissfft).
#
# Usage: build-tflm-core.sh <output-dir> [output-name]
#   TFLM_REF     upstream tensorflow/tflite-micro ref to build: a commit SHA,
#                branch, or tag. Defaults to a specific commit pinned below
#                for reproducibility -- upstream has no version tags, main
#                is a rolling branch, so a SHA is the only real "version".
#                Pass TFLM_REF=main for the latest, or any other SHA/tag to
#                pin a different version.
#   TARGET_ARCH  Cortex-M target passed to create_tflm_tree.py (default: cortex-m55)
#
# Alongside <output-name>, writes <output-name>.version recording the exact
# resolved commit built, for traceability.
#
# NOTE: the resulting archive is ABI-coupled to the flags below (target
# triple, -mcpu/-mfloat-abi, -fno-exceptions/-fno-rtti, -std=c++17/c17). Any
# consumer linking against it must use compatible flags.
set -euo pipefail

OUT_DIR="${1:?usage: build-tflm-core.sh <output-dir> [output-name]}"
OUT_NAME="${2:-tflm-core.a}"
TFLM_REF="${TFLM_REF:-0ee39f5fc6629b7403166d325da374f01d890cf1}"
TARGET_ARCH="${TARGET_ARCH:-cortex-m55}"

CC=arm-none-eabi-gcc
CXX=arm-none-eabi-g++
AR=arm-none-eabi-ar

# GCC infers the Cortex-M55 FPU/MVE config from -mcpu alone -- no -mfpu=
# needed (that's a clang-only spelling).
CPU_FLAGS=(-mcpu=cortex-m55 -mthumb -mfloat-abi=hard)

COMMON_FLAGS=(
    "${CPU_FLAGS[@]}"
    -mlittle-endian
    -funsigned-char
    -fomit-frame-pointer
    -fno-unwind-tables
    -ffunction-sections
    -fdata-sections
    -fmessage-length=0
    -DTF_LITE_STATIC_MEMORY
    -DTF_LITE_MCU_DEBUG_LOG
    -DPROJECT_GENERATION
    -DCMSIS_NN
)
CXXFLAGS=("${COMMON_FLAGS[@]}" -std=c++17 -fno-rtti -fno-exceptions -fno-threadsafe-statics)
CFLAGS=("${COMMON_FLAGS[@]}" -std=c17)

# Matches the main Makefile's CORE_OPTIMIZATION_LEVEL (-Os, non-armclang) and
# KERNEL_OPTIMIZATION_LEVEL/THIRD_PARTY_KERNEL_OPTIMIZATION_LEVEL (both -O2).
CORE_OPT=-Os
KERNEL_OPT=-O2

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# Plain `git clone --branch` only accepts branch/tag names, not arbitrary
# commit SHAs. init+fetch by ref works uniformly for branches, tags, and
# full commit SHAs (GitHub serves shallow fetches of any reachable commit).
mkdir -p "$WORK_DIR/tflite-micro"
git -C "$WORK_DIR/tflite-micro" init -q
git -C "$WORK_DIR/tflite-micro" remote add origin https://github.com/tensorflow/tflite-micro.git
git -C "$WORK_DIR/tflite-micro" fetch --depth 1 origin "$TFLM_REF"
git -C "$WORK_DIR/tflite-micro" checkout -q FETCH_HEAD
RESOLVED_SHA="$(git -C "$WORK_DIR/tflite-micro" rev-parse HEAD)"

TREE_DIR="$WORK_DIR/tflm_tree"
# TOOLCHAIN=armclang here only prevents create_tflm_tree.py's internal
# Makefile from downloading a full ARM GNU toolchain as a side effect of its
# default TOOLCHAIN=gcc (we don't use their generated build files at all --
# we compile the resulting tree ourselves with arm-none-eabi-gcc below).
(cd "$WORK_DIR/tflite-micro" && python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py \
    "$TREE_DIR" \
    --makefile_options="TARGET=cortex_m_generic TARGET_ARCH=${TARGET_ARCH} OPTIMIZED_KERNEL_DIR=cmsis_nn TOOLCHAIN=armclang TENSORFLOW_ROOT=")

INCLUDES=(
    -I"$TREE_DIR"
    -I"$TREE_DIR/third_party/flatbuffers/include"
    -I"$TREE_DIR/third_party/gemmlowp"
    -I"$TREE_DIR/third_party/ruy"
    -I"$TREE_DIR/third_party/kissfft"
    -I"$TREE_DIR/third_party/kissfft/tools"
    -I"$TREE_DIR/third_party/cmsis"
    -I"$TREE_DIR/third_party/cmsis/CMSIS/Core/Include"
    -I"$TREE_DIR/third_party/cmsis_nn"
    -I"$TREE_DIR/third_party/cmsis_nn/Include"
)

OBJ_DIR="$WORK_DIR/obj"
mkdir -p "$OBJ_DIR"
OBJS=()

# "Kernel" sources (op implementations, incl. CMSIS-NN) get -O2; everything
# else (core interpreter, allocators, third-party support code) gets -Os.
is_kernel_src() {
    case "$1" in
        */kernels/*|*/third_party/cmsis_nn/Source/*) return 0 ;;
        *) return 1 ;;
    esac
}

while IFS= read -r -d '' src; do
    obj="$OBJ_DIR/$(echo "$src" | tr '/' '_').o"
    opt="$CORE_OPT"; is_kernel_src "$src" && opt="$KERNEL_OPT"
    "$CC" "${CFLAGS[@]}" "$opt" "${INCLUDES[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done < <(find "$TREE_DIR/third_party" -name '*.c' -print0)

while IFS= read -r -d '' src; do
    obj="$OBJ_DIR/$(echo "$src" | tr '/' '_').o"
    opt="$CORE_OPT"; is_kernel_src "$src" && opt="$KERNEL_OPT"
    "$CXX" "${CXXFLAGS[@]}" "$opt" "${INCLUDES[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done < <(find "$TREE_DIR/tensorflow" "$TREE_DIR/signal" -name '*.cc' -print0)

mkdir -p "$OUT_DIR"
"$AR" rcs "$OUT_DIR/$OUT_NAME" "${OBJS[@]}"
cat > "$OUT_DIR/$OUT_NAME.version" <<EOF
tensorflow/tflite-micro @ $RESOLVED_SHA
requested TFLM_REF: $TFLM_REF
TOOLCHAIN: GCC_ARM
TARGET_ARCH: $TARGET_ARCH
built: $(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF
echo "Built $OUT_DIR/$OUT_NAME ($(du -h "$OUT_DIR/$OUT_NAME" | cut -f1)) from tensorflow/tflite-micro@$RESOLVED_SHA"
