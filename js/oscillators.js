let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/oscillators.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start() { wasm.osc_init(); }
export function step(n) { wasm.osc_step(n); }
export function setCoupling(k) { wasm.osc_set_coupling(k); }

export function getPixels() {
    const ptr = wasm.osc_pixels();
    return new Uint8ClampedArray(memory.buffer, ptr, 512 * 512 * 4);
}
