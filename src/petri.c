//
// Created by Real on 3/13/26.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

#define W 512
#define H 512
#define N (W * H)

#define DU 0.16f
#define DV 0.08f

// buffers
static float u_a[N], u_b[N];
static float v_a[N], v_b[N];
static unsigned char pixels[N * 4];

static float *u_read = u_a, *u_write = u_b;
static float *v_read = v_a, *v_write = v_b;

// params
static float param_f = 0.055f;
static float param_k = 0.062f;

// not using libc, need rand(). not cryptographically secure
static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

__attribute__((export_name("petri_seed")))
void petri_seed(int pattern) {
    for (int i = 0; i < N; i++) {
        u_a[i] = 1.0f;
        v_a[i] = 0.0f;
    }
    u_read = u_a; u_write = u_b;
    v_read = v_a; v_write = v_b;

    // square in the center
    if (pattern == 0) {
        int cx = W / 2, cy = H / 2;
        for (int dy = -10; dy < 10; dy++) {
            for (int dx = -10; dx < 10; dx++) {
                int idx = (cy + dy) * W + (cx + dx);
                u_a[idx] = 0.5f + randf() * 0.02f - 0.01f;
                v_a[idx] = 0.25f + randf() * 0.02f - 0.01f;
            }
        }
    }

    // rings - this is the default for the site
    else if (pattern == 1) {
        struct { int cx, cy, r; } circles[] = {
            {200, 150, 25},
            {350, 300, 20},
            {100, 350, 15},
            {400, 100, 30},
            {250, 400, 18}
        };
        for (int c = 0; c < 5; c++) {
            int cx = circles[c].cx, cy = circles[c].cy, r = circles[c].r;
            int r2 = r * r;
            for (int dy = -r; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (dx * dx + dy * dy <= r2) {
                        int x = cx + dx, y = cy + dy;
                        if (x >= 0 && x < W && y >= 0 && y < H) {
                            int idx = y * W + x;
                            u_a[idx] = 0.5f + randf() * 0.02f - 0.01f;
                            v_a[idx] = 0.25f + randf() * 0.02f - 0.01f;
                        }
                    }
                }
            }
        }
    }

    // annular ring
    else if (pattern == 2) {
        int cx = W / 2, cy = H / 2;
        int inner_r2 = 60 * 60, outer_r2 = 80 * 80;
        for (int y = cy - 80; y <= cy + 80; y++) {
            for (int x = cx - 80; x <= cx + 80; x++) {
                int dx = x - cx, dy = y - cy;
                int d2 = dx * dx + dy * dy;
                if (d2 >= inner_r2 && d2 <= outer_r2) {
                    if (x >= 0 && x < W && y >= 0 && y < H) {
                        int idx = y * W + x;
                        u_a[idx] = 0.5f + randf() * 0.02f - 0.01f;
                        v_a[idx] = 0.25f + randf() * 0.02f - 0.01f;
                    }
                }
            }
        }
    }

    // squares
    else if (pattern == 3) {
        for (int s = 0; s < 12; s++) {
            int sx = (int)(randf() * (W - 20));
            int sy = (int)(randf() * (H - 20));
            int size = 8 + (int)(randf() * 12);  // 8 to 20
            for (int dy = 0; dy < size; dy++) {
                for (int dx = 0; dx < size; dx++) {
                    int idx = (sy + dy) * W + (sx + dx);
                    u_a[idx] = 0.5f + randf() * 0.02f - 0.01f;
                    v_a[idx] = 0.25f + randf() * 0.02f - 0.01f;
                }
            }
        }
    }
}

__attribute__((export_name("petri_set_params")))
void petri_set_params(float f, float k) {
    param_f = f;
    param_k = k;
}

static const float stencil[9] = {
    0.05f, 0.2f, 0.05f,
    0.2f, -1.0f, 0.2f,
    0.05f, 0.2f, 0.05f
};

// hint to LLVM to inline this
static inline float lap3x3(const float *buf, const int idx[9]) {
    float sum = 0.0f;
    for (int i = 0; i < 9; i++)
        sum += stencil[i] * buf[idx[i]];
    return sum;
}

// hot path
__attribute__((export_name("petri_step")))
void petri_step(int n) {
    for (int s = 0; s < n; s++) {
        for (int y = 0; y < H; y++) {
            int ym = (y - 1 + H) % H;
            int yp = (y + 1) % H;
            int row_ym = ym * W;
            int row_y = y * W;
            int row_yp = yp * W;

            for (int x = 0; x < W; x++) {
                int xm = (x - 1 + W) % W;
                int xp = (x + 1) % W;

                int i = row_y + x;
                float u = u_read[i];
                float v = v_read[i];

                int idx[9] = {
                    row_ym + xm, row_ym + x, row_ym + xp,
                    row_y  + xm, row_y  + x, row_y  + xp,
                    row_yp + xm, row_yp + x, row_yp + xp
                };

                float lap_u = lap3x3(u_read, idx);
                float lap_v = lap3x3(v_read, idx);

                // the actual reaction
                float uvv = u * v * v;
                float u_new = u + DU * lap_u - uvv + param_f * (1.0f - u);
                float v_new = v + DV * lap_v + uvv - (param_f + param_k) * v;

                // clamp
                u_write[i] = u_new < 0.0f ? 0.0f : (u_new > 1.0f ? 1.0f : u_new);
                v_write[i] = v_new < 0.0f ? 0.0f : (v_new > 1.0f ? 1.0f : v_new);
            }
        }

        // swap read and write
        float *tmp;
        tmp = u_read; u_read = u_write; u_write = tmp;
        tmp = v_read; v_read = v_write; v_write = tmp;
    }
}

__attribute__((export_name("petri_pixels")))
unsigned char* petri_pixels(void) {
    static const float stops[] = {0.0f, 0.15f, 0.35f, 0.65f, 1.0f};
    static const unsigned char colors[][3] = {
        {  8,   6,  10},
        { 30,  14,  16},
        { 80,  20,   8},
        {185,  86,  42},
        {235, 200, 110}
    };

    for (int i = 0; i < N; i++) {
        float t = v_read[i] * 2.8f;
        if (t > 1.0f) t = 1.0f;

        int seg = 3;
        if (t < stops[1])       seg = 0;
        else if (t < stops[2])  seg = 1;
        else if (t < stops[3])  seg = 2;

        float frac = (t - stops[seg]) / (stops[seg + 1] - stops[seg]);

        int pi = i * 4;
        pixels[pi]     = colors[seg][0] + (unsigned char)(frac * (float)(colors[seg + 1][0] - colors[seg][0]));
        pixels[pi + 1] = colors[seg][1] + (unsigned char)(frac * (float)(colors[seg + 1][1] - colors[seg][1]));
        pixels[pi + 2] = colors[seg][2] + (unsigned char)(frac * (float)(colors[seg + 1][2] - colors[seg][2]));
        pixels[pi + 3] = 255;
    }

    return pixels;
}

__attribute__((export_name("petri_init")))
void petri_init(void) {
    petri_seed(1);
}

