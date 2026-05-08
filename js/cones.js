// cones.js
//
// Thin wrapper around cones.wasm. Most consumers on the Fugue side load
// the WASM module directly because they only need the raw activations
// pointer; this wrapper exists for callers that want a JS-shaped API
// matching the other modules in this repo.

let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/cones.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
    wasm.cones_init();
}

// L, M, S activations at the given wavelength (nm). `mode` zeroes one
// channel: 0 normal, 1 protanope (no L), 2 deuteranope (no M),
// 3 tritanope (no S). Returns a Float32Array view of length 3 backed by
// WASM memory; values are overwritten on every call.
export function activations(lambdaNm, mode = 0) {
    const ptr = wasm.cones_activations(lambdaNm, mode | 0);
    return new Float32Array(memory.buffer, ptr, 3);
}

export function lambdaMin() { return wasm.cones_lambda_min(); }
export function lambdaMax() { return wasm.cones_lambda_max(); }
