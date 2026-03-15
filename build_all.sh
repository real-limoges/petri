#!/bin/sh
CC="${CC:-$(brew --prefix llvm)/bin/clang}"
FLAGS="--target=wasm32-unknown-unknown -O2 -flto -nostdlib -Wl,--no-entry -Wl,--export-dynamic"

mkdir -p wasm

$CC $FLAGS -Wl,--initial-memory=33554432  -o wasm/boids.wasm       src/boids.c
$CC $FLAGS -Wl,--initial-memory=16777216  -o wasm/langton.wasm     src/langton.c
$CC $FLAGS -Wl,--initial-memory=16777216  -o wasm/oscillators.wasm src/oscillators.c
$CC $FLAGS -Wl,--initial-memory=67108864  -o wasm/physarum.wasm    src/physarum.c

echo "Built all WASM modules."
