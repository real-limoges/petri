#!/bin/sh
CC="${CC:-$(brew --prefix llvm)/bin/clang}"
$CC --target=wasm32-unknown-unknown -O2 -flto -nostdlib \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=8388608 \
  -o petri.wasm src/petri.c