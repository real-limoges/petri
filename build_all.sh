#!/bin/sh
CC="${CC:-$(brew --prefix llvm)/bin/clang}"

mkdir -p wasm

$CC --target=wasm32-unknown-unknown -O2 -flto -nostdlib \
    -Wl,--no-entry -Wl,--export-dynamic \
    -Wl,--initial-memory=33554432 \
    -o wasm/boids.wasm src/boids.c

$CC --target=wasm32-unknown-unknown -O2 -flto -nostdlib \
    -Wl,--no-entry -Wl,--export-dynamic \
    -Wl,--initial-memory=33554432 \
    -o wasm/langton.wasm src/langton.c

$CC --target=wasm32-unknown-unknown -O2 -flto -nostdlib \
    -Wl,--no-entry -Wl,--export-dynamic \
    -Wl,--initial-memory=67108864 \
    -o wasm/oscillators.wasm src/oscillators.c

$CC --target=wasm32-unknown-unknown -O2 -flto -nostdlib \
    -Wl,--no-entry -Wl,--export-dynamic \
    -Wl,--initial-memory=104857600 \
    -o wasm/physarum.wasm src/physarum.c

echo "Built all WASM modules."