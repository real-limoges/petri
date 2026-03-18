let wasm, memory;
let simW, simH;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/boids.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start(count, width, height) {
    simW = width; simH = height;
    wasm.boids_init(count, width, height);
}
export function step(n) { wasm.boids_step(n); }
export function swapN(count) { wasm.boids_swap_n(count); }
export function swapEdge(count) { wasm.boids_swap_edge(count); }

export function getPixels() {
    const ptr = wasm.boids_pixels();
    return new Uint8Array(memory.buffer, ptr, simW * simH);
}
