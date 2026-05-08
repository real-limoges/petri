//
// Cloud sim -- 2D moist convection on a vertical slice. Boussinesq with
// saturation closure. See fugue/docs/cloud_theory.md for full derivation.
//
// Physics-only: this module exposes raw fields (qc, etc.) and primitives
// (apply_bubble) and steps the solver. All composition (palette, view
// window, render, foreground) lives in JS -- see
// fugue/assets/js/hooks/clouds_canvas.js.
//
// Operator splitting per timestep (theory doc 3.4):
//   1. semi-Lagrangian advect T, qv, qc, u, w
//   2. buoyancy on w, dry-adiabatic cooling on T
//   3. pressure projection (Jacobi) for incompressibility
//   4. saturation adjustment (Tetens)
//   5. boundary conditions
//
// Single regime: plains fair-weather cumulus. (The marine-layer regime and
// the style slider are retired; `weather` is kept as an API-compat no-op.)
//
// Native validation: src/clouds_test.c is a separate translation unit
// that #includes this file with NATIVE_TEST defined. It dumps PGM frames
// for off-browser inspection. The WASM build compiles only clouds.c.
//

#ifdef NATIVE_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EXPORT(name)
#else
#define EXPORT(name) __attribute__((export_name(name)))
#endif

#ifndef NATIVE_TEST
// memset/memcpy: clang lowers some array-init / struct-copy ops to these.
void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return d;
}
#endif

// --- Domain ---
#define NX 120
#define NZ 60
#define N (NX * NZ)
#define DX 50.0f
#define DZ 50.0f

// (Render lives in JS now. The WASM exposes raw fields and primitives;
// composition -- palette, view window, tones, banding -- is JS-side.)

// --- Physics constants ---
#define G       9.81f
#define RD      287.0f
#define RV      461.5f
#define CP      1005.0f
#define LV      2.5e6f
#define P0      100000.0f
#define EPS_RV  0.622f

#define DT      1.0f
#define JACOBI_ITERS 25

// --- Indexing ---
// Row-major: ix is unit stride, iz is NX stride.
#define IX(ix, iz) ((iz) * NX + (ix))

// --- Fields ---
static float T_field[N];
static float qv_field[N];
static float qc_field[N];
static float u_field[N];
static float w_field[N];
static float p_field[N];
static float scratch_a[N];
static float scratch_b[N];

static float T_ref[NZ];
static float qv_ref[NZ];
static float p_ref[NZ];
static float rho_ref[NZ];

// Plains cumulus scene. Big isolated towers over a flat horizon, gentle
// wind so the clouds emerge in place rather than drift across the canvas.
// `weather` is kept as a no-op for API compatibility.
static float weather = 1.0f;
static float mean_wind = 1.0f;
static int frame_count = 0;
static int periodic_bubbles_enabled = 1;

static unsigned int rng_state = 0xC0DEC0DEu;
static unsigned int randu(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state & 0x7fffffffu;
}
static float randf(void) {
    return (float)randu() / (float)0x7fffffff;
}

// (Pixel buffer removed -- JS reads qc_field directly via Float32Array view.)

// --- expf approximation ---
//
// Range-reduce to [-ln2/2, +ln2/2] via exp(x) = 2^k * exp(r), then a 6-term
// Taylor series on r. For Tetens the input range is roughly [-3, +2] so k
// stays in {-4..3} -- well-behaved.
//
static float expf_approx(float x) {
    if (x > 60.0f) x = 60.0f;
    if (x < -60.0f) x = -60.0f;
    const float LOG2E = 1.4426950408889634f;
    const float LN2   = 0.6931471805599453f;
    float kf = x * LOG2E;
    int k = (int)(kf >= 0.0f ? kf + 0.5f : kf - 0.5f);
    float r = x - (float)k * LN2;
    float exp_r = 1.0f + r * (1.0f + r * (0.5f + r * (1.0f/6.0f
                + r * (1.0f/24.0f + r * (1.0f/120.0f + r * (1.0f/720.0f))))));
    union { unsigned int u; float f; } pow2;
    int biased = k + 127;
    if (biased < 1)   return 0.0f;
    if (biased > 254) return 1e30f;
    pow2.u = ((unsigned int)biased) << 23;
    return pow2.f * exp_r;
}

// --- Saturation ---
//
// Tetens formula -- see theory doc 2.5.
//
static float saturation_vapor_pressure(float T) {
    float Tc = T - 273.15f;
    return 611.2f * expf_approx(17.67f * Tc / (Tc + 243.5f));
}

static float saturation_mixing_ratio(float T, float p) {
    float es = saturation_vapor_pressure(T);
    if (es > 0.99f * p) es = 0.99f * p;
    return EPS_RV * es / (p - es);
}

// --- Reference profile ---
//
// Builds T_ref, qv_ref, p_ref, rho_ref. Pressure integrated hydrostatically
// upward from P0 using current rho_ref(z). Don't bother iterating -- the
// gas-law density from a 1-pass T-then-p sweep is fine for visualization.
//
static void build_reference_profile(float w) {
    (void)w;  // weather knob is currently unused -- one fixed cumulus column

    // Plains cumulus profile: warm surface, conditionally unstable through
    // the troposphere, a gentle stability bump above 2 km to soften the
    // anvil tops without preventing tall vertical development.
    const float T_surf = 296.0f;
    for (int iz = 0; iz < NZ; iz++) {
        float z = ((float)iz + 0.5f) * DZ;
        if (z < 2000.0f) {
            T_ref[iz] = T_surf - 6.5e-3f * z;
        } else {
            float T_break = T_surf - 6.5e-3f * 2000.0f;
            T_ref[iz] = T_break - 4.5e-3f * (z - 2000.0f);
        }
    }

    float p = P0;
    for (int iz = 0; iz < NZ; iz++) {
        float rho = p / (RD * T_ref[iz]);
        rho_ref[iz] = rho;
        p_ref[iz] = p;
        p -= rho * G * DZ;
    }

    // Moist BL with RH ~ 90% at surface tapering to 50% above. High
    // surface RH puts LCL low enough that bubbles condense reliably; the
    // dry layer above 2 km keeps the visible cloud body distinct from
    // surrounding air rather than a uniform haze.
    for (int iz = 0; iz < NZ; iz++) {
        float z = ((float)iz + 0.5f) * DZ;
        float frac = z / ((float)NZ * DZ);
        float rh = 0.92f + (0.50f - 0.92f) * frac;

        float qvs_here = saturation_mixing_ratio(T_ref[iz], p_ref[iz]);
        float qv = rh * qvs_here;
        if (qv < 0.0005f) qv = 0.0005f;
        qv_ref[iz] = qv;
    }
}

// --- Field initialization ---
static void init_fields(void) {
    for (int iz = 0; iz < NZ; iz++) {
        for (int ix = 0; ix < NX; ix++) {
            int i = IX(ix, iz);
            T_field[i] = T_ref[iz];
            qv_field[i] = qv_ref[iz];
            qc_field[i] = 0.0f;
            u_field[i] = mean_wind;
            w_field[i] = 0.0f;
            p_field[i] = 0.0f;
        }
    }
}

// --- Boundary conditions ---
//
// Top: rigid lid (w=0), zero qc, T/qv clamped to reference.
// Bottom: surface clamps T and qv to their reference values (acts as
//         ocean/ground source), w=0.
// Sides: marine -> west inflow (u=mean_wind, T/qv from ref), east outflow
//                 (zero gradient).
//        cumulus -> periodic in x (handled implicitly in advection by wrap;
//                   here we just leave side cells alone).
//
static void apply_boundary_conditions(void) {
    // Top
    int iz_top = NZ - 1;
    for (int ix = 0; ix < NX; ix++) {
        int i = IX(ix, iz_top);
        w_field[i] = 0.0f;
        T_field[i] = T_ref[iz_top];
        qv_field[i] = qv_ref[iz_top];
        qc_field[i] = 0.0f;
    }

    // Bottom: surface clamps T (ocean acts as heat reservoir) and zeros w.
    // Don't clamp qv -- if surface qv_ref is at/above saturation for the
    // surface T, a Dirichlet clamp injects mass every step (saturation
    // adjustment removes the excess into qc, BC restores it, repeat). qv is
    // left to advect/condense freely; the inflow boundary is the moisture
    // source for marine, periodic-with-init for cumulus.
    for (int ix = 0; ix < NX; ix++) {
        int i = IX(ix, 0);
        w_field[i] = 0.0f;
        T_field[i] = T_ref[0];
    }

    // Sides: west inflow, east outflow (zero-gradient). Same scheme for all
    // weather values -- the inflow advects fresh reference air in, condensed
    // qc drifts out the right side. Steady state is set by the inflow
    // moisture and the column's saturation curve.
    for (int iz = 0; iz < NZ; iz++) {
        u_field[IX(0, iz)]  = mean_wind;
        T_field[IX(0, iz)]  = T_ref[iz];
        qv_field[IX(0, iz)] = qv_ref[iz];
        qc_field[IX(0, iz)] = 0.0f;
        w_field[IX(0, iz)]  = 0.0f;

        int j = IX(NX - 1, iz);
        int jl = IX(NX - 2, iz);
        T_field[j]  = T_field[jl];
        qv_field[j] = qv_field[jl];
        qc_field[j] = qc_field[jl];
        u_field[j]  = u_field[jl];
        w_field[j]  = w_field[jl];
    }
}

// --- Bilinear sample (semi-Lagrangian backtrace) ---
//
// Always clamps -- domain is open in x with west inflow / east outflow.
//
static float bilinear_sample(const float *field, float x_m, float z_m) {
    float fx = x_m / DX - 0.5f;
    float fz = z_m / DZ - 0.5f;

    if (fz < 0.0f) fz = 0.0f;
    if (fz > (float)(NZ - 1) - 0.001f) fz = (float)(NZ - 1) - 0.001f;
    if (fx < 0.0f) fx = 0.0f;
    if (fx > (float)(NX - 1) - 0.001f) fx = (float)(NX - 1) - 0.001f;

    int i0 = (int)fx;
    int i1 = i0 + 1;
    float a = fx - (float)i0;

    int j0 = (int)fz;
    int j1 = j0 + 1;
    float b = fz - (float)j0;

    float f00 = field[IX(i0, j0)];
    float f10 = field[IX(i1, j0)];
    float f01 = field[IX(i0, j1)];
    float f11 = field[IX(i1, j1)];

    return (1.0f - a) * (1.0f - b) * f00
         +        a  * (1.0f - b) * f10
         + (1.0f - a) *        b  * f01
         +        a  *        b  * f11;
}

static void advect_field(const float *in, float *out, float dt) {
    for (int iz = 0; iz < NZ; iz++) {
        for (int ix = 0; ix < NX; ix++) {
            int i = IX(ix, iz);
            float x = ((float)ix + 0.5f) * DX;
            float z = ((float)iz + 0.5f) * DZ;
            float xb = x - u_field[i] * dt;
            float zb = z - w_field[i] * dt;
            out[i] = bilinear_sample(in, xb, zb);
        }
    }
}

static void swap_into(float *dst, const float *src) {
    for (int i = 0; i < N; i++) dst[i] = src[i];
}

static void advect_all(float dt) {
    advect_field(T_field, scratch_a, dt);  swap_into(T_field, scratch_a);
    advect_field(qv_field, scratch_a, dt); swap_into(qv_field, scratch_a);
    advect_field(qc_field, scratch_a, dt); swap_into(qc_field, scratch_a);
    advect_field(u_field, scratch_a, dt);  // need both u and w intact during w-advect
    advect_field(w_field, scratch_b, dt);
    swap_into(u_field, scratch_a);
    swap_into(w_field, scratch_b);
}

// --- Buoyancy ---
static void apply_buoyancy(float dt) {
    for (int iz = 1; iz < NZ - 1; iz++) {
        float Tref_j = T_ref[iz];
        float qvref_j = qv_ref[iz];
        for (int ix = 0; ix < NX; ix++) {
            int i = IX(ix, iz);
            float Tprime  = T_field[i]  - Tref_j;
            float qvprime = qv_field[i] - qvref_j;
            float B = G * (Tprime / Tref_j + 0.61f * qvprime - qc_field[i]);
            w_field[i] += dt * B;
            T_field[i] -= dt * (G / CP) * w_field[i];
        }
    }
}

// --- Pressure projection (Jacobi) ---
//
// Solve laplacian(p) = (rho/dt) * div(u*), then correct u, w to be
// divergence-free. Uses scratch_a for divergence and scratch_b for the
// p-iteration ping-pong.
//
static void pressure_project(float dt) {
    float *divu = scratch_a;
    float *p_new = scratch_b;

    // Compute divergence
    for (int iz = 0; iz < NZ; iz++) {
        for (int ix = 0; ix < NX; ix++) {
            int i = IX(ix, iz);
            float du_dx, dw_dz;

            int ip = ix + 1, im = ix - 1;
            if (ip >= NX) ip = NX - 1;
            if (im < 0)   im = 0;
            du_dx = (u_field[IX(ip, iz)] - u_field[IX(im, iz)]) / (2.0f * DX);

            int jp = iz + 1, jm = iz - 1;
            if (jp >= NZ) jp = NZ - 1;
            if (jm < 0)   jm = 0;
            dw_dz = (w_field[IX(ix, jp)] - w_field[IX(ix, jm)]) / (2.0f * DZ);

            divu[i] = du_dx + dw_dz;
        }
    }

    // Initial guess: zero
    for (int i = 0; i < N; i++) p_field[i] = 0.0f;

    float coef = 1.0f / (2.0f / (DX * DX) + 2.0f / (DZ * DZ));

    for (int iter = 0; iter < JACOBI_ITERS; iter++) {
        for (int iz = 0; iz < NZ; iz++) {
            for (int ix = 0; ix < NX; ix++) {
                int i = IX(ix, iz);

                int ip = ix + 1, im = ix - 1;
                if (ip >= NX) ip = NX - 1;
                if (im < 0)   im = 0;
                int jp = iz + 1, jm = iz - 1;
                if (jp >= NZ) jp = NZ - 1;
                if (jm < 0)   jm = 0;

                float lap_x = (p_field[IX(ip, iz)] + p_field[IX(im, iz)]) / (DX * DX);
                float lap_z = (p_field[IX(ix, jp)] + p_field[IX(ix, jm)]) / (DZ * DZ);
                float rhs = (rho_ref[iz] / dt) * divu[i];

                p_new[i] = (lap_x + lap_z - rhs) * coef;
            }
        }
        for (int i = 0; i < N; i++) p_field[i] = p_new[i];
    }

    // Correct velocities
    for (int iz = 0; iz < NZ; iz++) {
        float inv_rho = 1.0f / rho_ref[iz];
        for (int ix = 0; ix < NX; ix++) {
            int i = IX(ix, iz);

            int ip = ix + 1, im = ix - 1;
            if (ip >= NX) ip = NX - 1;
            if (im < 0)   im = 0;
            int jp = iz + 1, jm = iz - 1;
            if (jp >= NZ) jp = NZ - 1;
            if (jm < 0)   jm = 0;

            float dpdx = (p_field[IX(ip, iz)] - p_field[IX(im, iz)]) / (2.0f * DX);
            float dpdz = (p_field[IX(ix, jp)] - p_field[IX(ix, jm)]) / (2.0f * DZ);

            u_field[i] -= dt * inv_rho * dpdx;
            w_field[i] -= dt * inv_rho * dpdz;
        }
    }
}

// --- Saturation adjustment ---
//
// Theory doc 2.5: implicit linearization
//   dqc = (qv - qvs) / (1 + Lv^2 qvs / (Cp Rv T^2))
//
static void saturation_adjustment(void) {
    for (int iz = 0; iz < NZ; iz++) {
        float p = p_ref[iz];
        for (int ix = 0; ix < NX; ix++) {
            int i = IX(ix, iz);
            float T = T_field[i];
            float qv = qv_field[i];
            float qc = qc_field[i];

            float qvs = saturation_mixing_ratio(T, p);
            float denom = 1.0f + (LV * LV * qvs) / (CP * RV * T * T);

            if (qv > qvs) {
                float dqc = (qv - qvs) / denom;
                qv_field[i] -= dqc;
                qc_field[i] += dqc;
                T_field[i]  += LV * dqc / CP;
            } else if (qc > 0.0f && qv < qvs) {
                float dqc_max = (qvs - qv) / denom;
                float dqc = (qc < dqc_max) ? qc : dqc_max;
                qv_field[i] += dqc;
                qc_field[i] -= dqc;
                T_field[i]  -= LV * dqc / CP;
            }

            // Floor qc to avoid drifting slightly negative.
            if (qc_field[i] < 0.0f) qc_field[i] = 0.0f;
        }
    }
}

// --- Bubble triggering for cumulus ---
//
// Drop a small warm bubble at random x along the bottom every BUBBLE_PERIOD
// sim seconds. Models daytime surface heating without modeling it.
//
#define BUBBLE_PERIOD 120
#define BUBBLE_DT_K     1.5f
#define BUBBLE_RADIUS_M 120.0f
#define BUBBLE_Z_M       150.0f

static void apply_warm_bubble(float xc_m, float zc_m, float dT_K, float radius_m) {
    float r2 = radius_m * radius_m;
    for (int iz = 0; iz < NZ; iz++) {
        float z = ((float)iz + 0.5f) * DZ;
        float dz = z - zc_m;
        for (int ix = 0; ix < NX; ix++) {
            float x = ((float)ix + 0.5f) * DX;
            float dx = x - xc_m;
            float d2 = dx * dx + dz * dz;
            if (d2 < r2) {
                float gauss = expf_approx(-d2 / (0.5f * r2));
                T_field[IX(ix, iz)] += dT_K * gauss;
            }
        }
    }
}

// (Forcing scheduling lives in JS now -- it calls clouds_apply_bubble
// to inject thermals on whatever cadence/pattern composition wants.)

// --- Render ---
//
// Removed. The WASM is physics-only now; JS reads qc_field via a typed
// array view and does palette / view window / tone quantization there.
// See assets/js/hooks/clouds_canvas.js.


// --- Public API ---

EXPORT("clouds_init")
void clouds_init(void) {
    weather = 1.0f;
    mean_wind = 1.0f;
    frame_count = 0;
    rng_state = 0xC0DEC0DEu;
    build_reference_profile(weather);
    init_fields();
    apply_boundary_conditions();
}

EXPORT("clouds_step")
void clouds_step(int n) {
    for (int s = 0; s < n; s++) {
        advect_all(DT);
        apply_buoyancy(DT);
        pressure_project(DT);
        saturation_adjustment();
        apply_boundary_conditions();
        frame_count++;
    }
}

// Kept for API compatibility -- currently the scene is fixed plains
// cumulus, so the value is stored but doesn't change anything yet. No
// field reset; future versions can swap profiles smoothly.
EXPORT("clouds_set_weather")
void clouds_set_weather(float w) {
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    weather = w;
}

EXPORT("clouds_set_wind")
void clouds_set_wind(float u_ms) {
    mean_wind = u_ms;
}

EXPORT("clouds_seed")
void clouds_seed(int pattern) {
    init_fields();
    apply_boundary_conditions();
    if (pattern == 1) {
        // Single test bubble at domain center bottom (cumulus sense).
        // 3 K is stronger than the 2 K spec but needed for the parcel to
        // reach LCL ~600 m given semi-Lagrangian diffusion of the anomaly.
        float xc = ((float)NX * 0.5f) * DX;
        apply_warm_bubble(xc, BUBBLE_Z_M, 3.0f, 200.0f);
    }
    // pattern 0 = rest (no bubble), pattern 2 = default (no perturbation,
    // bubbles will be applied during stepping for cumulus).
}

// JS-callable forcing primitive. Add a Gaussian warm anomaly centered
// at (x_m, z_m). All composition (cadence, cluster patterns, distributed
// vs impulsive) is the JS hook's job.
EXPORT("clouds_apply_bubble")
void clouds_apply_bubble(float x_m, float z_m, float dT_K, float radius_m) {
    apply_warm_bubble(x_m, z_m, dT_K, radius_m);
}

// Pointer to the qc field. JS wraps this in a Float32Array view of
// size NX*NZ. Stable across step() calls because everything is static.
EXPORT("clouds_qc")
const float *clouds_qc(void) {
    return qc_field;
}

EXPORT("clouds_grid_nx") int clouds_grid_nx(void) { return NX; }
EXPORT("clouds_grid_nz") int clouds_grid_nz(void) { return NZ; }
EXPORT("clouds_grid_dx") float clouds_grid_dx(void) { return DX; }
EXPORT("clouds_grid_dz") float clouds_grid_dz(void) { return DZ; }

