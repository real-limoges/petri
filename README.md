# Petri

Reaction-diffusion pattern formation as a visual computational system. Part of [Fugue](https://github.com/real-limoges/fugue).

Petri simulates the [Gray-Scott model](https://groups.csail.mit.edu/mac/projects/amorphous/GrayScott/) — two virtual chemicals diffusing across a 2D grid, feeding and killing each other according to simple local rules. From these rules, complex spatial patterns emerge spontaneously: spots that self-replicate, stripes that branch, spirals that pulse, coral-like structures that grow and die.

The entire behavioral repertoire is controlled by two parameters: **feed rate** (F) and **kill rate** (k). Different regions of the (F, k) parameter space produce qualitatively different emergent phenomena — Petri makes this space navigable and visual.

Petri also serves as the living background for the [realcomplex.systems](http://realcomplex.systems) landing page. Visitors see emergence happening before they read a word.

## Why

Petri exists to demonstrate emergence concretely: the claim that complex, structured behavior can arise from simple, local interactions is easy to state and hard to believe until you watch it happen. Reaction-diffusion is one of the cleanest examples in mathematics — Alan Turing proposed it in 1952 as the mechanism behind biological pattern formation (animal coats, shell pigmentation, digit spacing), and the Gray-Scott model distills it to two PDEs and two parameters.

Within Fugue, Petri is the most direct embodiment of the project's core thesis. Where Bloom shows emergence in graph structure and Funktor in harmonic sequence, Petri shows it in physical space: pattern from substrate, signal from noise.

## The Model

The Gray-Scott system tracks concentrations of two chemicals, U and V, on a discrete grid:

```
∂U/∂t = Dᵤ∇²U - UV² + F(1 - U)
∂V/∂t = Dᵥ∇²V + UV² - (F + k)V
```

Where:
- **U, V** — concentrations of the two species at each grid cell (0.0–1.0)
- **Dᵤ, Dᵥ** — diffusion rates (Dᵤ = 0.16, Dᵥ = 0.08; V diffuses slower)
- **F** — feed rate: how quickly U is replenished from an external reservoir
- **k** — kill rate: how quickly V decays
- **∇²** — discrete Laplacian (3×3 weighted stencil)
- **UV²** — the autocatalytic reaction term: V converts U into more V

The simulation loop: compute Laplacian for both grids → apply reaction terms → clamp values → swap buffers → render.

### Parameter Space

The (F, k) plane contains distinct behavioral regimes, roughly:

| Region | F range | k range | Behavior |
|--------|---------|---------|----------|
| Uniform | high F | high k | V dies out, solid U field |
| Spots | ~0.03 | ~0.06 | Self-replicating solitons |
| Stripes | ~0.04 | ~0.06 | Labyrinthine/fingerprint patterns |
| Waves | ~0.01 | ~0.05 | Pulsing, oscillatory fronts |
| Coral | ~0.06 | ~0.06 | Branching, organic growth |
| Mitosis | ~0.03 | ~0.065 | Spots that split |
| Chaos | low F | low k | Turbulent, unstable mixing |

These boundaries are approximate — the transitions between regimes are themselves interesting (gradients, thresholds).

## Architecture

### Language: C → WebAssembly

Petri is freestanding C compiled directly to WASM with `clang --target=wasm32-unknown-unknown`. No Emscripten, no libc, no runtime. The simulation operates on flat arrays using only arithmetic — it doesn't need a POSIX environment, a memory allocator, or any library functions. The entire build is a single clang invocation.

At 512×512 with 6 steps per animation frame, Petri performs ~9.4 million cell updates per second. C/WASM handles this comfortably. WASM SIMD is available via `wasm_simd128.h` for headroom at higher resolutions, but is not required at 512×512.

### Repository Structure

Petri is an independent repo consumed by Fugue as a git submodule (same pattern as [Bloom](https://github.com/real-limoges/bloom)). The compiled `.wasm` binary is committed so that Fugue can consume it without requiring a C toolchain.

```
petri/
├── src/
│   └── petri.c         # the entire simulation (~300 lines)
├── js/
│   └── petri.js        # JS loader/glue — WASM init, memory view, exports
├── petri.wasm          # build output (committed)
├── build.sh            # single clang invocation
├── README.md
├── CLAUDE.md
└── .github/
    └── workflows/
        └── build.yml   # CI: build WASM, verify output
```

One C file. One JS file. One WASM binary. The toolchain complexity is near zero.

### WASM Exports

```c
void  petri_init(void);                // initialize grids, seed default pattern
void  petri_set_params(float f, float k);  // update feed/kill rates (live, no restart)
void  petri_step(int n);               // advance simulation by n steps
unsigned char* petri_pixels(void);     // pointer to RGBA pixel buffer
void  petri_seed(int pattern);         // reset grid: 0=center, 1=multi, 2=ring, 3=random
```

Five exported functions. No strings cross the WASM boundary — seed patterns are identified by integer. The pixel pointer is stable (all memory is statically allocated). JS reads directly from WASM linear memory with zero copy.

### Memory Layout

All buffers are statically allocated in WASM linear memory. No malloc, no dynamic allocation, no memory growth.

```
WASM Linear Memory (8 MB, --initial-memory=8388608)
┌──────────────────────────────┐  0x000000
│  Grid U (read)   512×512×f32 │  1,048,576 bytes
├──────────────────────────────┤  0x100000
│  Grid U (write)  512×512×f32 │  1,048,576 bytes
├──────────────────────────────┤  0x200000
│  Grid V (read)   512×512×f32 │  1,048,576 bytes
├──────────────────────────────┤  0x300000
│  Grid V (write)  512×512×f32 │  1,048,576 bytes
├──────────────────────────────┤  0x400000
│  Pixels RGBA     512×512×u8  │  1,048,576 bytes
├──────────────────────────────┤  0x500000
│  (unused)                    │  3,145,728 bytes
└──────────────────────────────┘  0x800000
```

Double-buffered grids: the Laplacian reads from one buffer while the reaction step writes to the other. After each step, read and write pointers swap. This ensures the update is order-independent.

### Data Flow

```
 cursor position (x, y)
        │
        │  JS maps to (F, k) in parameter space
        ▼
 ┌──────────────────────────────────────────────┐
 │  Browser                                      │
 │                                               │
 │  petri_hook.js (Fugue side)                   │
 │    ├─ mounted():  load WASM, start rAF loop   │
 │    ├─ rAF loop:   step(6) → pixels() → blit   │
 │    ├─ mousemove:  set_params(f, k)             │
 │    └─ destroyed(): cancel rAF                  │
 │                                               │
 │  Canvas 2D ← ImageData ← WASM linear memory   │
 └──────────────────────────────────────────────┘
        ▲
        │  LiveView serves page + navigation
        │  No server round-trip for sim params
        │
 ┌──────────────┐
 │  Fugue       │
 │  LiveView    │
 │  (Elixir)    │
 └──────────────┘
```

On the landing page, the parameter interaction is entirely client-side — mouse position maps to (F, k) in the JS hook, no Phoenix channel traffic. LiveView's role is routing and serving the page shell.

### JS Loader

The JS glue module handles WASM instantiation and exposes a clean interface for the Fugue hook:

```javascript
// js/petri.js
let wasm, memory;

export async function init() {
  const resp = await fetch('/vendor/petri/petri.wasm');
  const bytes = await resp.arrayBuffer();
  const { instance } = await WebAssembly.instantiate(bytes);
  wasm = instance.exports;
  memory = wasm.memory;
  wasm.petri_init();
}

export function step(n) { wasm.petri_step(n); }
export function setParams(f, k) { wasm.petri_set_params(f, k); }
export function seed(pattern) { wasm.petri_seed(pattern); }

export function getPixels() {
  const ptr = wasm.petri_pixels();
  return new Uint8ClampedArray(memory.buffer, ptr, 512 * 512 * 4);
}
```

`getPixels()` returns a view into WASM linear memory — no copy, no serialization. JS wraps it in an `ImageData` and blits to canvas with `putImageData`.

### Rendering: Canvas 2D

WASM writes RGBA values into a pre-allocated buffer. JS reads them through a typed array view and calls `putImageData` each frame. At 512×512 this is a 1MB blit — trivial for any modern browser.

No WebGPU, no WebGL, no shaders. The simulation is CPU-bound (in WASM), and the rendering is a single buffer copy.

### Integration with Fugue

Petri is a git submodule at `assets/vendor/petri/`. The Fugue-side hook:

```javascript
// assets/js/hooks/petri_hook.js
import { init, step, setParams, getPixels, seed } from "../../vendor/petri/js/petri.js";

export const PetriHook = {
  async mounted() {
    await init();
    seed(1); // multi-point seeding for landing page
    this.canvas = this.el.querySelector("canvas");
    this.ctx = this.canvas.getContext("2d");

    this.onMove = (e) => {
      const x = e.clientX / window.innerWidth;
      const y = e.clientY / window.innerHeight;
      setParams(0.02 + x * 0.04, 0.045 + y * 0.025);
    };
    window.addEventListener("mousemove", this.onMove);

    this.loop = () => {
      step(6);
      const img = new ImageData(getPixels(), 512, 512);
      this.ctx.putImageData(img, 0, 0);
      this.raf = requestAnimationFrame(this.loop);
    };
    this.raf = requestAnimationFrame(this.loop);
  },

  destroyed() {
    cancelAnimationFrame(this.raf);
    window.removeEventListener("mousemove", this.onMove);
  }
};
```

## UI: Landing Page

The simulation runs full-viewport as a living background. Text and navigation float above it. The visitor's cursor silently controls the chemistry — moving across the page shifts (F, k) and reshapes the patterns. A subtle hint appears after a few seconds for those who notice the patterns responding to their movement.

Below the fold: the six Fugue services as a grid, each linking to its own route.

For a dedicated `/petri` route (future): the full parameter map UI — 2D (F, k) plane, parameter readout, seed selection, colormap picker, pause/play.

## Build

```bash
# Prerequisites: clang with wasm32 target support
# On macOS: comes with Xcode command line tools
# On Linux: apt install clang lld

# Build
./build.sh

# Or manually:
clang \
  --target=wasm32-unknown-unknown \
  -O2 \
  -flto \
  -nostdlib \
  -Wl,--no-entry \
  -Wl,--export-dynamic \
  -Wl,--initial-memory=8388608 \
  -o petri.wasm \
  src/petri.c

# With SIMD:
clang \
  --target=wasm32-unknown-unknown \
  -O2 \
  -flto \
  -nostdlib \
  -msimd128 \
  -Wl,--no-entry \
  -Wl,--export-dynamic \
  -Wl,--initial-memory=8388608 \
  -o petri.wasm \
  src/petri.c

# After building, commit petri.wasm so Fugue can consume it
git add petri.wasm && git commit -m "rebuild wasm"
```

### Adding to Fugue

```bash
# From the Fugue repo root
git submodule add https://github.com/real-limoges/petri assets/vendor/petri
git submodule update --init --recursive

# Register the hook in assets/js/app.js
# import { PetriHook } from "./hooks/petri_hook"
# hooks: { ..., PetriHook }
```

## Design Decisions

**Why C?** Petri is ~300 lines of arithmetic on flat arrays. It doesn't need a borrow checker, a type system, or a runtime. C is the natural language for this kind of work — minimal abstraction, maximal clarity about what the machine is doing. It also adds genuine language diversity to the Fugue stack (Elixir, Haskell, Rust, Julia, C) rather than doubling up on Rust.

**Why not Emscripten?** Emscripten provides a POSIX-like environment — filesystem, malloc, printf, libc shims. Petri uses none of these. Compiling with `clang --target=wasm32` directly produces a smaller binary, a simpler build, and no unnecessary runtime. The JS glue is 15 lines instead of a generated 2000-line loader.

**Why Canvas 2D instead of WebGPU?** Petri's rendering is a single buffer blit — there's no geometry, no camera, no scene graph. Canvas 2D does this in one call. WebGPU would only help if the simulation moved to a compute shader, which is a different project.

**Why commit petri.wasm?** Same rationale as Bloom's `pkg/`: anyone cloning Fugue can run it without installing a C toolchain. The WASM binary is small (~15-30KB). The tradeoff is worth the developer experience.

**Why static memory allocation?** The grid dimensions are known at compile time. All five buffers (U read, U write, V read, V write, pixels) can be declared as global arrays with fixed sizes. This eliminates the need for malloc, prevents memory growth (which would invalidate JS typed array views), and makes the memory layout inspectable and predictable.

**Why int instead of string for seed patterns?** Without libc there's no strcmp. Passing strings across the WASM boundary requires encoding length or null-terminating in shared memory. For four options, an int enum is simpler and faster.

## Cross-Service Connections (Aspirational)

These are not implemented — they represent how Petri could participate in Fugue's eventual inter-service communication:

- **Ish → Petri:** Mood state maps to a region in (F, k) space. Anxiety → high-F chaotic regime. Calm → stable stripe equilibrium. Depression → low-F uniform decay. The grid becomes a visual correlate of internal state.
- **Petri → Bloom:** Pattern features (spatial frequency, symmetry, entropy) could parameterize a Bloom graph query — high-entropy patterns trigger exploration of "chaos theory" or "turbulence" Wikipedia subgraphs.
- **Petri → Funktor:** Spatial frequency of the current pattern maps to harmonic intervals or rhythmic density in the jazz generator.
- **Chirplet → Petri:** Birdsong spectral features seed the initial grid conditions — frequency content becomes spatial pattern.

## References

- Pearson, J.E. (1993). "Complex Patterns in a Simple System." *Science*, 261(5118), 189–192.
- Turing, A.M. (1952). "The Chemical Basis of Morphogenesis." *Phil. Trans. R. Soc. Lond. B*, 237(641), 37–72.
- [Robert Munafo's Gray-Scott Explorer](https://mrob.com/pub/comp/xmorphia/) — definitive parameter space catalog
- [Karl Sims' Reaction-Diffusion Tutorial](https://www.karlsims.com/rd.html) — accessible visual introduction