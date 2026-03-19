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
static unsigned int swap_call_count = 0;
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

// replace count boids, clustered around a single random point
__attribute__((export_name("boids_swap_n")))
void boids_swap_n(int count) {
    if (num_boids == 0 || count <= 0) return;
    if (count > num_boids) count = num_boids;

    // mix call count into rng so repeated calls produce varied locations
    rng_state ^= ++swap_call_count * 2654435761u;

    float cx = randf() * w;
    float cy = randf() * h;
    float radius = 40.0f;

    int start = (int)(randf() * num_boids);
    for (int k = 0; k < count; k++) {
        int i = (start + k) % num_boids;
        boid_x[i] = cx + (randf() - 0.5f) * 2.0f * radius;
        boid_y[i] = cy + (randf() - 0.5f) * 2.0f * radius;
        if (boid_x[i] < 0) boid_x[i] += w;
        if (boid_x[i] >= w) boid_x[i] -= w;
        if (boid_y[i] < 0) boid_y[i] += h;
        if (boid_y[i] >= h) boid_y[i] -= h;
        float speed = 1.0f + randf() * 2.0f;
        boid_vx[i] = (randf() - 0.5f) * speed * 2.0f;
        boid_vy[i] = (randf() - 0.5f) * speed * 2.0f;
    }
}

__attribute__((export_name("boids_swap_edge")))
void boids_swap_edge(int count) {
    if (num_boids == 0 || count <= 0) return;
    if (count > num_boids) count = num_boids;

    rng_state ^= ++swap_call_count * 2654435761u;

    // pick a random edge: 0=top, 1=bottom, 2=left, 3=right
    int edge = (int)(randf() * 4);
    if (edge >= 4) edge = 3;

    float speed = max_speed * 0.8f;
    float depth = 30.0f;  // inward spawn depth
    int start = (int)(randf() * num_boids);
    for (int k = 0; k < count; k++) {
        int i = (start + k) % num_boids;
        // spread along the edge with full width/height, depth only goes inward
        if (edge == 0) {        // top — spread across width, push downward
            boid_x[i] = randf() * w;
            boid_y[i] = randf() * depth;
            boid_vx[i] = (randf() - 0.5f) * speed * 0.4f;
            boid_vy[i] = speed + (randf() - 0.5f) * speed * 0.4f;
        } else if (edge == 1) { // bottom — spread across width, push upward
            boid_x[i] = randf() * w;
            boid_y[i] = h - 1 - randf() * depth;
            boid_vx[i] = (randf() - 0.5f) * speed * 0.4f;
            boid_vy[i] = -speed + (randf() - 0.5f) * speed * 0.4f;
        } else if (edge == 2) { // left — spread across height, push rightward
            boid_x[i] = randf() * depth;
            boid_y[i] = randf() * h;
            boid_vx[i] = speed + (randf() - 0.5f) * speed * 0.4f;
            boid_vy[i] = (randf() - 0.5f) * speed * 0.4f;
        } else {                // right — spread across height, push leftward
            boid_x[i] = w - 1 - randf() * depth;
            boid_y[i] = randf() * h;
            boid_vx[i] = -speed + (randf() - 0.5f) * speed * 0.4f;
            boid_vy[i] = (randf() - 0.5f) * speed * 0.4f;
        }
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
