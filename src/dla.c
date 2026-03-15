//
// Diffusion-Limited Aggregation — WASM
// Random walkers stick to a growing crystal → fractal structures
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define W 1024
#define H 1024
#define N (W * H)
#define MAX_WALKERS 5000

static unsigned char stuck[N];    // 1 = part of crystal
static float age[N];              // when it stuck (for coloring)
static unsigned char pixels[N * 4];

static float walker_x[MAX_WALKERS];
static float walker_y[MAX_WALKERS];
static int num_walkers = 0;
static int total_stuck = 0;

static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

static int has_neighbor(int x, int y) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = (x + dx + W) % W;
            int ny = (y + dy + H) % H;
            if (stuck[ny * W + nx]) return 1;
        }
    }
    return 0;
}

static void spawn_walker(int i) {
    // spawn on a random edge
    int side = (int)(randf() * 4);
    if (side == 0) { walker_x[i] = randf() * W; walker_y[i] = 0; }
    else if (side == 1) { walker_x[i] = randf() * W; walker_y[i] = H - 1; }
    else if (side == 2) { walker_x[i] = 0; walker_y[i] = randf() * H; }
    else { walker_x[i] = W - 1; walker_y[i] = randf() * H; }
}

__attribute__((export_name("dla_init")))
void dla_init(int walkers) {
    if (walkers > MAX_WALKERS) walkers = MAX_WALKERS;
    num_walkers = walkers;
    total_stuck = 0;

    memset(stuck, 0, sizeof(stuck));
    memset(age, 0, sizeof(age));

    // seed: several points scattered around
    int seeds[][2] = {
        {W/2, H/2},
        {W/4, H/4}, {3*W/4, H/4},
        {W/4, 3*H/4}, {3*W/4, 3*H/4},
        {W/2, H/4}, {W/2, 3*H/4},
        {W/4, H/2}, {3*W/4, H/2}
    };
    for (int s = 0; s < 9; s++) {
        stuck[seeds[s][1] * W + seeds[s][0]] = 1;
        total_stuck++;
    }

    for (int i = 0; i < walkers; i++)
        spawn_walker(i);
}

__attribute__((export_name("dla_step")))
void dla_step(int steps) {
    float norm_stuck = (float)total_stuck / (float)N;
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < num_walkers; i++) {
            // random walk with larger steps
            walker_x[i] += (randf() - 0.5f) * 4.0f;
            walker_y[i] += (randf() - 0.5f) * 4.0f;

            // wrap
            if (walker_x[i] < 0) walker_x[i] += W;
            if (walker_x[i] >= W) walker_x[i] -= W;
            if (walker_y[i] < 0) walker_y[i] += H;
            if (walker_y[i] >= H) walker_y[i] -= H;

            int ix = (int)walker_x[i];
            int iy = (int)walker_y[i];

            if (has_neighbor(ix, iy)) {
                stuck[iy * W + ix] = 1;
                age[iy * W + ix] = norm_stuck;
                total_stuck++;
                norm_stuck = (float)total_stuck / (float)N;
                spawn_walker(i);
            }
        }
    }
}

__attribute__((export_name("dla_pixels")))
unsigned char* dla_pixels(void) {
    for (int i = 0; i < N; i++) {
        int pi = i * 4;
        if (stuck[i]) {
            float t = age[i];
            // color by age: early = bright, late = dimmer
            float bright = 1.0f - t * 0.6f;
            pixels[pi]     = (unsigned char)(bright * 230.0f);
            pixels[pi + 1] = (unsigned char)(bright * 200.0f);
            pixels[pi + 2] = (unsigned char)(bright * 160.0f);
        } else {
            pixels[pi]     = 8;
            pixels[pi + 1] = 6;
            pixels[pi + 2] = 10;
        }
        pixels[pi + 3] = 255;
    }
    return pixels;
}
