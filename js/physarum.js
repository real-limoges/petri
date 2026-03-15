let wasm, memory;
let simW, simH;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/physarum.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
}

export function start(count, width, height) {
    simW = width; simH = height;
    wasm.physarum_init(count, width, height);
}
export function step(n) { wasm.physarum_step(n); }
export function setParams(sa, sd, ts, ms, dep, dec) {
    wasm.physarum_set_params(sa, sd, ts, ms, dep, dec);
}

export function getPixels() {
    const ptr = wasm.physarum_pixels();
    return new Uint8Array(memory.buffer, ptr, simW * simH);
}
