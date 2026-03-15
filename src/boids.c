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
static float sep_radius = 15.0f;
static float align_radius = 40.0f;
static float cohesion_radius = 60.0f;
static float max_speed = 3.0f;
static float sep_force = 0.05f;
static float align_force = 0.03f;
static float cohesion_force = 0.005f;
static float trail_decay = 0.97f;

static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
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
        // inline sin/cos via Taylor
        float a = angle;
        float a2 = a * a;
        float a3 = a2 * a;
        float a5 = a3 * a2;
        float sin_a = a - a3 / 6.0f + a5 / 120.0f;
        float ca = a + 1.5707963f;
        float ca2 = ca * ca;
        float ca3 = ca2 * ca;
        float ca5 = ca3 * ca2;
        float cos_a = ca - ca3 / 6.0f + ca5 / 120.0f;
        boid_vx[i] = cos_a * speed;
        boid_vy[i] = sin_a * speed;
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
                // wrap distance
                if (dx > w/2) dx -= w; if (dx < -w/2) dx += w;
                if (dy > h/2) dy -= h; if (dy < -h/2) dy += h;
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
                    cx += boid_x[j];
                    cy += boid_y[j];
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
                float target_x = cx / c_count;
                float target_y = cy / c_count;
                boid_vx[i] += (target_x - boid_x[i]) * cohesion_force;
                boid_vy[i] += (target_y - boid_y[i]) * cohesion_force;
            }

            // clamp speed
            float spd2 = boid_vx[i] * boid_vx[i] + boid_vy[i] * boid_vy[i];
            if (spd2 > max_speed * max_speed) {
                float spd = sqrtf_approx(spd2);
                boid_vx[i] = boid_vx[i] / spd * max_speed;
                boid_vy[i] = boid_vy[i] / spd * max_speed;
            }

            boid_x[i] += boid_vx[i];
            boid_y[i] += boid_vy[i];

            // wrap
            if (boid_x[i] < 0) boid_x[i] += w;
            if (boid_x[i] >= w) boid_x[i] -= w;
            if (boid_y[i] < 0) boid_y[i] += h;
            if (boid_y[i] >= h) boid_y[i] -= h;

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

// replace one random boid with a new random one
__attribute__((export_name("boids_swap_one")))
void boids_swap_one(void) {
    if (num_boids == 0) return;
    int i = (int)(randf() * num_boids);
    if (i >= num_boids) i = num_boids - 1;
    boid_x[i] = randf() * w;
    boid_y[i] = randf() * h;
    float angle = randf() * 6.28318530f;
    float speed = 1.0f + randf() * 2.0f;
    float a = angle;
    float a2 = a * a; float a3 = a2 * a; float a5 = a3 * a2;
    boid_vx[i] = (a + 1.5707963f - (a + 1.5707963f) * (a + 1.5707963f) * (a + 1.5707963f) / 6.0f) * speed;
    // simpler: just use raw components
    boid_vx[i] = (randf() - 0.5f) * speed * 2.0f;
    boid_vy[i] = (randf() - 0.5f) * speed * 2.0f;
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
