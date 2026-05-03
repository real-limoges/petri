let wasm, memory;

// Render output dimensions are baked into clouds.c (OUT_W * OUT_H). If
// they change in the C source they need to change here too.
export const OUT_W = 600;
export const OUT_H = 300;

export async function init() {
    const resp = await fetch('/vendor/petri/wasm/clouds.wasm');
    const bytes = await resp.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    wasm = instance.exports;
    memory = wasm.memory;
    wasm.clouds_init();
}

export function step(n)            { wasm.clouds_step(n); }
export function setRegime(r)       { wasm.clouds_set_regime(r); }
export function setStyle(s)        { wasm.clouds_set_style(s); }
export function setWind(u)         { wasm.clouds_set_wind(u); }
export function seed(pattern)      { wasm.clouds_seed(pattern); }

export function getPixels() {
    const ptr = wasm.clouds_pixels();
    return new Uint8ClampedArray(memory.buffer, ptr, OUT_W * OUT_H * 4);
}
