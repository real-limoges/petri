//
// Cloud sim -- 2D moist convection on a vertical slice. Boussinesq with
// saturation closure. See fugue/docs/cloud_theory.md for full derivation.
//
// Operator splitting per timestep (theory doc 3.4):
//   1. semi-Lagrangian advect T, qv, qc, u, w
//   2. buoyancy on w, dry-adiabatic cooling on T
//   3. pressure projection (Jacobi) for incompressibility
//   4. saturation adjustment (Tetens)
//   5. boundary conditions per regime
//
// Two regimes share the solver and differ only in reference profile + BCs:
//   regime 0 = SF marine layer (cool surface, sharp inversion at 400 m)
//   regime 1 = fair-weather cumulus (warm surface, conditionally unstable)
//
// Render path is currently a stub -- Phase 2 work after sim validates.
//
// Native build (PGM dump driver): clang -DNATIVE_TEST -O2 -lm -o test src/clouds.c
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

// --- Render output (stub for Phase 1) ---
#define OUT_W 600
#define OUT_H 300

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

static int regime = 1;        // 0=marine, 1=cumulus
static float mean_wind = 4.0f;
static float style = 1.0f;
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

static unsigned char pixels[OUT_W * OUT_H * 4];

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
static void build_reference_profile(int reg) {
    // T profile first
    if (reg == 0) {
        // Marine layer: cool well-mixed surface up to inversion at iz=8 (400 m),
        // sharp +8 K jump, then standard 6.5 K/km lapse above.
        for (int iz = 0; iz < NZ; iz++) {
            float z = ((float)iz + 0.5f) * DZ;
            float T;
            if (iz < 8) {
                T = 285.0f + 0.005f * z;          // weakly stable below
            } else if (iz == 8) {
                T = 285.0f + 0.005f * (8.0f * DZ) + 8.0f;
            } else {
                float T_top_inv = 285.0f + 0.005f * (8.0f * DZ) + 8.0f;
                T = T_top_inv - 6.5e-3f * (z - 8.0f * DZ);
            }
            T_ref[iz] = T;
        }
    } else {
        // Cumulus: T_surf=295, lapse 6.5 K/km.
        for (int iz = 0; iz < NZ; iz++) {
            float z = ((float)iz + 0.5f) * DZ;
            T_ref[iz] = 295.0f - 6.5e-3f * z;
        }
    }

    // Hydrostatic pressure integration upward from P0
    float p = P0;
    for (int iz = 0; iz < NZ; iz++) {
        float rho = p / (RD * T_ref[iz]);
        rho_ref[iz] = rho;
        p_ref[iz] = p;
        p -= rho * G * DZ;
    }

    // qv profile -- needs p_ref to compute saturation
    if (reg == 0) {
        // Marine: surface 0.0095 (right at saturation for T=285, p=100000;
        // saturation adjustment will form a thin near-surface fog layer at
        // equilibrium). Above inversion, much drier.
        for (int iz = 0; iz < NZ; iz++) {
            qv_ref[iz] = (iz < 8) ? 0.0095f : (0.0095f * 0.3f);
        }
    } else {
        // Cumulus: target ~80% RH in BL. Cap at 0.95*qvs everywhere so the
        // reference state is strictly subsaturated -- a constant 0.013 would
        // cross qvs above ~900 m and trigger spurious condensation in the
        // undisturbed state, breaking hydrostatic rest.
        for (int iz = 0; iz < NZ; iz++) {
            float z = ((float)iz + 0.5f) * DZ;
            float qvs_here = saturation_mixing_ratio(T_ref[iz], p_ref[iz]);
            float qv_target = 0.015f;
            if (z > 1500.0f) qv_target = 0.015f - (z - 1500.0f) * (0.010f / 1500.0f);
            float qv_cap = 0.95f * qvs_here;
            float qv = qv_target < qv_cap ? qv_target : qv_cap;
            if (qv < 0.001f) qv = 0.001f;
            qv_ref[iz] = qv;
        }
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

    // Sides
    if (regime == 0) {
        // Marine: west inflow, east outflow zero-gradient
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
    // Cumulus: periodic in x is enforced by bilinear_sample wrapping; nothing
    // to do at side cells beyond what advection naturally produces.
}

// --- Bilinear sample (semi-Lagrangian backtrace) ---
//
// `regime` switches the x-boundary handling: marine clamps, cumulus wraps.
//
static float bilinear_sample(const float *field, float x_m, float z_m) {
    float fx = x_m / DX - 0.5f;
    float fz = z_m / DZ - 0.5f;

    // z is always clamped (rigid top, surface bottom).
    if (fz < 0.0f) fz = 0.0f;
    if (fz > (float)(NZ - 1) - 0.001f) fz = (float)(NZ - 1) - 0.001f;

    int i0, i1;
    float a;
    if (regime == 1) {
        // Periodic in x.
        float fxw = fx;
        // Wrap to [0, NX). fmodf-free.
        while (fxw < 0.0f)              fxw += (float)NX;
        while (fxw >= (float)NX)        fxw -= (float)NX;
        int ii = (int)fxw;
        if (ii < 0) ii = 0;
        if (ii >= NX) ii = NX - 1;
        a = fxw - (float)ii;
        i0 = ii;
        i1 = (ii + 1) % NX;
    } else {
        // Clamp in x.
        if (fx < 0.0f) fx = 0.0f;
        if (fx > (float)(NX - 1) - 0.001f) fx = (float)(NX - 1) - 0.001f;
        i0 = (int)fx;
        i1 = i0 + 1;
        a = fx - (float)i0;
    }

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
            if (regime == 1) {
                ip = (ip >= NX) ? 0 : ip;
                im = (im < 0) ? NX - 1 : im;
            } else {
                if (ip >= NX) ip = NX - 1;
                if (im < 0)   im = 0;
            }
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
                if (regime == 1) {
                    ip = (ip >= NX) ? 0 : ip;
                    im = (im < 0) ? NX - 1 : im;
                } else {
                    if (ip >= NX) ip = NX - 1;
                    if (im < 0)   im = 0;
                }
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
            if (regime == 1) {
                ip = (ip >= NX) ? 0 : ip;
                im = (im < 0) ? NX - 1 : im;
            } else {
                if (ip >= NX) ip = NX - 1;
                if (im < 0)   im = 0;
            }
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

static void maybe_trigger_bubble(void) {
    if (!periodic_bubbles_enabled) return;
    if (regime != 1) return;
    if (frame_count > 0 && (frame_count % BUBBLE_PERIOD) == 0) {
        float xc = randf() * (float)NX * DX;
        apply_warm_bubble(xc, BUBBLE_Z_M, BUBBLE_DT_K, BUBBLE_RADIUS_M);
    }
}

// --- Render ---
//
// Two endpoint styles blended by `style` in [0,1]:
//   s=0 -- SF marine layer: cool palette, 5 tones, Bayer 4x4 dither,
//          banded vertical sky gradient.
//   s=1 -- Ghibli: 3 tones, no dither, smoothstep edges on density,
//          flat saturated sky.
//
// Slider interpolation: tone count 5..3, dither amplitude 1..0, sky bands
// many..1, palette colors lerped per channel. Cloud density also gets
// sharper at s=1 (smoothstep on qc threshold).
//
// All work done by sampling qc bilinearly at output resolution -- sim grid
// independence per the spec. Renderer reads qc only.

static const unsigned char BAYER4[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5,
};

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Bilinear qc sample in world coordinates. Out-of-domain returns 0 (clear sky).
static float qc_world(float x_m, float z_m) {
    if (z_m < 0.0f || z_m > (NZ - 1) * DZ) return 0.0f;
    float fx = x_m / DX - 0.5f;
    float fz = z_m / DZ - 0.5f;
    if (fx < 0.0f) fx = 0.0f;
    if (fx > (float)(NX - 1) - 0.001f) fx = (float)(NX - 1) - 0.001f;
    if (fz < 0.0f) fz = 0.0f;
    if (fz > (float)(NZ - 1) - 0.001f) fz = (float)(NZ - 1) - 0.001f;
    int i = (int)fx, j = (int)fz;
    float a = fx - (float)i, b = fz - (float)j;
    float f00 = qc_field[IX(i, j)];
    float f10 = qc_field[IX(i + 1, j)];
    float f01 = qc_field[IX(i, j + 1)];
    float f11 = qc_field[IX(i + 1, j + 1)];
    return (1-a)*(1-b)*f00 + a*(1-b)*f10 + (1-a)*b*f01 + a*b*f11;
}

// Marine palette (5 tones, deepest -> brightest), cool blue-grays
static const unsigned char MARINE[5][3] = {
    { 90, 105, 120},   // deep shadow
    {130, 145, 160},   // shadow
    {170, 180, 190},   // mid
    {205, 212, 220},   // hilite
    {235, 238, 242},   // brightest
};

// Ghibli palette (3 tones), bright soft cumulus
static const unsigned char GHIBLI[3][3] = {
    {184, 200, 220},   // cool shadow
    {228, 236, 244},   // mid (most of the body)
    {252, 253, 255},   // highlight
};

// Sky endpoints
static const unsigned char SKY_MARINE_HORIZON[3] = {200, 210, 218};
static const unsigned char SKY_MARINE_ZENITH[3]  = {120, 145, 175};
static const unsigned char SKY_GHIBLI_HORIZON[3] = {175, 215, 240};
static const unsigned char SKY_GHIBLI_ZENITH[3]  = {110, 175, 220};

static void render(void) {
    float s = style;
    float dither_amp = (1.0f - s) * 0.10f;
    float sky_bandedness = (1.0f - s);  // 1 -> banded, 0 -> smooth single tone

    // Sun direction (upper-right, world units; not normalized -- ray steps
    // are in meters along this direction)
    const float SUN_DX = -1.0f;
    const float SUN_DZ = 2.0f;

    for (int py = 0; py < OUT_H; py++) {
        // py=0 is top of image. Map to z (m): top -> z=NZ*DZ.
        float z_m = (1.0f - (float)py / (float)(OUT_H - 1)) * ((float)NZ * DZ);
        // sky_t: 0 at horizon, 1 at zenith
        float sky_t = (float)(OUT_H - 1 - py) / (float)(OUT_H - 1);

        // Banded sky: snap sky_t to 6 bands at s=0, smooth at s=1
        float sky_t_banded;
        {
            int bands = 6;
            float snap = ((float)((int)(sky_t * bands)) + 0.5f) / (float)bands;
            sky_t_banded = sky_t + (snap - sky_t) * sky_bandedness;
        }

        for (int px = 0; px < OUT_W; px++) {
            float x_m = (float)px / (float)(OUT_W - 1) * ((float)NX * DX);

            float qc_here = qc_world(x_m, z_m);

            // Optical thickness toward the sun: integrate qc along ray
            float thick = 0.0f;
            for (int k = 1; k <= 6; k++) {
                float step = 60.0f;  // meters per ray sample
                float xs = x_m + SUN_DX * (float)k * step;
                float zs = z_m + SUN_DZ * (float)k * step;
                thick += qc_world(xs, zs) * step;
            }

            // Cloud density [0..1]. s=0 softer (more thickness gradients),
            // s=1 sharper (smoothstep on qc threshold for crisp Ghibli edges).
            float d_marine = qc_here * 60000.0f;
            float d_ghibli;
            {
                float t = (qc_here - 0.00003f) / 0.00015f;
                t = clampf(t, 0.0f, 1.0f);
                d_ghibli = t * t * (3.0f - 2.0f * t);
            }
            float density = (1.0f - s) * d_marine + s * d_ghibli;
            density = clampf(density, 0.0f, 1.0f);

            // Light "shade" inside cloud: more thickness above -> darker
            float t_above = clampf(thick * 4500.0f, 0.0f, 1.0f);
            float shade_marine = 1.0f - 0.55f * t_above;
            // Ghibli: just two flat zones (highlight on edges, body mid)
            float shade_ghibli = (t_above < 0.25f) ? 1.0f : 0.55f;
            float shade = (1.0f - s) * shade_marine + s * shade_ghibli;

            // Bayer dither (s=0 only, fades out)
            float dither = ((float)BAYER4[(py & 3) * 4 + (px & 3)] / 16.0f - 0.46875f);
            shade += dither * dither_amp;
            shade = clampf(shade, 0.0f, 1.0f);

            // Pick cloud color via shade -> palette index, lerp marine and Ghibli.
            unsigned char r_cloud, g_cloud, b_cloud;
            {
                // Marine: 5 tones, indexed by shade
                int mi = (int)(shade * 4.0f + 0.5f);
                if (mi < 0) mi = 0; if (mi > 4) mi = 4;
                // Ghibli: 3 tones
                int gi = (int)(shade * 2.0f + 0.5f);
                if (gi < 0) gi = 0; if (gi > 2) gi = 2;
                float r = (1.0f - s) * MARINE[mi][0] + s * GHIBLI[gi][0];
                float g = (1.0f - s) * MARINE[mi][1] + s * GHIBLI[gi][1];
                float b = (1.0f - s) * MARINE[mi][2] + s * GHIBLI[gi][2];
                r_cloud = (unsigned char)r;
                g_cloud = (unsigned char)g;
                b_cloud = (unsigned char)b;
            }

            // Sky: vertical gradient between horizon and zenith, lerp endpoints
            unsigned char r_sky, g_sky, b_sky;
            {
                float t = sky_t_banded;
                float r_m = (1.0f - t) * SKY_MARINE_HORIZON[0] + t * SKY_MARINE_ZENITH[0];
                float g_m = (1.0f - t) * SKY_MARINE_HORIZON[1] + t * SKY_MARINE_ZENITH[1];
                float b_m = (1.0f - t) * SKY_MARINE_HORIZON[2] + t * SKY_MARINE_ZENITH[2];
                float r_g = (1.0f - t) * SKY_GHIBLI_HORIZON[0] + t * SKY_GHIBLI_ZENITH[0];
                float g_g = (1.0f - t) * SKY_GHIBLI_HORIZON[1] + t * SKY_GHIBLI_ZENITH[1];
                float b_g = (1.0f - t) * SKY_GHIBLI_HORIZON[2] + t * SKY_GHIBLI_ZENITH[2];
                r_sky = (unsigned char)((1.0f - s) * r_m + s * r_g);
                g_sky = (unsigned char)((1.0f - s) * g_m + s * g_g);
                b_sky = (unsigned char)((1.0f - s) * b_m + s * b_g);
            }

            // Composite
            unsigned char r_out, g_out, b_out;
            {
                float a = density;
                r_out = (unsigned char)((1.0f - a) * r_sky + a * r_cloud);
                g_out = (unsigned char)((1.0f - a) * g_sky + a * g_cloud);
                b_out = (unsigned char)((1.0f - a) * b_sky + a * b_cloud);
            }

            unsigned char *p = &pixels[(py * OUT_W + px) * 4];
            p[0] = r_out;
            p[1] = g_out;
            p[2] = b_out;
            p[3] = 255;
        }
    }
}

// --- Public API ---

EXPORT("clouds_init")
void clouds_init(void) {
    regime = 1;
    mean_wind = 4.0f;
    style = 1.0f;
    frame_count = 0;
    rng_state = 0xC0DEC0DEu;
    build_reference_profile(regime);
    init_fields();
    apply_boundary_conditions();
}

EXPORT("clouds_step")
void clouds_step(int n) {
    for (int s = 0; s < n; s++) {
        maybe_trigger_bubble();
        advect_all(DT);
        apply_buoyancy(DT);
        pressure_project(DT);
        saturation_adjustment();
        apply_boundary_conditions();
        frame_count++;
    }
    render();
}

EXPORT("clouds_set_regime")
void clouds_set_regime(int r) {
    regime = (r == 0) ? 0 : 1;
    build_reference_profile(regime);
    init_fields();
    apply_boundary_conditions();
}

EXPORT("clouds_set_style")
void clouds_set_style(float s) {
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    style = s;
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

EXPORT("clouds_pixels")
unsigned char *clouds_pixels(void) {
    return pixels;
}

#ifdef NATIVE_TEST

void clouds_trigger_bubble(float x_m, float z_m, float dT_K, float radius_m) {
    apply_warm_bubble(x_m, z_m, dT_K, radius_m);
}

const float *clouds_qc(void) { return qc_field; }
const float *clouds_w(void) { return w_field; }
const float *clouds_T(void) { return T_field; }
const float *clouds_qv(void) { return qv_field; }

// Dump a 2D float field as a PGM. Min/max are auto-scaled if vmax == vmin.
static void dump_pgm(const char *path, const float *field, float vmin, float vmax) {
    if (vmax <= vmin) {
        vmin = field[0]; vmax = field[0];
        for (int i = 1; i < N; i++) {
            if (field[i] < vmin) vmin = field[i];
            if (field[i] > vmax) vmax = field[i];
        }
        if (vmax - vmin < 1e-12f) vmax = vmin + 1e-12f;
    }
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P5\n%d %d\n255\n", NX, NZ);
    // Write rows top-to-bottom (high z first) so the image looks "up"
    for (int iz = NZ - 1; iz >= 0; iz--) {
        for (int ix = 0; ix < NX; ix++) {
            float v = (field[IX(ix, iz)] - vmin) / (vmax - vmin);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            unsigned char b = (unsigned char)(v * 255.0f);
            fputc(b, f);
        }
    }
    fclose(f);
}

static float total_water(void) {
    float sum = 0.0f;
    for (int iz = 0; iz < NZ; iz++) {
        for (int ix = 0; ix < NX; ix++) {
            int i = IX(ix, iz);
            sum += (qv_field[i] + qc_field[i]) * rho_ref[iz];
        }
    }
    return sum;
}

static float max_abs_w(void) {
    float m = 0.0f;
    for (int i = 0; i < N; i++) {
        float a = w_field[i] < 0.0f ? -w_field[i] : w_field[i];
        if (a > m) m = a;
    }
    return m;
}

static float max_qc(void) {
    float m = 0.0f;
    for (int i = 0; i < N; i++) if (qc_field[i] > m) m = qc_field[i];
    return m;
}

// Highest iz where qc exceeds threshold (visual cloud-top diagnostic).
static int qc_top_iz(float threshold) {
    int top = -1;
    for (int iz = NZ - 1; iz >= 0; iz--) {
        for (int ix = 0; ix < NX; ix++) {
            if (qc_field[IX(ix, iz)] > threshold) { top = iz; break; }
        }
        if (top >= 0) break;
    }
    return top;
}

static void run_validation(const char *label, int reg, float wind, int seed_pattern,
                          int n_steps, int dump_every, const char *out_dir) {
    fprintf(stderr, "\n=== %s ===\n", label);
    clouds_set_regime(reg);
    clouds_set_wind(wind);
    clouds_seed(seed_pattern);

    float water0 = total_water();
    fprintf(stderr, "initial total water = %.6e\n", water0);

    for (int step = 0; step <= n_steps; step++) {
        if (step > 0) clouds_step(1);
        if ((step % dump_every) == 0) {
            char path[256];
            snprintf(path, sizeof(path), "%s/qc_%s_%04d.pgm", out_dir, label, step);
            dump_pgm(path, qc_field, 0.0f, 0.003f);
            float water = total_water();
            float water_drift = (water - water0) / water0 * 100.0f;
            fprintf(stderr, "  step %4d  max|w|=%.3f m/s  max qc=%.5f  qc top iz=%d  water drift=%+.2f%%\n",
                    step, max_abs_w(), max_qc(), qc_top_iz(1e-5f), water_drift);
        }
    }
}

int main(int argc, char **argv) {
    const char *out_dir = (argc > 1) ? argv[1] : ".";

    fprintf(stderr, "Cloud sim native test driver\n");
    fprintf(stderr, "out_dir = %s\n", out_dir);

    // For all validation runs, suppress the periodic-bubble auto-trigger so
    // each test isolates a single phenomenon.
    periodic_bubbles_enabled = 0;

    // Test 1: hydrostatic rest (cumulus, no bubble) -- should show no growth.
    clouds_init();
    clouds_seed(0);
    fprintf(stderr, "\n=== Hydrostatic rest (cumulus, no bubble, 600 steps) ===\n");
    float water0 = total_water();
    for (int step = 0; step < 600; step++) {
        clouds_step(1);
        if ((step % 100) == 99) {
            fprintf(stderr, "  step %4d  max|w|=%.6e  max qc=%.6e  water drift=%+.3f%%\n",
                    step + 1, max_abs_w(), max_qc(),
                    (total_water() - water0) / water0 * 100.0f);
        }
    }

    // Test 2: cumulus single-bubble validation
    clouds_init();
    run_validation("cumulus_bubble", 1, 0.0f, 1, 600, 30, out_dir);

    // Test 3: marine layer steady state
    clouds_init();
    run_validation("marine", 0, 5.0f, 2, 1200, 60, out_dir);

    // Test 4: render endpoints. Re-run cumulus then marine, and after a
    // representative number of steps dump RGB ppm at s=0, 0.5, 1.
    fprintf(stderr, "\n=== render endpoints ===\n");
    {
        // Cumulus with bubble well-developed (cloud grown ~1 km tall)
        clouds_init();
        clouds_seed(1);
        for (int i = 0; i < 540; i++) clouds_step(1);
        for (int k = 0; k <= 2; k++) {
            float s = (float)k * 0.5f;
            clouds_set_style(s);
            clouds_step(0); // refresh render with new style (step(0) is a no-op for sim, but we call render directly)
            // step(0) doesn't render; call clouds_pixels instead which doesn't render either.
            // We need render to be invoked. Easiest: call clouds_step(1) which
            // will both step physics and render. The extra step is fine.
            clouds_step(1);
            unsigned char *px = clouds_pixels();
            char path[256];
            snprintf(path, sizeof(path), "%s/render_cumulus_s%02d.ppm", out_dir, (int)(s * 10.0f));
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H);
                for (int i = 0; i < OUT_W * OUT_H; i++) {
                    fputc(px[i*4+0], f);
                    fputc(px[i*4+1], f);
                    fputc(px[i*4+2], f);
                }
                fclose(f);
                fprintf(stderr, "  wrote %s\n", path);
            }
        }

        // Marine at step ~600 (steady state)
        clouds_init();
        clouds_set_regime(0);
        clouds_set_wind(5.0f);
        clouds_seed(2);
        for (int i = 0; i < 600; i++) clouds_step(1);
        for (int k = 0; k <= 2; k++) {
            float s = (float)k * 0.5f;
            clouds_set_style(s);
            clouds_step(1);
            unsigned char *px = clouds_pixels();
            char path[256];
            snprintf(path, sizeof(path), "%s/render_marine_s%02d.ppm", out_dir, (int)(s * 10.0f));
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H);
                for (int i = 0; i < OUT_W * OUT_H; i++) {
                    fputc(px[i*4+0], f);
                    fputc(px[i*4+1], f);
                    fputc(px[i*4+2], f);
                }
                fclose(f);
                fprintf(stderr, "  wrote %s\n", path);
            }
        }
    }

    fprintf(stderr, "\nDone. Frames dumped to %s/\n", out_dir);
    return 0;
}

#endif // NATIVE_TEST
