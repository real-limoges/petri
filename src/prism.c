//
// Prism dispersion simulator — WASM
// White beam in, spectrum fans out via Snell + Cauchy. Geometry-out (ray data),
// not pixels — JS rasterizes the rays on a Canvas2D context.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

#define PI       3.14159265f
#define TWO_PI   6.28318530f
#define HALF_PI  1.57079632f

static float sinf_approx(float x) {
    while (x >  PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
}

static float cosf_approx(float x) {
    return sinf_approx(x + HALF_PI);
}

static float sqrtf_approx(float x) {
    if (x <= 0.0f) return 0.0f;
    return __builtin_sqrtf(x);
}

#define N_SAMPLES       60
#define LAMBDA_MIN      380.0f
#define LAMBDA_MAX      700.0f
#define FLOATS_PER_RAY  9    // entry.xy, exit.xy, screen.xy, r, g, b

// Cauchy coefficients, crown-glass-like.
#define CAUCHY_A 1.500f
#define CAUCHY_B 4500.0f

#define WORLD_W 600.0f
#define WORLD_H 400.0f
#define PRISM_CX 300.0f
#define PRISM_CY 200.0f
#define PRISM_R  90.0f
#define BEAM_Y   200.0f

static float rays[N_SAMPLES * FLOATS_PER_RAY];

// Tilt zone where neither violet TIRs nor red exits near the critical angle.
#define DEFAULT_TILT 0.5f
// Outer clamp on tilt input. The visible safe zone is ~[-0.1, 1.1] (JS-side
// cursor maps to ±0.6 around DEFAULT_TILT); the wider WASM clamp gives the
// caller breathing room before the geometry degenerates.
#define TILT_MIN    -0.6f
#define TILT_MAX     1.6f
static float tilt = DEFAULT_TILT;

static float index_of(float lambda_nm) {
    return CAUCHY_A + CAUCHY_B / (lambda_nm * lambda_nm);
}

// Bruton's piecewise wavelength → linear RGB in [0, 1].
static void wavelength_rgb(float lambda_nm, float *r, float *g, float *b) {
    float R, G, B;
    if (lambda_nm < 440.0f) {
        R = -(lambda_nm - 440.0f) / (440.0f - 380.0f);
        G = 0.0f;
        B = 1.0f;
    } else if (lambda_nm < 490.0f) {
        R = 0.0f;
        G = (lambda_nm - 440.0f) / (490.0f - 440.0f);
        B = 1.0f;
    } else if (lambda_nm < 510.0f) {
        R = 0.0f;
        G = 1.0f;
        B = -(lambda_nm - 510.0f) / (510.0f - 490.0f);
    } else if (lambda_nm < 580.0f) {
        R = (lambda_nm - 510.0f) / (580.0f - 510.0f);
        G = 1.0f;
        B = 0.0f;
    } else if (lambda_nm < 645.0f) {
        R = 1.0f;
        G = -(lambda_nm - 645.0f) / (645.0f - 580.0f);
        B = 0.0f;
    } else {
        R = 1.0f;
        G = 0.0f;
        B = 0.0f;
    }

    float factor;
    if (lambda_nm < 380.0f || lambda_nm > 780.0f) {
        factor = 0.0f;
    } else if (lambda_nm < 420.0f) {
        factor = 0.3f + 0.7f * (lambda_nm - 380.0f) / (420.0f - 380.0f);
    } else if (lambda_nm > 700.0f) {
        factor = 0.3f + 0.7f * (780.0f - lambda_nm) / (780.0f - 700.0f);
    } else {
        factor = 1.0f;
    }

    *r = R * factor;
    *g = G * factor;
    *b = B * factor;
}

// Ray R(t) = O + tD vs segment S(s) = A + s(B-A), 2x2 Cramer. Caller
// range-checks t > eps and s in [0, 1].
static int ray_seg_intersect(float ox, float oy, float dx, float dy,
                              float ax, float ay, float bx, float by,
                              float *out_t, float *out_s) {
    float ex = bx - ax;
    float ey = by - ay;
    float det = ex * dy - ey * dx;
    if (det > -1e-8f && det < 1e-8f) return 0;
    float ax_ox = ax - ox;
    float ay_oy = ay - oy;
    *out_t = (ex * ay_oy - ey * ax_ox) / det;
    *out_s = (dx * ay_oy - dy * ax_ox) / det;
    return 1;
}

static void edge_outward_normal(float ax, float ay, float bx, float by,
                                 float cx, float cy,
                                 float *nx, float *ny) {
    float ex = bx - ax;
    float ey = by - ay;
    float px = -ey;
    float py =  ex;
    float mid_x = 0.5f * (ax + bx);
    float mid_y = 0.5f * (ay + by);
    if (px * (cx - mid_x) + py * (cy - mid_y) > 0.0f) { px = -px; py = -py; }
    float len = sqrtf_approx(px * px + py * py);
    if (len < 1e-8f) { *nx = 1.0f; *ny = 0.0f; return; }
    *nx = px / len;
    *ny = py / len;
}

// Snell refraction. Returns 0 on TIR.
static int refract(float dx, float dy, float nx, float ny,
                    float n1, float n2,
                    float *out_tx, float *out_ty) {
    float dn = dx * nx + dy * ny;
    if (dn > 0.0f) { nx = -nx; ny = -ny; dn = -dn; }
    float c1 = -dn;
    float eta = n1 / n2;
    float k = 1.0f - eta * eta * (1.0f - c1 * c1);
    if (k < 0.0f) return 0;
    float c2 = sqrtf_approx(k);
    *out_tx = eta * dx + (eta * c1 - c2) * nx;
    *out_ty = eta * dy + (eta * c1 - c2) * ny;
    return 1;
}

// Find the closest triangle edge a ray strikes, optionally skipping one
// edge (the entry face when tracing the refracted ray, the exit face when
// tracing a TIR bounce). Returns the edge index (0..2) or -1 on miss.
//
// Out parameters are populated only on a hit; pass NULL to skip the
// outward-normal computation when the caller doesn't need it.
static int closest_edge_hit(float ox, float oy, float dx, float dy,
                             float verts[3][2], int skip_edge,
                             float cx, float cy,
                             float *out_x,  float *out_y,
                             float *out_nx, float *out_ny) {
    int   hit_edge = -1;
    float hit_t    = 0.0f;
    for (int e = 0; e < 3; e++) {
        if (e == skip_edge) continue;
        float ax = verts[e][0],         ay = verts[e][1];
        float bx = verts[(e+1)%3][0],   by = verts[(e+1)%3][1];
        float t, s;
        if (!ray_seg_intersect(ox, oy, dx, dy, ax, ay, bx, by, &t, &s)) continue;
        // t > epsilon rules out re-hitting the edge we just left;
        // s in [0, 1] keeps the hit on the segment, not its infinite line.
        if (t <= 1e-4f || s < 0.0f || s > 1.0f) continue;
        if (hit_edge == -1 || t < hit_t) {
            hit_edge = e;
            hit_t    = t;
            *out_x = ox + t * dx;
            *out_y = oy + t * dy;
            if (out_nx) edge_outward_normal(ax, ay, bx, by, cx, cy, out_nx, out_ny);
        }
    }
    return hit_edge;
}

static void compute_ray(float lambda_nm, float *out) {
    // Equilateral triangle, vertex 0 apex-up at tilt=0, in canvas y-down.
    float verts[3][2];
    const float base_angle[3] = {
        -HALF_PI,
        -HALF_PI + 2.0f * PI / 3.0f,
        -HALF_PI - 2.0f * PI / 3.0f,
    };
    for (int j = 0; j < 3; j++) {
        float a = base_angle[j] + tilt;
        verts[j][0] = PRISM_CX + PRISM_R * cosf_approx(a);
        verts[j][1] = PRISM_CY + PRISM_R * sinf_approx(a);
    }
    float cx = (verts[0][0] + verts[1][0] + verts[2][0]) / 3.0f;
    float cy = (verts[0][1] + verts[1][1] + verts[2][1]) / 3.0f;

    float r, g, b;
    wavelength_rgb(lambda_nm, &r, &g, &b);

    float ox = 0.0f,  oy = BEAM_Y;
    float dx = 1.0f,  dy = 0.0f;

    float entry_x, entry_y, entry_nx, entry_ny;
    int hit_edge = closest_edge_hit(ox, oy, dx, dy, verts, -1, cx, cy,
                                    &entry_x, &entry_y, &entry_nx, &entry_ny);

    // Beam misses prism — straight through.
    if (hit_edge == -1) {
        out[0] = 0.0f;     out[1] = BEAM_Y;
        out[2] = 0.0f;     out[3] = BEAM_Y;
        out[4] = WORLD_W;  out[5] = BEAM_Y;
        out[6] = r;        out[7] = g;       out[8] = b;
        return;
    }

    float n_glass = index_of(lambda_nm);
    float tx, ty;
    if (!refract(dx, dy, entry_nx, entry_ny, 1.0f, n_glass, &tx, &ty)) {
        // TIR at entry shouldn't happen physically (we're going from low
        // to high index), but degenerate geometry can still trip it.
        out[0] = entry_x;  out[1] = entry_y;
        out[2] = entry_x;  out[3] = entry_y;
        out[4] = entry_x;  out[5] = entry_y;
        out[6] = r;        out[7] = g;       out[8] = b;
        return;
    }

    float exit_x, exit_y, exit_nx, exit_ny;
    int exit_edge = closest_edge_hit(entry_x, entry_y, tx, ty, verts, hit_edge,
                                     cx, cy,
                                     &exit_x, &exit_y, &exit_nx, &exit_ny);

    if (exit_edge == -1) {
        out[0] = entry_x;  out[1] = entry_y;
        out[2] = entry_x;  out[3] = entry_y;
        out[4] = entry_x;  out[5] = entry_y;
        out[6] = r;        out[7] = g;       out[8] = b;
        return;
    }

    // Refract out of the exit face. On TIR, reflect the internal ray and
    // trace it to whichever third face it strikes — that bounce point
    // becomes `screen`, so JS renders the reflection inside the prism
    // instead of a stub. The post-bounce escape (if any) is omitted
    // because the export only carries three points per ray.
    float screen_x, screen_y;
    float ox2, oy2;
    if (refract(tx, ty, exit_nx, exit_ny, n_glass, 1.0f, &ox2, &oy2)) {
        // Walk the refracted ray to whichever world wall it hits first.
        float t_x = 1e30f, t_y = 1e30f;
        if (ox2 >  1e-6f) t_x = (WORLD_W - exit_x) / ox2;
        if (ox2 < -1e-6f) t_x = (0.0f    - exit_x) / ox2;
        if (oy2 >  1e-6f) t_y = (WORLD_H - exit_y) / oy2;
        if (oy2 < -1e-6f) t_y = (0.0f    - exit_y) / oy2;
        float t_hit = t_x < t_y ? t_x : t_y;
        if (t_hit < 0.0f) t_hit = 0.0f;
        screen_x = exit_x + t_hit * ox2;
        screen_y = exit_y + t_hit * oy2;
    } else {
        float dot = tx * exit_nx + ty * exit_ny;
        float rx  = tx - 2.0f * dot * exit_nx;
        float ry  = ty - 2.0f * dot * exit_ny;
        float bx, by;
        int b_edge = closest_edge_hit(exit_x, exit_y, rx, ry, verts, exit_edge,
                                      cx, cy, &bx, &by, 0, 0);
        if (b_edge == -1) { bx = exit_x; by = exit_y; }
        screen_x = bx;
        screen_y = by;
    }

    out[0] = entry_x;  out[1] = entry_y;
    out[2] = exit_x;   out[3] = exit_y;
    out[4] = screen_x; out[5] = screen_y;
    out[6] = r;        out[7] = g;       out[8] = b;
}

__attribute__((export_name("prism_init")))
void prism_init(void) {
    tilt = DEFAULT_TILT;
    memset(rays, 0, sizeof(rays));
}

__attribute__((export_name("prism_set_tilt")))
void prism_set_tilt(float radians) {
    if (radians < TILT_MIN) radians = TILT_MIN;
    if (radians > TILT_MAX) radians = TILT_MAX;
    tilt = radians;
}

__attribute__((export_name("prism_step")))
void prism_step(void) {
    for (int i = 0; i < N_SAMPLES; i++) {
        float t = (float)i / (float)(N_SAMPLES - 1);
        float lambda = LAMBDA_MIN + t * (LAMBDA_MAX - LAMBDA_MIN);
        compute_ray(lambda, &rays[i * FLOATS_PER_RAY]);
    }
}

__attribute__((export_name("prism_rays")))
const float* prism_rays(void) {
    return rays;
}

__attribute__((export_name("prism_ray_count")))
int prism_ray_count(void) {
    return N_SAMPLES;
}
