//
// Multiple Langton's Ants — WASM
// Simple state machines that produce complex emergent highways
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W 2560
#define MAX_H 1440
#define MAX_N (MAX_W * MAX_H)
#define MAX_ANTS 12

static int w = 1024, h = 1024, n = 1024 * 1024;

static unsigned char grid[MAX_N];  // 0 = white, 1 = black
static float heat[MAX_N];          // visit frequency for coloring
static unsigned char intensity[MAX_N];

static int ant_x[MAX_ANTS];
static int ant_y[MAX_ANTS];
static int ant_dir[MAX_ANTS]; // 0=up 1=right 2=down 3=left
static int num_ants = 0;

__attribute__((export_name("langton_init")))
void langton_init(int count, int width, int height) {
    if (width > MAX_W) width = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;

    if (count > MAX_ANTS) count = MAX_ANTS;
    num_ants = count;
    memset(grid, 0, n);
    memset(heat, 0, n * sizeof(float));

    // place ants spread across the grid
    for (int i = 0; i < count; i++) {
        ant_x[i] = w / 2 + (i - count / 2) * (w / (count + 1));
        ant_y[i] = h / 2 + ((i % 3) - 1) * (h / 4);
        ant_dir[i] = i % 4;
    }
}

__attribute__((export_name("langton_step")))
void langton_step(int steps) {
    for (int s = 0; s < steps; s++) {
        for (int a = 0; a < num_ants; a++) {
            int idx = ant_y[a] * w + ant_x[a];

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
            ant_x[a] = (ant_x[a] + w) % w;
            ant_y[a] = (ant_y[a] + h) % h;
        }
    }
}

__attribute__((export_name("langton_pixels")))
unsigned char* langton_pixels(void) {
    for (int i = 0; i < n; i++) {
        heat[i] *= 0.9999f;

        float t = heat[i] / 10.0f;
        if (t > 1.0f) t = 1.0f;
        intensity[i] = (unsigned char)(t * 255.0f);
    }
    return intensity;
}
