let wasm, memory;
let simW, simH;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/sandpile.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start(width, height) {
    simW = width; simH = height;
    wasm.sandpile_init(width, height);
}

export function step(n) { wasm.sandpile_step(n); }
export function drop(x, y) { wasm.sandpile_drop(x, y); }
export function dropCenter() { wasm.sandpile_drop_center(); }
export function dropRandom() { wasm.sandpile_drop_random(); }
export function setMode(m) { wasm.sandpile_set_mode(m); }
export function getLastAvalancheSize() { return wasm.sandpile_last_avalanche_size(); }
export function getTotalGrains() { return wasm.sandpile_total_grains(); }

export function getPixels() {
    const ptr = wasm.sandpile_pixels();
    return new Uint8Array(memory.buffer, ptr, simW * simH);
}
