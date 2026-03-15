let wasm, memory;
let simW, simH;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/oscillators.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start(width, height) {
    simW = width; simH = height;
    wasm.osc_init(width, height);
}
export function step(n) { wasm.osc_step(n); }
export function setCoupling(k) { wasm.osc_set_coupling(k); }

export function getPixels() {
    const ptr = wasm.osc_pixels();
    return new Uint8Array(memory.buffer, ptr, simW * simH);
}
