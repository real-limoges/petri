let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/physarum.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start(count) { wasm.physarum_init(count); }
export function step(n) { wasm.physarum_step(n); }
export function setParams(sa, sd, ts, ms, dep, dec) {
    wasm.physarum_set_params(sa, sd, ts, ms, dep, dec);
}

export function getPixels() {
    const ptr = wasm.physarum_pixels();
    return new Uint8ClampedArray(memory.buffer, ptr, 1024 * 1024 * 4);
}
