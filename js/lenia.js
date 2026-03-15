let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/lenia.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start() { wasm.lenia_init(); }
export function step(n) { wasm.lenia_step(n); }

export function getPixels() {
    const ptr = wasm.lenia_pixels();
    return new Uint8ClampedArray(memory.buffer, ptr, 512 * 512 * 4);
}
