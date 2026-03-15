# Petri

Gray-Scott reaction-diffusion simulator. Freestanding C compiled to WASM with clang — no Emscripten, no libc, no runtime. Part of [Fugue](https://github.com/real-limoges/fugue).

Powers the [realcomplex.systems](http://realcomplex.systems) landing page as a full-viewport background where cursor position maps to simulation parameters.

## The Model

Two chemicals (U, V) on a 512×512 toroidal grid:

```
∂U/∂t = Dᵤ∇²U - UV² + F(1 - U)
∂V/∂t = Dᵥ∇²V + UV² - (F + k)V
```

- **Dᵤ = 0.16, Dᵥ = 0.08** — diffusion rates
- **F** — feed rate (default 0.055)
- **k** — kill rate (default 0.062)
- **∇²** — discrete Laplacian via 3×3 weighted stencil (corners 0.05, edges 0.2, center -1.0)
- **dt = 1.0** (implicit)

Six steps per animation frame. ~9.4M cell updates per frame at 60fps.

### Parameter Space

| Region | F | k | Behavior |
|--------|---|---|----------|
| Spots | ~0.03 | ~0.06 | Self-replicating solitons |
| Stripes | ~0.04 | ~0.06 | Labyrinthine patterns |
| Waves | ~0.01 | ~0.05 | Pulsing fronts |
| Coral | ~0.06 | ~0.06 | Branching growth |
| Mitosis | ~0.03 | ~0.065 | Splitting spots |
| Chaos | low | low | Turbulent mixing |

## Architecture

```
petri/
├── src/petri.c      # simulation, colormap, seeding (~230 lines)
├── js/petri.js      # WASM loader + export wrappers
├── petri.wasm       # build output (committed, ~3.5 KB)
├── build.sh         # single clang invocation
├── docs/
│   ├── IMPLEMENTATION_GUIDE.md
│   └── gray-scott-theory.tex
├── CLAUDE.md
└── README.md
```

One C file. One JS file. One WASM binary.

### WASM Exports

```c
void  petri_init(void);                    // seed with pattern 1 (multi)
void  petri_set_params(float f, float k);  // live parameter update
void  petri_step(int n);                   // advance n simulation steps
unsigned char* petri_pixels(void);         // pointer to RGBA pixel buffer
void  petri_seed(int pattern);             // 0=center, 1=multi, 2=ring, 3=random
```

No strings cross the WASM boundary. The pixel pointer is stable — all memory is statically allocated. JS reads WASM linear memory directly via `Uint8ClampedArray` view (zero copy).

### Memory Layout

All buffers are global arrays. No malloc, no `memory.grow`.

```
WASM Linear Memory (8 MB)
┌──────────────────────────────┐  0x000000
│  Grid U (read)   512×512×f32 │  1 MB
├──────────────────────────────┤
│  Grid U (write)  512×512×f32 │  1 MB
├──────────────────────────────┤
│  Grid V (read)   512×512×f32 │  1 MB
├──────────────────────────────┤
│  Grid V (write)  512×512×f32 │  1 MB
├──────────────────────────────┤
│  Pixels RGBA     512×512×u8  │  1 MB
├──────────────────────────────┤
│  (stack + headroom)          │  3 MB
└──────────────────────────────┘
```

Double-buffered: Laplacian reads from one buffer, reaction writes to the other. Pointers swap after each step.

### Data Flow

```
cursor (x, y) → JS maps to (F, k) → petri_set_params()
                                          │
rAF loop: petri_step(6) → petri_pixels() → Uint8ClampedArray → ImageData → putImageData
```

Rendering is Canvas 2D — single 1MB buffer blit per frame. No WebGL, no shaders.

### Fugue Integration

Git submodule at `assets/vendor/petri/`. LiveView hook imports `js/petri.js`, runs the rAF loop client-side. No server round-trips for simulation state.

## Build

Requires LLVM with `wasm-ld`:
- **macOS:** `brew install llvm` (Xcode clang lacks `wasm-ld`)
- **Linux:** `apt install clang lld`

```bash
./build_macos.sh
```

`build.sh` auto-detects Homebrew LLVM on macOS. Override with `CC=/path/to/clang ./build.sh`.

After building, commit `petri.wasm` — Fugue imports it directly.

## Design Decisions

**C, not Rust/Zig** — ~230 lines of arithmetic on flat arrays. No allocator, no strings, no error handling. C is the right tool.

**No Emscripten** — Petri uses zero libc functions. Direct `clang --target=wasm32` produces a 3.5 KB binary and 15 lines of JS glue vs. Emscripten's 2000-line generated loader.

**Canvas 2D, not WebGPU** — Rendering is a single `putImageData`. WebGPU only helps if the simulation moves to a compute shader.

**Committed .wasm** — Anyone cloning Fugue can run without a C toolchain.

**Static allocation** — Grid dimensions are compile-time constants. Global arrays eliminate malloc and prevent `memory.grow` (which would invalidate JS typed array views).

**Int seed patterns, not strings** — No libc means no `strcmp`. Four patterns, four integers.

## References

- Pearson, J.E. (1993). "Complex Patterns in a Simple System." *Science*, 261(5118), 189–192.
- Turing, A.M. (1952). "The Chemical Basis of Morphogenesis." *Phil. Trans. R. Soc. Lond. B*, 237(641), 37–72.
- [Robert Munafo's Gray-Scott Explorer](https://mrob.com/pub/comp/xmorphia/)
- [Karl Sims' Reaction-Diffusion Tutorial](https://www.karlsims.com/rd.html)
