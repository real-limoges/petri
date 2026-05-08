//
// Native test driver for the cloud module. Dumps PGM frames so we can
// eyeball the simulation outside the browser.
//
//   clang -O2 -lm -o test src/clouds_test.c
//   ./test ./out
//
// This translation unit re-includes clouds.c so the driver can reach
// into the module's static fields (qc_field, w_field, ...) and helpers
// (apply_warm_bubble, periodic_bubbles_enabled). The WASM build path
// continues to compile clouds.c on its own — see build_all.sh.
//

#define NATIVE_TEST
#include "clouds.c"  // brings in fields, exports, and the simulator

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void clouds_trigger_bubble(float x_m, float z_m, float dT_K, float radius_m) {
    apply_warm_bubble(x_m, z_m, dT_K, radius_m);
}

const float *clouds_w(void)  { return w_field; }
const float *clouds_T(void)  { return T_field; }
const float *clouds_qv(void) { return qv_field; }

// Dump a 2D float field as a PGM. Auto-scales when vmax <= vmin.
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
    // Top row first so the image reads "up".
    for (int iz = NZ - 1; iz >= 0; iz--) {
        for (int ix = 0; ix < NX; ix++) {
            float v = (field[IX(ix, iz)] - vmin) / (vmax - vmin);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            fputc((unsigned char)(v * 255.0f), f);
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

static void run_validation(const char *label, float w, float wind, int seed_pattern,
                           int n_steps, int dump_every, const char *out_dir) {
    fprintf(stderr, "\n=== %s ===\n", label);
    clouds_set_weather(w);
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
            float drift = (water - water0) / water0 * 100.0f;
            fprintf(stderr, "  step %4d  max|w|=%.3f m/s  max qc=%.5f  qc top iz=%d  water drift=%+.2f%%\n",
                    step, max_abs_w(), max_qc(), qc_top_iz(1e-5f), drift);
        }
    }
}

int main(int argc, char **argv) {
    const char *out_dir = (argc > 1) ? argv[1] : ".";

    fprintf(stderr, "Cloud sim native test driver\n");
    fprintf(stderr, "out_dir = %s\n", out_dir);

    // Suppress the periodic-bubble auto-trigger so each test isolates a
    // single phenomenon.
    periodic_bubbles_enabled = 0;

    // Test 1: hydrostatic rest (no bubble) — should show no growth.
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

    clouds_init();
    run_validation("cumulus_bubble", 1.0f, 0.0f, 1, 600, 30, out_dir);

    clouds_init();
    run_validation("marine", 0.0f, 5.0f, 2, 1200, 60, out_dir);

    fprintf(stderr, "\nDone. Frames dumped to %s/\n", out_dir);
    return 0;
}
