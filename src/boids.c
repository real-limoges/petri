//
// Boids flocking simulation — WASM
// Separation, alignment, cohesion → emergent flocking
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W 2560
#define MAX_H 1440
#define MAX_N (MAX_W * MAX_H)
#define MAX_BOIDS 3000

static int w = 1024, h = 1024, n = 1024 * 1024;

static unsigned char intensity[MAX_N];

static float boid_x[MAX_BOIDS];
static float boid_y[MAX_BOIDS];
static float boid_vx[MAX_BOIDS];
static float boid_vy[MAX_BOIDS];
static int num_boids = 0;

// trail map for visual persistence
static float trail[MAX_N];

// params
static float sep_radius = 25.0f;
static float align_radius = 30.0f;
static float cohesion_radius = 35.0f;
static float max_speed = 3.0f;
static float sep_force = 0.06f;
static float align_force = 0.02f;
static float cohesion_force = 0.002f;
static float trail_decay = 0.97f;
static float crowd_threshold = 8.0f;
static float min_speed = 0.5f;

static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

static float sinf_approx(float x) {
    const float pi = 3.14159265f;
    const float twopi = 6.28318530f;
    // reduce to [-pi, pi]
    while (x > pi) x -= twopi;
    while (x < -pi) x += twopi;
    // reduce to [-pi/2, pi/2] where Taylor converges well
    if (x > pi * 0.5f) x = pi - x;
    else if (x < -pi * 0.5f) x = -pi - x;
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f)));
}

static float cosf_approx(float x) {
    return sinf_approx(x + 1.57079632f);
}

static float sqrtf_approx(float x) {
    if (x <= 0) return 0;
    float guess = x * 0.5f;
    for (int i = 0; i < 6; i++)
        guess = (guess + x / guess) * 0.5f;
    return guess;
}

__attribute__((export_name("boids_init")))
void boids_init(int count, int width, int height) {
    if (width > MAX_W) width = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;

    if (count > MAX_BOIDS) count = MAX_BOIDS;
    num_boids = count;
    memset(trail, 0, n * sizeof(float));

    for (int i = 0; i < count; i++) {
        boid_x[i] = randf() * w;
        boid_y[i] = randf() * h;
        float angle = randf() * 6.28318530f;
        float speed = 1.0f + randf() * 2.0f;
        boid_vx[i] = cosf_approx(angle) * speed;
        boid_vy[i] = sinf_approx(angle) * speed;
    }
}

__attribute__((export_name("boids_step")))
void boids_step(int steps) {
    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < num_boids; i++) {
            float sx = 0, sy = 0; int s_count = 0;
            float ax = 0, ay = 0; int a_count = 0;
            float cx = 0, cy = 0; int c_count = 0;

            for (int j = 0; j < num_boids; j++) {
                if (i == j) continue;
                float dx = boid_x[j] - boid_x[i];
                float dy = boid_y[j] - boid_y[i];
                // wrap distance (x only — y has hard walls)
                if (dx > w/2) dx -= w; if (dx < -w/2) dx += w;
                float d2 = dx * dx + dy * dy;

                if (d2 < sep_radius * sep_radius && d2 > 0) {
                    float d = sqrtf_approx(d2);
                    sx -= dx / d;
                    sy -= dy / d;
                    s_count++;
                }
                if (d2 < align_radius * align_radius) {
                    ax += boid_vx[j];
                    ay += boid_vy[j];
                    a_count++;
                }
                if (d2 < cohesion_radius * cohesion_radius) {
                    cx += dx;
                    cy += dy;
                    c_count++;
                }
            }

            if (s_count > 0) {
                boid_vx[i] += sx * sep_force;
                boid_vy[i] += sy * sep_force;
            }
            if (a_count > 0) {
                boid_vx[i] += (ax / a_count - boid_vx[i]) * align_force;
                boid_vy[i] += (ay / a_count - boid_vy[i]) * align_force;
            }
            if (c_count > 0) {
                // cx/cy are already wrap-corrected deltas, so average = offset to center of mass
                float density_scale = 1.0f - (float)c_count / crowd_threshold;
                float eff_cohesion = cohesion_force * density_scale;
                boid_vx[i] += (cx / c_count) * eff_cohesion;
                boid_vy[i] += (cy / c_count) * eff_cohesion;
            }

            // clamp speed
            float spd2 = boid_vx[i] * boid_vx[i] + boid_vy[i] * boid_vy[i];
            if (spd2 > max_speed * max_speed) {
                float spd = sqrtf_approx(spd2);
                boid_vx[i] = boid_vx[i] / spd * max_speed;
                boid_vy[i] = boid_vy[i] / spd * max_speed;
            } else if (spd2 < min_speed * min_speed) {
                if (spd2 > 0.0001f) {
                    float spd = sqrtf_approx(spd2);
                    boid_vx[i] = boid_vx[i] / spd * min_speed;
                    boid_vy[i] = boid_vy[i] / spd * min_speed;
                } else {
                    float angle = randf() * 6.28318530f;
                    boid_vx[i] = cosf_approx(angle) * min_speed;
                    boid_vy[i] = sinf_approx(angle) * min_speed;
                }
            }

            boid_x[i] += boid_vx[i];
            boid_y[i] += boid_vy[i];

            // wrap x, bounce y
            if (boid_x[i] < 0) boid_x[i] += w;
            if (boid_x[i] >= w) boid_x[i] -= w;
            if (boid_y[i] < 0) { boid_y[i] = 0; boid_vy[i] = -boid_vy[i]; }
            if (boid_y[i] >= h) { boid_y[i] = h - 1; boid_vy[i] = -boid_vy[i]; }

            // deposit trail
            int ix = (int)boid_x[i];
            int iy = (int)boid_y[i];
            if (ix >= 0 && ix < w && iy >= 0 && iy < h)
                trail[iy * w + ix] += 3.0f;
        }

        // decay trail
        for (int i = 0; i < n; i++)
            trail[i] *= trail_decay;
    }
}

__attribute__((export_name("boids_pixels")))
unsigned char* boids_pixels(void) {
    for (int i = 0; i < n; i++) {
        float t = trail[i];
        if (t > 1.0f) t = 1.0f;
        intensity[i] = (unsigned char)(t * 255.0f);
    }
    return intensity;
}
