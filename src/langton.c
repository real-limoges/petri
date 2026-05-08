//
// Multiple Langton's Ants — WASM
//
// Each ant runs the same two-state machine (turn right on white, turn left
// on black, flipping the cell as it goes). One ant escapes its chaotic
// regime after ~10k steps and lays down a diagonal "highway" — an emergent
// invariant of a 4-bit transition table.
//
// We track per-cell visit frequency in `heat[]` and map that to a warm
// gradient. The grid itself is binary; the visual interest is in the heat.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W    2560
#define MAX_H    1440
#define MAX_N    (MAX_W * MAX_H)
#define MAX_ANTS 12

// Direction encoding: 0=up, 1=right, 2=down, 3=left. Right-turn is +1,
// left-turn is +3 (mod 4). The dx/dy tables below match this encoding.
static const int DIR_DX[4] = {  0,  1,  0, -1 };
static const int DIR_DY[4] = { -1,  0,  1,  0 };

// Heat tunables. Each visit deposits HEAT_GAIN onto a cell, capped at
// HEAT_CAP so a long-running highway saturates rather than blooming. The
// per-frame decay nudges abandoned regions back toward black slowly enough
// that the highway trail stays legible for thousands of frames.
#define HEAT_GAIN  0.3f
#define HEAT_CAP   10.0f
#define HEAT_DECAY 0.9999f

static int w = 1024, h = 1024, n = 1024 * 1024;

static unsigned char grid[MAX_N];       // 0 = white, 1 = black
static float         heat[MAX_N];       // per-cell visit count, decayed
static unsigned char intensity[MAX_N];  // packed render output

static int ant_x[MAX_ANTS];
static int ant_y[MAX_ANTS];
static int ant_dir[MAX_ANTS];
static int num_ants = 0;

static inline int wrap(int v, int lim) {
    // `v` is at most one step out of range, so `+lim` then `%lim` is enough
    // to bring negatives back without a branch.
    return (v + lim) % lim;
}

__attribute__((export_name("langton_init")))
void langton_init(int count, int width, int height) {
    if (width  > MAX_W) width  = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;

    if (count > MAX_ANTS) count = MAX_ANTS;
    num_ants = count;
    memset(grid, 0, n);
    memset(heat, 0, n * sizeof(float));

    // Spread ants symmetrically around the grid centre with a y-jitter so
    // the initial chaotic phase doesn't collapse into a single highway.
    // The wrap() in langton_step pulls any out-of-range start back inside.
    for (int i = 0; i < count; i++) {
        ant_x[i]   = wrap(w / 2 + (i - count / 2) * (w / (count + 1)), w);
        ant_y[i]   = wrap(h / 2 + ((i % 3) - 1) * (h / 4), h);
        ant_dir[i] = i % 4;
    }
}

__attribute__((export_name("langton_step")))
void langton_step(int steps) {
    for (int s = 0; s < steps; s++) {
        for (int a = 0; a < num_ants; a++) {
            int idx = ant_y[a] * w + ant_x[a];

            // Langton's rule: turn right on white (and blacken), left on
            // black (and whiten). +1 / +3 mod 4 in the encoding above.
            if (grid[idx] == 0) {
                ant_dir[a] = (ant_dir[a] + 1) & 3;
                grid[idx]  = 1;
            } else {
                ant_dir[a] = (ant_dir[a] + 3) & 3;
                grid[idx]  = 0;
            }

            heat[idx] += HEAT_GAIN;
            if (heat[idx] > HEAT_CAP) heat[idx] = HEAT_CAP;

            int d = ant_dir[a];
            ant_x[a] = wrap(ant_x[a] + DIR_DX[d], w);
            ant_y[a] = wrap(ant_y[a] + DIR_DY[d], h);
        }
    }
}

__attribute__((export_name("langton_pixels")))
unsigned char* langton_pixels(void) {
    const float scale = 255.0f / HEAT_CAP;
    for (int i = 0; i < n; i++) {
        heat[i] *= HEAT_DECAY;
        float v = heat[i] * scale;
        if (v > 255.0f) v = 255.0f;
        intensity[i] = (unsigned char)v;
    }
    return intensity;
}
