# Petri

A collection of emergent behavior simulations running in the browser. Freestanding C compiled to WASM with clang — no Emscripten, no libc, no runtime. Part of [Fugue](https://github.com/real-limoges/fugue), powering the [realcomplex.systems](http://realcomplex.systems) landing page.

Four simulations, each a different flavor of emergence: simple local rules producing complex global behavior.

## The Simulations

### Boids

A flock without a leader. Each boid follows three rules — avoid crowding neighbors, steer toward their average heading, and drift toward the group center. Nothing coordinates them. The murmuration-like swirling, splitting, and regrouping emerges entirely from those three local interactions. A trail map gives each boid a glowing wake, so the flock paints neon arcs across a dark field.

### Langton's Ants

An ant on a grid flips the color of each cell it visits and turns accordingly — right on white, left on black. For thousands of steps, it scribbles chaos. Then, abruptly, it builds a perfectly straight diagonal highway and never stops. Nobody has proven why. Multiple ants interact, their highways colliding and interfering with each other. A heat map colors cells by visit frequency, revealing the hidden structure beneath the apparent disorder.

### Oscillators

A grid of fireflies, each blinking at its own natural rhythm. Each one nudges its neighbors toward synchrony — the Kuramoto model. Watch long enough and traveling waves of phase coherence sweep across the grid. Pockets synchronize, fronts collide, and the whole field breathes. The coupling strength controls how quickly (or whether) global order emerges from the initial randomness.

### Physarum

Digital slime mold. A hundred thousand particle agents wander a grid, depositing chemical trail as they go. Each agent sniffs the trail ahead — left, center, right — and turns toward the strongest signal. The trail diffuses and decays. The result: the agents self-organize into a pulsing network of veins and transport channels, strikingly similar to real *Physarum polycephalum*. The network continuously adapts, pruning weak connections and reinforcing strong ones.

---

## Technical Details

### Architecture

```
petri/
├── src/
│   ├── boids.c          # flocking (199 lines)
│   ├── langton.c         # ants (100 lines)
│   ├── oscillators.c     # Kuramoto (107 lines)
│   └── physarum.c        # slime mold (197 lines)
├── js/
│   ├── boids.js          # WASM loader + export wrappers
│   ├── langton.js
│   ├── oscillators.js
│   └── physarum.js
├── wasm/
│   ├── boids.wasm        # build outputs (committed)
│   ├── langton.wasm
│   ├── oscillators.wasm
│   └── physarum.wasm
├── build_macos.sh        # builds all WASM modules
├── CLAUDE.md
└── README.md
```

~600 lines of C total. Each simulation is one C file and one JS file.

### Simulation Details

| Sim | Grid | Agents | Key Params | WASM Exports |
|-----|------|--------|------------|--------------|
| **Boids** | 1024x1024 | up to 3,000 | sep/align/cohesion radii, forces, trail decay | `boids_init(count)`, `boids_step(n)`, `boids_swap_one()`, `boids_pixels()` |
| **Langton** | 1024x1024 | up to 12 | — (deterministic rules) | `langton_init(count)`, `langton_step(n)`, `langton_pixels()` |
| **Oscillators** | 512x512 | — (grid cells) | coupling strength, dt | `osc_init()`, `osc_step(n)`, `osc_set_coupling(k)`, `osc_pixels()` |
| **Physarum** | 1024x1024 | up to 100,000 | sensor angle/dist, turn/move speed, deposit, decay | `physarum_init(count)`, `physarum_step(n)`, `physarum_set_params(...)`, `physarum_pixels()` |

### Shared Implementation Patterns

**No libc.** No `malloc`, no `printf`, no `math.h`. Each file provides its own `memset`. Trigonometry is 7th-order Taylor series. Square roots use Newton's method. Random numbers come from xorshift PRNGs.

**Static allocation.** All buffers are global arrays sized at compile time. No `memory.grow`, which means JS typed array views into WASM linear memory never invalidate.

**Toroidal boundaries.** Every grid wraps — modular arithmetic on all edge access. Boids and Physarum agents wrap their floating-point positions; Langton and Oscillators wrap grid indices.

**Double-buffering.** Physarum and Oscillators use read/write buffer pairs, swapped after each step, so the Laplacian/diffusion reads stable data while writing updates.

**Zero-copy pixel output.** Each `*_pixels()` function returns a pointer to a static RGBA buffer. JS wraps it in a `Uint8ClampedArray` view — no data crosses the boundary.

**Trail visualization.** Boids and Langton use heat/trail maps that decay over time, turning agent paths into glowing traces. Physarum's trail *is* the simulation state (agents read it to navigate). All four use warm-toned color ramps on dark backgrounds.

### Build

Requires LLVM with `wasm-ld`:
- **macOS:** `brew install llvm` (Xcode clang lacks `wasm-ld`)
- **Linux:** `apt install clang lld`

```bash
./build_macos.sh
```

The script auto-detects Homebrew LLVM on macOS. Override with `CC=/path/to/clang ./build_macos.sh`.

After building, commit the `.wasm` files — Fugue imports them directly.

### Fugue Integration

Git submodule at `assets/vendor/petri/`. LiveView hooks import from `js/*.js`, run `requestAnimationFrame` loops client-side. Each frame: `step(n)` then `getPixels()` then `putImageData`. No server round-trips for simulation state.

Canvas 2D rendering — single buffer blit per frame. No WebGL, no shaders.

### Design Decisions

**C, not Rust/Zig** — ~600 lines of arithmetic on flat arrays. No allocator, no strings, no error handling. C is the right tool.

**No Emscripten** — These simulations use zero libc functions. Direct `clang --target=wasm32` produces tiny binaries and ~18 lines of JS glue per sim vs. Emscripten's generated loader.

**Canvas 2D, not WebGPU** — Rendering is a single `putImageData`. WebGPU only helps if the simulation moves to a compute shader.

**Committed .wasm** — Anyone cloning Fugue can run without a C toolchain.

**One file per sim** — Each simulation is self-contained. No shared headers, no build dependencies between them. You can delete any sim without affecting the others.

## References

- Turing, A.M. (1952). "The Chemical Basis of Morphogenesis." *Phil. Trans. R. Soc. Lond. B*, 237(641), 37-72.
- Reynolds, C.W. (1987). "Flocks, Herds, and Schools: A Distributed Behavioral Model." *Computer Graphics*, 21(4), 25-34.
- Langton, C.G. (1986). "Studying Artificial Life with Cellular Automata." *Physica D*, 22(1-3), 120-149.
- Kuramoto, Y. (1975). "Self-Entrainment of a Population of Coupled Non-linear Oscillators." *International Symposium on Mathematical Problems in Theoretical Physics*, 420-422.
- Jones, J. (2010). "Characteristics of Pattern Formation and Evolution in Approximations of Physarum Transport Networks." *Artificial Life*, 16(2), 127-153.
