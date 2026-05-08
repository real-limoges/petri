//
// Boids flocking — WASM
//
// Reynolds' three rules: separation (push away from close neighbours),
// alignment (match neighbours' heading), cohesion (drift toward the
// neighbourhood centroid). Each rule has its own radius and weight; a
// crowd-density term throttles cohesion in dense flocks so they don't
// collapse into a knot.
//
// Boundaries: x wraps (toroidal), y bounces (hard walls). The mismatch is
// deliberate — it keeps boids inside the visible band on widescreens
// without forcing the eye to track wrap-around vertical edges.
//
// A trail buffer accumulates each boid's path and decays per frame; we
// render it under the boids themselves for a comet-tail effect.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W     2560
#define MAX_H     1440
#define MAX_N     (MAX_W * MAX_H)
#define MAX_BOIDS 3000

#define PI     3.14159265f
#define TWO_PI 6.28318530f
#define HALF_PI 1.57079632f

// Below this distance after sqrt, two boids are functionally on top of
// each other and the unit-vector separation force is undefined. Treat as
// "no contribution this frame" rather than dividing into a near-zero.
#define MIN_DIST       1e-4f
// Lower bound on crowd_threshold to keep the cohesion divisor finite even
// if the slider on the host side hits zero.
#define MIN_CROWD      0.5f

static int w = 1024, h = 1024, n = 1024 * 1024;

static unsigned char intensity[MAX_N];
static float         trail[MAX_N];

static float boid_x[MAX_BOIDS];
static float boid_y[MAX_BOIDS];
static float boid_vx[MAX_BOIDS];
static float boid_vy[MAX_BOIDS];
static int   num_boids = 0;

// Tunable parameters. Defaults produce a stable, recognisable flock at
// 1024 boids on 1024×1024; the host wires sliders to the setters below.
static float sep_radius      = 25.0f;
static float align_radius    = 30.0f;
static float cohesion_radius = 35.0f;
static float sep_force       = 0.06f;
static float align_force     = 0.02f;
static float cohesion_force  = 0.002f;
static float max_speed       = 3.0f;
static float min_speed       = 0.5f;
static float trail_decay     = 0.97f;
static float crowd_threshold = 8.0f;

static unsigned int rng_state = 42;
static float randf(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0x7fffffff) / (float)0x7fffffff;
}

static float sinf_approx(float x) {
    while (x >  PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    if (x >  HALF_PI) x =  PI - x;
    if (x < -HALF_PI) x = -PI - x;
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f)));
}
static float cosf_approx(float x) { return sinf_approx(x + HALF_PI); }

// Toroidal x-axis delta: shortest signed distance from a to b on a circle
// of circumference w. Y is intentionally not wrapped — see header.
static inline float wrap_dx(float dx, int wlim) {
    if (dx >  wlim / 2) dx -= wlim;
    if (dx < -wlim / 2) dx += wlim;
    return dx;
}

static void spawn_boid(int i) {
    boid_x[i]  = randf() * w;
    boid_y[i]  = randf() * h;
    float ang  = randf() * TWO_PI;
    float spd  = 1.0f + randf() * 2.0f;
    boid_vx[i] = cosf_approx(ang) * spd;
    boid_vy[i] = sinf_approx(ang) * spd;
}

// Re-launch a boid that has lost almost all kinetic energy. Without this,
// a few stationary boids accumulate at the centre of every dense flock
// and visually read as dead pixels.
static void relaunch(int i) {
    float ang  = randf() * TWO_PI;
    boid_vx[i] = cosf_approx(ang) * min_speed;
    boid_vy[i] = sinf_approx(ang) * min_speed;
}

static void clamp_speed(int i) {
    float vx = boid_vx[i], vy = boid_vy[i];
    float spd2 = vx * vx + vy * vy;
    if (spd2 > max_speed * max_speed) {
        float inv = max_speed / __builtin_sqrtf(spd2);
        boid_vx[i] = vx * inv;
        boid_vy[i] = vy * inv;
    } else if (spd2 < min_speed * min_speed) {
        if (spd2 > MIN_DIST * MIN_DIST) {
            float inv = min_speed / __builtin_sqrtf(spd2);
            boid_vx[i] = vx * inv;
            boid_vy[i] = vy * inv;
        } else {
            relaunch(i);
        }
    }
}

__attribute__((export_name("boids_init")))
void boids_init(int count, int width, int height) {
    if (width  > MAX_W) width  = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;

    if (count > MAX_BOIDS) count = MAX_BOIDS;
    num_boids = count;
    memset(trail, 0, n * sizeof(float));

    for (int i = 0; i < count; i++) spawn_boid(i);
}

__attribute__((export_name("boids_set_count")))
void boids_set_count(int count) {
    if (count < 0)         count = 0;
    if (count > MAX_BOIDS) count = MAX_BOIDS;
    for (int i = num_boids; i < count; i++) spawn_boid(i);
    num_boids = count;
}

__attribute__((export_name("boids_set_sep_radius")))      void boids_set_sep_radius(float v)      { sep_radius = v; }
__attribute__((export_name("boids_set_align_radius")))    void boids_set_align_radius(float v)    { align_radius = v; }
__attribute__((export_name("boids_set_cohesion_radius"))) void boids_set_cohesion_radius(float v) { cohesion_radius = v; }
__attribute__((export_name("boids_set_sep_force")))       void boids_set_sep_force(float v)       { sep_force = v; }
__attribute__((export_name("boids_set_align_force")))     void boids_set_align_force(float v)     { align_force = v; }
__attribute__((export_name("boids_set_cohesion_force")))  void boids_set_cohesion_force(float v)  { cohesion_force = v; }
__attribute__((export_name("boids_set_max_speed")))       void boids_set_max_speed(float v)       { max_speed = v; }
__attribute__((export_name("boids_set_min_speed")))       void boids_set_min_speed(float v)       { min_speed = v; }
__attribute__((export_name("boids_set_trail_decay")))     void boids_set_trail_decay(float v)     { trail_decay = v; }

__attribute__((export_name("boids_set_crowd_threshold")))
void boids_set_crowd_threshold(float v) {
    crowd_threshold = v < MIN_CROWD ? MIN_CROWD : v;
}

__attribute__((export_name("boids_step")))
void boids_step(int steps) {
    const float sep_r2  = sep_radius      * sep_radius;
    const float ali_r2  = align_radius    * align_radius;
    const float coh_r2  = cohesion_radius * cohesion_radius;

    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < num_boids; i++) {
            float sx = 0, sy = 0; int s_count = 0;
            float ax = 0, ay = 0; int a_count = 0;
            float cx = 0, cy = 0; int c_count = 0;

            for (int j = 0; j < num_boids; j++) {
                if (j == i) continue;
                float dx = wrap_dx(boid_x[j] - boid_x[i], w);
                float dy = boid_y[j] - boid_y[i];
                float d2 = dx * dx + dy * dy;

                if (d2 < sep_r2) {
                    float d = __builtin_sqrtf(d2);
                    if (d > MIN_DIST) {
                        sx -= dx / d;
                        sy -= dy / d;
                        s_count++;
                    }
                }
                if (d2 < ali_r2) {
                    ax += boid_vx[j];
                    ay += boid_vy[j];
                    a_count++;
                }
                if (d2 < coh_r2) {
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
                // Damp cohesion as the local neighbourhood saturates,
                // otherwise dense flocks spiral into a single point.
                float density = 1.0f - (float)c_count / crowd_threshold;
                float w_coh   = cohesion_force * density;
                boid_vx[i] += (cx / c_count) * w_coh;
                boid_vy[i] += (cy / c_count) * w_coh;
            }

            clamp_speed(i);

            boid_x[i] += boid_vx[i];
            boid_y[i] += boid_vy[i];

            if (boid_x[i] <  0) boid_x[i] += w;
            if (boid_x[i] >= w) boid_x[i] -= w;
            if (boid_y[i] <  0)     { boid_y[i] = 0;       boid_vy[i] = -boid_vy[i]; }
            if (boid_y[i] >= h)     { boid_y[i] = h - 1;   boid_vy[i] = -boid_vy[i]; }

            int ix = (int)boid_x[i];
            int iy = (int)boid_y[i];
            if (ix >= 0 && ix < w && iy >= 0 && iy < h)
                trail[iy * w + ix] += 3.0f;
        }

        for (int i = 0; i < n; i++) trail[i] *= trail_decay;
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
