//
// Lenia — continuous cellular automata — WASM
// Smooth Game of Life producing organic gliders and blobs
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define W 512
#define H 512
#define N (W * H)
#define KERNEL_R 13

static float grid_a[N], grid_b[N];
static float *grid_read = grid_a, *grid_write = grid_b;
static unsigned char pixels[N * 4];

// precomputed kernel
#define KERNEL_SIZE ((2 * KERNEL_R + 1) * (2 * KERNEL_R + 1))
static float kernel[KERNEL_SIZE];
static int kernel_dx[KERNEL_SIZE];
static int kernel_dy[KERNEL_SIZE];
static int kernel_count = 0;
static float kernel_sum = 0;

// params
static float mu = 0.15f;    // growth center
static float sigma = 0.017f; // growth width
static float dt = 0.1f;

static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

static float expf_approx(float x) {
    if (x < -10.0f) return 0.0f;
    // Pade approximant: e^x ≈ (1 + x/2 + x^2/12) / (1 - x/2 + x^2/12)
    // chain for better range
    int neg = 0;
    if (x < 0) { neg = 1; x = -x; }
    float result = 1.0f;
    while (x > 1.0f) { result *= 2.71828182f; x -= 1.0f; }
    // Taylor for remainder
    float x2 = x * x;
    float x3 = x2 * x;
    float x4 = x3 * x;
    float r = 1.0f + x + x2/2.0f + x3/6.0f + x4/24.0f;
    result *= r;
    return neg ? 1.0f / result : result;
}

static float sqrtf_approx(float x) {
    if (x <= 0) return 0;
    float guess = x * 0.5f;
    for (int i = 0; i < 6; i++)
        guess = (guess + x / guess) * 0.5f;
    return guess;
}

// bell-shaped growth function
static float growth(float u) {
    float d = (u - mu) / sigma;
    return 2.0f * expf_approx(-0.5f * d * d) - 1.0f;
}

// ring-shaped kernel
static void build_kernel(void) {
    kernel_count = 0;
    kernel_sum = 0;
    float peak_r = KERNEL_R * 0.5f;
    float kr_sigma = KERNEL_R * 0.23f;
    for (int dy = -KERNEL_R; dy <= KERNEL_R; dy++) {
        for (int dx = -KERNEL_R; dx <= KERNEL_R; dx++) {
            float r = sqrtf_approx((float)(dx*dx + dy*dy));
            if (r <= KERNEL_R) {
                float d = (r - peak_r) / kr_sigma;
                float w = expf_approx(-0.5f * d * d);
                kernel_dx[kernel_count] = dx;
                kernel_dy[kernel_count] = dy;
                kernel[kernel_count] = w;
                kernel_sum += w;
                kernel_count++;
            }
        }
    }
}

__attribute__((export_name("lenia_init")))
void lenia_init(void) {
    build_kernel();
    memset(grid_a, 0, sizeof(grid_a));
    memset(grid_b, 0, sizeof(grid_b));
    grid_read = grid_a;
    grid_write = grid_b;

    // seed multiple blobs
    for (int b = 0; b < 20; b++) {
        int cx = 40 + (int)(randf() * (W - 80));
        int cy = 40 + (int)(randf() * (H - 80));
        int r = 15 + (int)(randf() * 20);
        int r2 = r * r;
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx*dx + dy*dy <= r2) {
                    int x = (cx + dx + W) % W;
                    int y = (cy + dy + H) % H;
                    grid_a[y * W + x] = 0.5f + randf() * 0.5f;
                }
            }
        }
    }
}

__attribute__((export_name("lenia_step")))
void lenia_step(int steps) {
    for (int s = 0; s < steps; s++) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float potential = 0;
                for (int k = 0; k < kernel_count; k++) {
                    int nx = (x + kernel_dx[k] + W) % W;
                    int ny = (y + kernel_dy[k] + H) % H;
                    potential += kernel[k] * grid_read[ny * W + nx];
                }
                potential /= kernel_sum;

                float g = growth(potential);
                float val = grid_read[y * W + x] + dt * g;
                if (val < 0.0f) val = 0.0f;
                if (val > 1.0f) val = 1.0f;
                grid_write[y * W + x] = val;
            }
        }
        float *tmp = grid_read;
        grid_read = grid_write;
        grid_write = tmp;
    }
}

__attribute__((export_name("lenia_pixels")))
unsigned char* lenia_pixels(void) {
    for (int i = 0; i < N; i++) {
        float t = grid_read[i];

        int pi = i * 4;
        pixels[pi]     = 8 + (unsigned char)(t * 222.0f);
        pixels[pi + 1] = 6 + (unsigned char)(t * 194.0f);
        pixels[pi + 2] = 10 + (unsigned char)(t * 150.0f);
        pixels[pi + 3] = 255;
    }
    return pixels;
}
