//
// Coupled Oscillators (Kuramoto model) — WASM
// Grid of phase oscillators that synchronize, producing traveling waves
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define W 512
#define H 512
#define N (W * H)

static float phase[N];       // phase angle [0, 2π)
static float freq[N];        // natural frequency
static float phase_new[N];
static unsigned char pixels[N * 4];

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
void osc_init(void) {
    for (int i = 0; i < N; i++) {
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
        for (int y = 0; y < H; y++) {
            int ym = (y - 1 + H) % H;
            int yp = (y + 1) % H;
            for (int x = 0; x < W; x++) {
                int xm = (x - 1 + W) % W;
                int xp = (x + 1) % W;
                int i = y * W + x;
                float p = phase[i];

                // Kuramoto coupling to 4 neighbors
                float sync = sinf_approx(phase[ym * W + x] - p)
                           + sinf_approx(phase[yp * W + x] - p)
                           + sinf_approx(phase[y * W + xm] - p)
                           + sinf_approx(phase[y * W + xp] - p);

                phase_new[i] = p + dt * (freq[i] + coupling * sync / 4.0f);

                // keep in [0, 2π)
                while (phase_new[i] >= 6.28318530f) phase_new[i] -= 6.28318530f;
                while (phase_new[i] < 0.0f) phase_new[i] += 6.28318530f;
            }
        }

        // copy new → current
        for (int i = 0; i < N; i++)
            phase[i] = phase_new[i];
    }
}

__attribute__((export_name("osc_pixels")))
unsigned char* osc_pixels(void) {
    for (int i = 0; i < N; i++) {
        // map phase to brightness (sin wave gives smooth pulsing)
        float t = (sinf_approx(phase[i]) + 1.0f) * 0.5f;

        int pi = i * 4;
        pixels[pi]     = 8 + (unsigned char)(t * 222.0f);
        pixels[pi + 1] = 6 + (unsigned char)(t * 194.0f);
        pixels[pi + 2] = 10 + (unsigned char)(t * 150.0f);
        pixels[pi + 3] = 255;
    }
    return pixels;
}
