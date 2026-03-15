#!/bin/sh
CC="${CC:-$(brew --prefix llvm)/bin/clang}"
FLAGS="--target=wasm32-unknown-unknown -O2 -flto -nostdlib -Wl,--no-entry -Wl,--export-dynamic"

$CC $FLAGS -Wl,--initial-memory=33554432  -o petri.wasm     src/petri.c
$CC $FLAGS -Wl,--initial-memory=67108864  -o physarum.wasm  src/physarum.c
$CC $FLAGS -Wl,--initial-memory=33554432  -o boids.wasm     src/boids.c
$CC $FLAGS -Wl,--initial-memory=8388608   -o lenia.wasm     src/lenia.c
$CC $FLAGS -Wl,--initial-memory=16777216  -o dla.wasm       src/dla.c
$CC $FLAGS -Wl,--initial-memory=16777216  -o langton.wasm   src/langton.c
$CC $FLAGS -Wl,--initial-memory=16777216  -o oscillators.wasm src/oscillators.c

echo "Built all WASM modules."
