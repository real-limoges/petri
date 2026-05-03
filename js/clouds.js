// Physics-only WASM. Composition (palette, view window, tones) lives
// in the consuming JS (e.g. assets/js/hooks/clouds_canvas.js).

let wasm, memory;
let _NX = 120, _NZ = 60, _DX = 50, _DZ = 50;

export async function init() {
  const resp = await fetch('/vendor/petri/wasm/clouds.wasm');
  const bytes = await resp.arrayBuffer();
  const { instance } = await WebAssembly.instantiate(bytes);
  wasm = instance.exports;
  memory = wasm.memory;
  wasm.clouds_init();
  _NX = wasm.clouds_grid_nx();
  _NZ = wasm.clouds_grid_nz();
  _DX = wasm.clouds_grid_dx();
  _DZ = wasm.clouds_grid_dz();
}

export function step(n)       { wasm.clouds_step(n); }
export function setWind(u)    { wasm.clouds_set_wind(u); }
export function setWeather(w) { wasm.clouds_set_weather(w); }
export function seed(p)       { wasm.clouds_seed(p); }

// Drop a Gaussian warm anomaly at (x_m, z_m) with peak temperature
// perturbation dT_K and 1-sigma radius radius_m.
export function applyBubble(x_m, z_m, dT_K, radius_m) {
  wasm.clouds_apply_bubble(x_m, z_m, dT_K, radius_m);
}

// Cloud water mixing ratio field, NX*NZ Float32Array view into WASM
// linear memory. Stable pointer -- safe to cache. Indexed iz*NX + ix.
export function getQC() {
  const ptr = wasm.clouds_qc();
  return new Float32Array(memory.buffer, ptr, _NX * _NZ);
}

export const grid = {
  get NX() { return _NX; },
  get NZ() { return _NZ; },
  get DX() { return _DX; },
  get DZ() { return _DZ; },
  get widthM()  { return _NX * _DX; },
  get heightM() { return _NZ * _DZ; },
};
