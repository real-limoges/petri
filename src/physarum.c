//
// Physarum (slime mold) simulation — WASM
// Particle agents deposit + follow chemical trails on a grid.
// The trail diffuses and decays, producing emergent vein networks.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W 2560
#define MAX_H 1440
#define MAX_N (MAX_W * MAX_H)
#define MAX_AGENTS 100000

static int w = 1024, h = 1024, n = 1024 * 1024;

// trail map (double-buffered)
static float trail_a[MAX_N], trail_b[MAX_N];
static float *trail_read = trail_a, *trail_write = trail_b;
static unsigned char intensity[MAX_N];

// agent state
static float agent_x[MAX_AGENTS];
static float agent_y[MAX_AGENTS];
static float agent_angle[MAX_AGENTS];
static int num_agents = 0;

// parameters
static float sensor_angle = 0.5f;    // radians (~28 degrees)
static float sensor_dist = 9.0f;
static float turn_speed = 0.4f;
static float move_speed = 1.0f;
static float deposit_amount = 5.0f;
static float decay_rate = 0.95f;

// xorshift rng
static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

// trig via Taylor series (no libc)
static float fmodf_approx(float x, float y) {
    return x - (int)(x / y) * y;
}

static float sinf_approx(float x) {
    // reduce to [-pi, pi]
    x = fmodf_approx(x, 6.28318530f);
    if (x > 3.14159265f) x -= 6.28318530f;
    if (x < -3.14159265f) x += 6.28318530f;
    // Taylor 7th order
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
}

static float cosf_approx(float x) {
    return sinf_approx(x + 1.57079632f);
}

static float sense(int agent_idx, float angle_offset) {
    float a = agent_angle[agent_idx] + angle_offset;
    float sx = agent_x[agent_idx] + cosf_approx(a) * sensor_dist;
    float sy = agent_y[agent_idx] + sinf_approx(a) * sensor_dist;

    // wrap
    int ix = ((int)sx % w + w) % w;
    int iy = ((int)sy % h + h) % h;

    return trail_read[iy * w + ix];
}

__attribute__((export_name("physarum_init")))
void physarum_init(int count, int width, int height) {
    if (width > MAX_W) width = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;

    if (count > MAX_AGENTS) count = MAX_AGENTS;
    num_agents = count;

    memset(trail_a, 0, n * sizeof(float));
    memset(trail_b, 0, n * sizeof(float));
    trail_read = trail_a;
    trail_write = trail_b;

    // spawn agents in a disc at center
    float cx = w / 2.0f, cy = h / 2.0f;
    float max_r = (w < h ? w : h) * 0.35f;
    for (int i = 0; i < count; i++) {
        float r = max_r * randf();
        float a = randf() * 6.28318530f;
        agent_x[i] = cx + cosf_approx(a) * r;
        agent_y[i] = cy + sinf_approx(a) * r;
        // point outward initially
        agent_angle[i] = a + 3.14159265f + (randf() - 0.5f) * 1.0f;
    }
}

__attribute__((export_name("physarum_set_params")))
void physarum_set_params(float sa, float sd, float ts, float ms, float dep, float dec) {
    sensor_angle = sa;
    sensor_dist = sd;
    turn_speed = ts;
    move_speed = ms;
    deposit_amount = dep;
    decay_rate = dec;
}

__attribute__((export_name("physarum_step")))
void physarum_step(int steps) {
    for (int s = 0; s < steps; s++) {
        // move agents and deposit
        for (int i = 0; i < num_agents; i++) {
            // sense
            float f_left = sense(i, sensor_angle);
            float f_center = sense(i, 0.0f);
            float f_right = sense(i, -sensor_angle);

            // turn
            if (f_center >= f_left && f_center >= f_right) {
                // keep going straight
            } else if (f_left > f_right) {
                agent_angle[i] += turn_speed * (randf() * 0.5f + 0.5f);
            } else if (f_right > f_left) {
                agent_angle[i] -= turn_speed * (randf() * 0.5f + 0.5f);
            } else {
                // left == right, random turn
                agent_angle[i] += turn_speed * (randf() - 0.5f) * 2.0f;
            }

            // move
            agent_x[i] += cosf_approx(agent_angle[i]) * move_speed;
            agent_y[i] += sinf_approx(agent_angle[i]) * move_speed;

            // wrap
            if (agent_x[i] < 0) agent_x[i] += w;
            if (agent_x[i] >= w) agent_x[i] -= w;
            if (agent_y[i] < 0) agent_y[i] += h;
            if (agent_y[i] >= h) agent_y[i] -= h;

            // deposit trail
            int ix = (int)agent_x[i];
            int iy = (int)agent_y[i];
            trail_read[iy * w + ix] += deposit_amount;
        }

        // diffuse + decay into trail_write
        for (int y = 0; y < h; y++) {
            int ym = (y - 1 + h) % h;
            int yp = (y + 1) % h;
            for (int x = 0; x < w; x++) {
                int xm = (x - 1 + w) % w;
                int xp = (x + 1) % w;

                float sum = trail_read[ym * w + xm]
                          + trail_read[ym * w + x]
                          + trail_read[ym * w + xp]
                          + trail_read[y  * w + xm]
                          + trail_read[y  * w + x]
                          + trail_read[y  * w + xp]
                          + trail_read[yp * w + xm]
                          + trail_read[yp * w + x]
                          + trail_read[yp * w + xp];

                trail_write[y * w + x] = (sum / 9.0f) * decay_rate;
            }
        }

        // swap buffers
        float *tmp = trail_read;
        trail_read = trail_write;
        trail_write = tmp;
    }
}

__attribute__((export_name("physarum_pixels")))
unsigned char* physarum_pixels(void) {
    for (int i = 0; i < n; i++) {
        float t = trail_read[i] * 0.15f;
        if (t > 1.0f) t = 1.0f;
        intensity[i] = (unsigned char)(t * 255.0f);
    }
    return intensity;
}
