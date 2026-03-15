let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/boids.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start(count) { wasm.boids_init(count); }
export function step(n) { wasm.boids_step(n); }
export function swapOne() { wasm.boids_swap_one(); }

export function getPixels() {
    const ptr = wasm.boids_pixels();
    return new Uint8Array(memory.buffer, ptr, 1024 * 1024);
}
