let wasm, memory;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/prism.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
    wasm.prism_init();
}

export function setTilt(radians) { wasm.prism_set_tilt(radians); }
export function step()           { wasm.prism_step(); }

export function getRays() {
    const ptr = wasm.prism_rays();
    const n   = wasm.prism_ray_count();
    return new Float32Array(memory.buffer, ptr, n * 9);
}

export const FLOATS_PER_RAY = 9;
