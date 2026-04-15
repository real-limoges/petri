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

export function getPixels() {
    const ptr = wasm.boids_pixels();
    return new Uint8Array(memory.buffer, ptr, simW * simH);
}

// Defaults match the C source — kept here so JS callers can reset to known values.
export const DEFAULTS = Object.freeze({
    count: 1500,
    sep_radius: 25.0,
    align_radius: 30.0,
    cohesion_radius: 35.0,
    sep_force: 0.06,
    align_force: 0.02,
    cohesion_force: 0.002,
    max_speed: 3.0,
    min_speed: 0.5,
    trail_decay: 0.97,
    crowd_threshold: 8.0,
});

const FLOAT_SETTERS = {
    sep_radius:       "boids_set_sep_radius",
    align_radius:     "boids_set_align_radius",
    cohesion_radius:  "boids_set_cohesion_radius",
    sep_force:        "boids_set_sep_force",
    align_force:      "boids_set_align_force",
    cohesion_force:   "boids_set_cohesion_force",
    max_speed:        "boids_set_max_speed",
    min_speed:        "boids_set_min_speed",
    trail_decay:      "boids_set_trail_decay",
    crowd_threshold:  "boids_set_crowd_threshold",
};

export function setParam(name, value) {
    if (name === "count") {
        wasm.boids_set_count(value | 0);
        return;
    }
    const fn = FLOAT_SETTERS[name];
    if (fn && wasm[fn]) wasm[fn](+value);
}

export function setParams(params) {
    for (const key in params) setParam(key, params[key]);
}
