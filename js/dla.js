let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/dla.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start(walkers) { wasm.dla_init(walkers); }
export function step(n) { wasm.dla_step(n); }

export function getPixels() {
    const ptr = wasm.dla_pixels();
    return new Uint8ClampedArray(memory.buffer, ptr, 1024 * 1024 * 4);
}
