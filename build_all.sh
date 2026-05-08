#!/bin/sh
# Build every WASM module under wasm/. Each module is one freestanding C
# file compiled directly to wasm32-unknown-unknown — no libc, no runtime,
# no allocator. Initial-memory is sized to fit the module's static
# buffers (see comments per build below).
#
# Override the compiler with CC=/path/to/clang. Defaults to Homebrew LLVM.

set -e

CC="${CC:-$(brew --prefix llvm 2>/dev/null)/bin/clang}"
if [ ! -x "$CC" ]; then
    echo "build_all: clang not found at \"$CC\"." >&2
    echo "  Set CC=/path/to/clang or install LLVM (brew install llvm)." >&2
    exit 1
fi

WASM_FLAGS="--target=wasm32-unknown-unknown -O2 -flto -nostdlib \
            -Wl,--no-entry -Wl,--export-dynamic"

mkdir -p wasm

build() {
    # build <module> <initial-memory-bytes> <comment>
    name=$1; mem=$2; note=$3
    printf '  %-12s mem=%9d  %s\n' "$name" "$mem" "$note"
    $CC $WASM_FLAGS -Wl,--initial-memory="$mem" \
        -o "wasm/$name.wasm" "src/$name.c"
}

# 32 MB: 2560×1440 single-byte pixel buffer + same-shape float trail map
# = ~14.7 MB of state, rounded up to the next power of two.
build boids       33554432  "flocking, trails"

# 32 MB: same render envelope as boids but only an unsigned char grid +
# float heat map.
build langton     33554432  "Langton's ants, heat map"

# 64 MB: three full-resolution float fields (phase × 2 + freq) ≈ 44 MB.
build oscillators 67108864  "Kuramoto, ping-ponged phase buffers"

# 64 MB: padded grid + scheduled flags + 4 MB queue + render buffer.
build sandpile    67108864  "Abelian sandpile, padded queue"

# 128 KB: 60 rays × 9 floats. Tiny.
build prism          131072  "ray tracing, geometry-out"

# 128 KB: three 391-entry float tables and a 3-element scratch.
build cones          131072  "LMS cone fundamentals"

# 4 MB: 120×60 grid × ~10 fields × 4 bytes ≈ 300 KB; rounded up to 4 MB
# so the JS hook can attach a few more diagnostic views without a rebuild.
build clouds       4194304  "moist convection on a vertical slice"

echo
echo "wasm/:"
ls -l wasm/*.wasm | awk '{ printf "  %6s bytes  %s\n", $5, $9 }'
