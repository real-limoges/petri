let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/petri.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
    wasm.petri_init();
}

export function step(n) { wasm.petri_step(n); }
export function setParams(f, k) { wasm.petri_set_params(f, k); }
export function seed(pattern) { wasm.petri_seed(pattern); }

export function getPixels() {
    const ptr = wasm.petri_pixels();
    return new Uint8ClampedArray(memory.buffer, ptr, 512 * 512 * 4);
}