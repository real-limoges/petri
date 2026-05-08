# Petri

A collection of emergent-behavior and physical-simulation toys running in the browser. Freestanding C compiled to WASM with clang — no Emscripten, no libc, no runtime. Part of [Fugue](https://github.com/real-limoges/fugue), powering the [realcomplex.systems](http://realcomplex.systems) landing page.

Seven modules, each a different flavor of "simple local rules → complex global behavior" — plus one reference dataset.

## The Simulations

### Boids

A flock without a leader. Each boid follows three rules — avoid crowding neighbors, steer toward their average heading, and drift toward the group center. Nothing coordinates them. The murmuration-like swirling, splitting, and regrouping emerges entirely from those three local interactions. A trail map gives each boid a glowing wake, so the flock paints neon arcs across a dark field. Every parameter (separation/alignment/cohesion radii and forces, max/min speed, trail decay, crowd threshold) is exposed to JS so the page can tune the flock live.

### Langton's Ants

An ant on a grid flips the color of each cell it visits and turns accordingly — right on white, left on black. For thousands of steps, it scribbles chaos. Then, abruptly, it builds a perfectly straight diagonal highway and never stops. Nobody has proven why. Multiple ants interact, their highways colliding and interfering with each other. A heat map colors cells by visit frequency, revealing the hidden structure beneath the apparent disorder.

### Oscillators

A grid of fireflies, each blinking at its own natural rhythm. Each one nudges its neighbors toward synchrony — the Kuramoto model. Watch long enough and traveling waves of phase coherence sweep across the grid. Pockets synchronize, fronts collide, and the whole field breathes. The coupling strength controls how quickly (or whether) global order emerges from the initial randomness.

### Sandpile

Drop grains on a square. Cells with four or more grains topple, sending one grain to each cardinal neighbor. Most drops do nothing. A few trigger avalanches that cascade across the board in fractal bursts — self-organized criticality. The grid is sentinel-padded so border grains fall off into the void (open boundary), and a power-of-two circular queue with deduping makes the topple loop branchless. JS gets the avalanche size, total grains, and an intensity buffer per frame.

### Prism

A white beam enters a circular glass cross-section and fans out into a spectrum on the far wall. Snell's law at both surfaces, with refractive index per wavelength from the Cauchy equation (crown-glass-like coefficients). Unlike the other modules, prism is **geometry-out**: it returns a flat array of ray records (entry, exit, screen, RGB) and JS rasterizes them on a Canvas2D context. Tilt is the one live input — the cursor maps to the prism's rotation about its center.

### Cones

Not really a simulation — a lookup table for the Stockman & Sharpe (2000) 2-degree LMS cone fundamentals at 1 nm spacing across 390–780 nm. Given a wavelength, returns L, M, S activations (peak-normalized). Supports protanope/deuteranope/tritanope modes by zeroing the corresponding channel. Used by other Fugue pages that visualize human color perception.

### Clouds

A 2D vertical slice of moist convection — Boussinesq physics with a saturation closure. Each step does semi-Lagrangian advection of temperature, water vapor, cloud water, and velocity; applies buoyancy and dry-adiabatic cooling; projects velocity onto the divergence-free subspace via Jacobi pressure; then runs Tetens-formula saturation adjustment. The current scene is plains fair-weather cumulus: warm surface, conditionally unstable atmosphere, cauliflower clouds drifting on a gentle wind. The WASM module is physics-only — it exposes the raw cloud-water field and a `clouds_apply_bubble` forcing primitive; palette, view window, and tone mapping live in the consuming JS hook (`clouds_canvas.js` on the Fugue side).

---

## Technical Details

### Architecture

```
petri/
├── src/
│   ├── boids.c          # flocking, parameterized
│   ├── clouds.c         # 2D moist convection
│   ├── clouds_test.c    # native PGM-dump driver, includes clouds.c
│   ├── cones.c          # LMS cone fundamentals
│   ├── langton.c        # multi-ant Langton
│   ├── oscillators.c    # Kuramoto
│   ├── prism.c          # dispersion, geometry-out
│   └── sandpile.c       # abelian sandpile
├── js/
│   ├── boids.js         # WASM loader + export wrappers
│   ├── clouds.js
│   ├── cones.js
│   ├── langton.js
│   ├── oscillators.js
│   ├── prism.js
│   └── sandpile.js
├── wasm/
│   ├── boids.wasm       # build outputs (committed)
│   ├── clouds.wasm
│   ├── cones.wasm
│   ├── langton.wasm
│   ├── oscillators.wasm
│   ├── prism.wasm
│   └── sandpile.wasm
├── build_all.sh         # builds all WASM modules
├── CLAUDE.md
└── README.md
```

Each module is one C file with a matching JS wrapper. Both `cones` and `clouds` can also be loaded directly from Fugue-side code that only needs the raw exports.

### Simulation Details

| Module | Grid / Domain | Agents | Live Inputs | Output |
|--------|---------------|--------|-------------|--------|
| **Boids** | up to 2560×1440 | up to 3,000 | count, sep/align/cohesion radii & forces, max/min speed, trail decay, crowd threshold | RGBA pixels |
| **Langton** | up to 2560×1440 | up to 12 ants | — (deterministic) | RGBA pixels |
| **Oscillators** | up to 2560×1440 | — (grid cells) | coupling strength | RGBA pixels |
| **Sandpile** | up to 2560×1440 padded | — (grid cells) | drop position / mode | intensity buffer + avalanche stats |
| **Prism** | 600×400 world units | 60 wavelength samples | tilt (radians) | float32 ray records (9 floats per ray) |
| **Cones** | 390–780 nm, 1 nm step | — | wavelength, daltonism mode | 3 floats (L, M, S) |
| **Clouds** | 6 km × 3 km, 120×60 cells | — (Eulerian fields) | wind, seed pattern, warm-bubble forcing | float32 cloud-water field (NX×NZ) |

### WASM Exports

```c
// boids.c
void  boids_init(int count, int width, int height);
void  boids_set_count(int n);
void  boids_set_sep_radius(float r);
void  boids_set_align_radius(float r);
void  boids_set_cohesion_radius(float r);
void  boids_set_sep_force(float f);
void  boids_set_align_force(float f);
void  boids_set_cohesion_force(float f);
void  boids_set_max_speed(float s);
void  boids_set_min_speed(float s);
void  boids_set_trail_decay(float d);
void  boids_set_crowd_threshold(float t);
void  boids_step(int n);
unsigned char* boids_pixels(void);

// langton.c
void  langton_init(int num_ants, int width, int height);
void  langton_step(int n);
unsigned char* langton_pixels(void);

// oscillators.c
void  osc_init(int width, int height);
void  osc_set_coupling(float k);
void  osc_step(int n);
unsigned char* osc_pixels(void);

// sandpile.c
void  sandpile_init(int width, int height);
void  sandpile_set_mode(int mode);          // 0 = center, 1 = random
void  sandpile_drop(int x, int y);
void  sandpile_drop_center(void);
void  sandpile_drop_random(void);
void  sandpile_step(int count);
int   sandpile_last_avalanche_size(void);
int   sandpile_total_grains(void);
unsigned char* sandpile_pixels(void);

// prism.c
void  prism_init(void);
void  prism_set_tilt(float radians);
void  prism_step(void);
const float* prism_rays(void);              // n_rays * 9 floats
int   prism_ray_count(void);

// cones.c
void  cones_init(void);
const float* cones_activations(float lambda_nm, int mode);
const float* cones_values(void);
int   cones_lambda_min(void);               // 390
int   cones_lambda_max(void);               // 780

// clouds.c — physics-only; render lives JS-side
void  clouds_init(void);
void  clouds_step(int n);                   // n × Δt = 1 s
void  clouds_set_wind(float u_ms);
void  clouds_set_weather(float w);          // API-compat no-op
void  clouds_seed(int pattern);             // 0=rest, 1=test bubble, 2=default
void  clouds_apply_bubble(float x_m, float z_m, float dT_K, float radius_m);
const float* clouds_qc(void);               // NX*NZ cloud-water field
int   clouds_grid_nx(void);
int   clouds_grid_nz(void);
float clouds_grid_dx(void);
float clouds_grid_dz(void);
```

### Shared Implementation Patterns

**No libc.** No `malloc`, no `printf`, no `math.h`. Each file provides its own `memset` (and `memcpy` where needed). Trigonometry is 7th-order Taylor series; clouds also carries a hand-rolled `expf` (range-reduction + 6-term Taylor on the reduced interval, IEEE-bit-trick for the `2^k` factor) for the Tetens saturation formula. Square roots use `__builtin_sqrtf` (which lowers to the WASM `f32.sqrt` opcode — not a libc call). Random numbers come from xorshift PRNGs.

**Static allocation.** All buffers are global arrays sized at compile time, with `MAX_W`/`MAX_H` dimensions of 2560×1440 (so a single build covers any reasonable canvas). No `memory.grow`, which means JS typed-array views into WASM linear memory never invalidate.

**Toroidal boundaries — except sandpile.** Boids/Langton/Oscillators wrap with modular arithmetic. Sandpile uses a sentinel-padded grid with permanently-`scheduled` border cells, so grains falling off vanish (open boundary). Prism is continuous geometry, no grid.

**Ping-pong buffers.** Oscillators keeps two phase arrays and swaps the read/write pointers after each step, so neighbour reads always see the previous frame and the renderer always sees a fully-resolved frame.

**Zero-copy output.** Pixel/ray functions return pointers to static buffers. JS wraps each in a typed-array view (`Uint8ClampedArray` for pixels, `Float32Array` for rays) — no data crosses the JS/WASM boundary per frame.

**Trail visualization.** Boids and Langton use heat/trail maps that decay over time, turning agent paths into glowing traces. Warm-toned color ramps on dark backgrounds throughout.

### Build

Requires LLVM with `wasm-ld`:
- **macOS:** `brew install llvm` (Xcode clang lacks `wasm-ld`)
- **Linux:** `apt install clang lld`

```bash
./build_all.sh
```

The script auto-detects Homebrew LLVM on macOS. Override with `CC=/path/to/clang ./build_all.sh`. Outputs land in `wasm/`. After building, commit the `.wasm` files — Fugue imports them directly.

Initial-memory sizing per module is set in `build_all.sh`: 32 MB for boids/langton, 64 MB for oscillators/sandpile, 128 KB for prism/cones, 4 MB for clouds. Don't bump these casually — the JS side relies on the buffer never detaching, which means no `memory.grow`.

Clouds has a native validation driver in `src/clouds_test.c`. It `#include`s `clouds.c` with `NATIVE_TEST` defined so the driver can reach the static fields, then dumps PGM frames of the `qc` field plus diagnostics for hydrostatic rest, single-bubble convection, and a marine-layer steady state. Build with `clang -O2 -lm -o test src/clouds_test.c`, run with `./test ./out`. This is the only module with a native validation path so far — added because the cloud sim has more physics to get wrong than the others.

### Fugue Integration

Git submodule at `assets/vendor/petri/`. LiveView hooks import from `js/*.js` and run `requestAnimationFrame` loops client-side. Pixel-output sims do `step(n)` → `getPixels()` → `putImageData`; prism does `step()` → `getRays()` → Canvas2D path drawing. No server round-trips for simulation state.

### Design Decisions

**C, not Rust/Zig** — Arithmetic on flat arrays, no allocator, no strings, no error handling. C is the right tool for what this is.

**No Emscripten** — These modules use zero libc functions. Direct `clang --target=wasm32` produces tiny binaries (most modules under 6 KB) and ~25 lines of JS glue per module vs. Emscripten's generated loader.

**Canvas 2D, not WebGPU** — Rendering is a single `putImageData` (or a few hundred line draws for prism). WebGPU only helps if a simulation moves to a compute shader.

**Committed .wasm** — Anyone cloning Fugue can run without a C toolchain.

**One file per module** — Each is self-contained. No shared headers, no build dependencies between modules. You can delete any one without affecting the others.

## References

- Reynolds, C.W. (1987). "Flocks, Herds, and Schools: A Distributed Behavioral Model." *Computer Graphics*, 21(4), 25–34.
- Langton, C.G. (1986). "Studying Artificial Life with Cellular Automata." *Physica D*, 22(1–3), 120–149.
- Kuramoto, Y. (1975). "Self-Entrainment of a Population of Coupled Non-linear Oscillators." *International Symposium on Mathematical Problems in Theoretical Physics*, 420–422.
- Bak, P., Tang, C., & Wiesenfeld, K. (1987). "Self-Organized Criticality: An Explanation of 1/f Noise." *Physical Review Letters*, 59(4), 381–384.
- Stockman, A., & Sharpe, L.T. (2000). "The spectral sensitivities of the middle- and long-wavelength-sensitive cones derived from measurements in observers of known genotype." *Vision Research*, 40(13), 1711–1737.
- Boussinesq, J. (1903). *Théorie analytique de la chaleur*, Vol. 2. Gauthier-Villars.
- Tetens, O. (1930). "Über einige meteorologische Begriffe." *Zeitschrift für Geophysik*, 6, 297–309.
