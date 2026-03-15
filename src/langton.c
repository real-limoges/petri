//
// Multiple Langton's Ants — WASM
// Simple state machines that produce complex emergent highways
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define W 1024
#define H 1024
#define N (W * H)
#define MAX_ANTS 12

static unsigned char grid[N];  // 0 = white, 1 = black
static float heat[N];          // visit frequency for coloring
static unsigned char pixels[N * 4];

static int ant_x[MAX_ANTS];
static int ant_y[MAX_ANTS];
static int ant_dir[MAX_ANTS]; // 0=up 1=right 2=down 3=left
static int num_ants = 0;

__attribute__((export_name("langton_init")))
void langton_init(int count) {
    if (count > MAX_ANTS) count = MAX_ANTS;
    num_ants = count;
    memset(grid, 0, sizeof(grid));
    memset(heat, 0, sizeof(heat));

    // place ants spread across the grid
    for (int i = 0; i < count; i++) {
        ant_x[i] = W / 2 + (i - count / 2) * (W / (count + 1));
        ant_y[i] = H / 2 + ((i % 3) - 1) * (H / 4);
        ant_dir[i] = i % 4;
    }
}

__attribute__((export_name("langton_step")))
void langton_step(int steps) {
    for (int s = 0; s < steps; s++) {
        for (int a = 0; a < num_ants; a++) {
            int idx = ant_y[a] * W + ant_x[a];

            if (grid[idx] == 0) {
                // on white: turn right, flip to black
                ant_dir[a] = (ant_dir[a] + 1) % 4;
                grid[idx] = 1;
            } else {
                // on black: turn left, flip to white
                ant_dir[a] = (ant_dir[a] + 3) % 4;
                grid[idx] = 0;
            }

            // heat map
            heat[idx] += 0.3f;
            if (heat[idx] > 10.0f) heat[idx] = 10.0f;

            // move
            switch (ant_dir[a]) {
                case 0: ant_y[a]--; break;
                case 1: ant_x[a]++; break;
                case 2: ant_y[a]++; break;
                case 3: ant_x[a]--; break;
            }
            // wrap
            ant_x[a] = (ant_x[a] + W) % W;
            ant_y[a] = (ant_y[a] + H) % H;
        }
    }
}

__attribute__((export_name("langton_pixels")))
unsigned char* langton_pixels(void) {
    // decay heat slowly
    for (int i = 0; i < N; i++) {
        heat[i] *= 0.9999f;

        float t = heat[i] / 10.0f;
        if (t > 1.0f) t = 1.0f;

        int pi = i * 4;
        if (grid[i]) {
            // black cells: tinted by heat
            pixels[pi]     = 8 + (unsigned char)(t * 180.0f);
            pixels[pi + 1] = 6 + (unsigned char)(t * 140.0f);
            pixels[pi + 2] = 10 + (unsigned char)(t * 100.0f);
        } else {
            // white cells: dim base + heat glow
            pixels[pi]     = 8 + (unsigned char)(t * 60.0f);
            pixels[pi + 1] = 6 + (unsigned char)(t * 50.0f);
            pixels[pi + 2] = 10 + (unsigned char)(t * 40.0f);
        }
        pixels[pi + 3] = 255;
    }
    return pixels;
}
