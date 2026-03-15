//
// Coupled Oscillators (Kuramoto model) — WASM
// Grid of phase oscillators that synchronize, producing traveling waves
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W 2560
#define MAX_H 1440
#define MAX_N (MAX_W * MAX_H)

static int w = 512, h = 512, n = 512 * 512;

static float phase[MAX_N];       // phase angle [0, 2pi)
static float freq[MAX_N];        // natural frequency
static float phase_new[MAX_N];
static unsigned char intensity[MAX_N];

static float coupling = 0.3f;
static float dt = 0.05f;

static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

// Taylor sin
static float sinf_approx(float x) {
    while (x > 3.14159265f) x -= 6.28318530f;
    while (x < -3.14159265f) x += 6.28318530f;
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
}

static float cosf_approx(float x) {
    return sinf_approx(x + 1.57079632f);
}

__attribute__((export_name("osc_init")))
void osc_init(int width, int height) {
    if (width > MAX_W) width = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;

    for (int i = 0; i < n; i++) {
        phase[i] = randf() * 6.28318530f;
        // natural frequencies: slight variation around a base
        freq[i] = 1.0f + (randf() - 0.5f) * 0.4f;
    }
}

__attribute__((export_name("osc_set_coupling")))
void osc_set_coupling(float k) {
    coupling = k;
}

__attribute__((export_name("osc_step")))
void osc_step(int steps) {
    for (int s = 0; s < steps; s++) {
        for (int y = 0; y < h; y++) {
            int ym = (y - 1 + h) % h;
            int yp = (y + 1) % h;
            for (int x = 0; x < w; x++) {
                int xm = (x - 1 + w) % w;
                int xp = (x + 1) % w;
                int i = y * w + x;
                float p = phase[i];

                // Kuramoto coupling to 4 neighbors
                float sync = sinf_approx(phase[ym * w + x] - p)
                           + sinf_approx(phase[yp * w + x] - p)
                           + sinf_approx(phase[y * w + xm] - p)
                           + sinf_approx(phase[y * w + xp] - p);

                phase_new[i] = p + dt * (freq[i] + coupling * sync / 4.0f);

                // keep in [0, 2pi)
                while (phase_new[i] >= 6.28318530f) phase_new[i] -= 6.28318530f;
                while (phase_new[i] < 0.0f) phase_new[i] += 6.28318530f;
            }
        }

        // copy new -> current
        for (int i = 0; i < n; i++)
            phase[i] = phase_new[i];
    }
}

__attribute__((export_name("osc_pixels")))
unsigned char* osc_pixels(void) {
    for (int i = 0; i < n; i++) {
        float t = (sinf_approx(phase[i]) + 1.0f) * 0.5f;
        intensity[i] = (unsigned char)(t * 255.0f);
    }
    return intensity;
}
