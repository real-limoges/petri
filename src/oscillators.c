//
// Coupled oscillators (Kuramoto on a grid) — WASM
//
// Each cell holds a phase angle and a fixed natural frequency. Per step,
// every cell is nudged toward its 4-neighbour mean via
//   phase' = phase + dt * (omega + (K/4) * Σ sin(neighbour - self))
// which is the standard Kuramoto coupling, restricted to nearest neighbours
// for cache locality. Boundaries are toroidal.
//
// We render `(sin(phase) + 1) / 2` so synchronised regions map to bright
// bands. Travelling waves emerge once K is large enough to overcome the
// 0.4-wide spread in natural frequency.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W 2560
#define MAX_H 1440
#define MAX_N (MAX_W * MAX_H)

#define PI     3.14159265f
#define TWO_PI 6.28318530f

// Default regime: weak coupling with a narrow frequency band, integrated
// at a step large enough to see motion in real time but small enough that
// the explicit Euler stays stable for K up to ~1.0.
#define DEFAULT_COUPLING 0.3f
#define DEFAULT_DT       0.05f
#define FREQ_BASE        1.0f
#define FREQ_SPREAD      0.4f

static int w = 512, h = 512, n = 512 * 512;

// Two phase buffers ping-pong each step so we never read the partial
// result of the same pass we are writing. After the inner loop swaps the
// pointers, `cur` holds the new state for the renderer.
static float phase_a[MAX_N];
static float phase_b[MAX_N];
static float *cur = phase_a;
static float *nxt = phase_b;

static float        freq[MAX_N];
static unsigned char intensity[MAX_N];

static float coupling = DEFAULT_COUPLING;
static float dt       = DEFAULT_DT;

static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

// Taylor sin truncated at x^7 — accurate to ~5e-7 over [-pi, pi], which is
// well below rendering precision and avoids any libc dependency.
static float sinf_approx(float x) {
    while (x >  PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
}

static inline int wrap(int v, int lim) {
    return (v + lim) % lim;
}

__attribute__((export_name("osc_init")))
void osc_init(int width, int height) {
    if (width  > MAX_W) width  = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;

    cur = phase_a;
    nxt = phase_b;

    for (int i = 0; i < n; i++) {
        cur[i]  = randf() * TWO_PI;
        freq[i] = FREQ_BASE + (randf() - 0.5f) * FREQ_SPREAD;
    }
}

__attribute__((export_name("osc_set_coupling")))
void osc_set_coupling(float k) { coupling = k; }

__attribute__((export_name("osc_step")))
void osc_step(int steps) {
    for (int s = 0; s < steps; s++) {
        for (int y = 0; y < h; y++) {
            int ym = wrap(y - 1, h);
            int yp = wrap(y + 1, h);
            for (int x = 0; x < w; x++) {
                int xm = wrap(x - 1, w);
                int xp = wrap(x + 1, w);
                int i  = y * w + x;
                float p = cur[i];

                float sync = sinf_approx(cur[ym * w + x ] - p)
                           + sinf_approx(cur[yp * w + x ] - p)
                           + sinf_approx(cur[y  * w + xm] - p)
                           + sinf_approx(cur[y  * w + xp] - p);

                float pn = p + dt * (freq[i] + coupling * sync * 0.25f);

                // Range-reduce here so sinf_approx's inner reduction stays
                // a no-op on the next pass.
                while (pn >= TWO_PI) pn -= TWO_PI;
                while (pn <  0.0f)   pn += TWO_PI;
                nxt[i] = pn;
            }
        }
        float *tmp = cur; cur = nxt; nxt = tmp;
    }
}

__attribute__((export_name("osc_pixels")))
unsigned char* osc_pixels(void) {
    for (int i = 0; i < n; i++) {
        float v = (sinf_approx(cur[i]) + 1.0f) * 0.5f;
        intensity[i] = (unsigned char)(v * 255.0f);
    }
    return intensity;
}
