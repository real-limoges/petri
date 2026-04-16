//
// Abelian Sandpile — WASM (optimized)
//
// Hot-path design notes:
//  - Sentinel border: the grid is padded by one cell on every side. Interior
//    cells' four neighbors are always valid indices, so the topple loop has
//    zero bounds checks and zero divmod. Border cells are permanently flagged
//    `scheduled` so that grains falling off the edge land on a cell that is
//    never enqueued — an open boundary for free.
//  - Batch drain: a cell can accumulate above 4 before we reach it in the
//    queue. Drain the whole multiple of 4 in one shot (`k = g >> 2`) and add
//    k to each neighbor, rather than looping until stable.
//  - Power-of-two circular queue: wraparound is a mask, not a modulo. The
//    `scheduled` flag dedupes, bounding concurrent queue entries to N.
//  - Early out: most drops add to a cell in state 0..2 and return
//    immediately without touching the queue.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W 2560
#define MAX_H 1440
#define MAX_STRIDE (MAX_W + 2)
#define MAX_PADDED_N (MAX_STRIDE * (MAX_H + 2))
#define MAX_INTERIOR_N (MAX_W * MAX_H)

// Smallest power of two >= MAX_PADDED_N (~3.69M). 2^22 = 4,194,304.
#define QCAP (1u << 22)
#define QMASK (QCAP - 1u)

static int w = 512, h = 512, n = 512 * 512;
static int stride = 514;

static unsigned char grid[MAX_PADDED_N];
static unsigned char scheduled[MAX_PADDED_N];
static int queue[QCAP];
static unsigned char intensity[MAX_INTERIOR_N];

static int last_avalanche = 0;
static int total_grains = 0;
static int drop_mode = 0; // 0 = center, 1 = random

static unsigned int rng_state = 1337;
static unsigned int randu(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state & 0x7fffffff;
}

static void reset_padded(void) {
    int padded = stride * (h + 2);
    memset(grid, 0, padded);
    memset(scheduled, 0, padded);

    // Lock border cells as "scheduled" so they never enter the queue.
    for (int x = 0; x < stride; x++) {
        scheduled[x] = 1;
        scheduled[(h + 1) * stride + x] = 1;
    }
    for (int y = 0; y < h + 2; y++) {
        scheduled[y * stride] = 1;
        scheduled[y * stride + (w + 1)] = 1;
    }
}

static inline int pidx(int x, int y) { return (y + 1) * stride + (x + 1); }

__attribute__((export_name("sandpile_init")))
void sandpile_init(int width, int height) {
    if (width > MAX_W) width = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;
    stride = w + 2;

    reset_padded();
    memset(intensity, 0, n);
    last_avalanche = 0;
    total_grains = 0;
}

__attribute__((export_name("sandpile_set_mode")))
void sandpile_set_mode(int m) { drop_mode = m ? 1 : 0; }

static void relax(int seed) {
    if (grid[seed] < 4) return;

    unsigned int head = 0, tail = 0;
    int topples = 0;

    queue[tail++ & QMASK] = seed;
    scheduled[seed] = 1;

    const int s = stride;

    while (head != tail) {
        int idx = queue[head++ & QMASK];
        scheduled[idx] = 0;

        unsigned int g = grid[idx];
        if (g < 4) continue;

        unsigned int k = g >> 2;
        grid[idx] = (unsigned char)(g & 3u);
        topples += (int)k;

        int l = idx - 1;
        int r = idx + 1;
        int u = idx - s;
        int d = idx + s;

        grid[l] += (unsigned char)k;
        grid[r] += (unsigned char)k;
        grid[u] += (unsigned char)k;
        grid[d] += (unsigned char)k;

        if (grid[l] >= 4 && !scheduled[l]) {
            queue[tail++ & QMASK] = l;
            scheduled[l] = 1;
        }
        if (grid[r] >= 4 && !scheduled[r]) {
            queue[tail++ & QMASK] = r;
            scheduled[r] = 1;
        }
        if (grid[u] >= 4 && !scheduled[u]) {
            queue[tail++ & QMASK] = u;
            scheduled[u] = 1;
        }
        if (grid[d] >= 4 && !scheduled[d]) {
            queue[tail++ & QMASK] = d;
            scheduled[d] = 1;
        }
    }

    last_avalanche = topples;
}

__attribute__((export_name("sandpile_drop")))
void sandpile_drop(int x, int y) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    int idx = pidx(x, y);
    grid[idx]++;
    total_grains++;
    relax(idx);
}

__attribute__((export_name("sandpile_drop_center")))
void sandpile_drop_center(void) {
    int idx = pidx(w / 2, h / 2);
    grid[idx]++;
    total_grains++;
    relax(idx);
}

__attribute__((export_name("sandpile_drop_random")))
void sandpile_drop_random(void) {
    int x = (int)(randu() % (unsigned int)w);
    int y = (int)(randu() % (unsigned int)h);
    int idx = pidx(x, y);
    grid[idx]++;
    total_grains++;
    relax(idx);
}

__attribute__((export_name("sandpile_step")))
void sandpile_step(int count) {
    int total = 0;
    for (int i = 0; i < count; i++) {
        int idx;
        if (drop_mode == 0) {
            idx = pidx(w / 2, h / 2);
        } else {
            int x = (int)(randu() % (unsigned int)w);
            int y = (int)(randu() % (unsigned int)h);
            idx = pidx(x, y);
        }
        grid[idx]++;
        total_grains++;
        relax(idx);
        total += last_avalanche;
    }
    last_avalanche = total;
}

__attribute__((export_name("sandpile_last_avalanche_size")))
int sandpile_last_avalanche_size(void) { return last_avalanche; }

__attribute__((export_name("sandpile_total_grains")))
int sandpile_total_grains(void) { return total_grains; }

__attribute__((export_name("sandpile_pixels")))
unsigned char* sandpile_pixels(void) {
    // Map 0..3 to 0/85/170/255 with a local lookup. Walk padded rows with a
    // stride pointer so we don't recompute (y+1)*stride per cell.
    static const unsigned char lut[8] = { 0, 85, 170, 255, 255, 255, 255, 255 };
    for (int y = 0; y < h; y++) {
        const unsigned char *src = &grid[(y + 1) * stride + 1];
        unsigned char *dst = &intensity[y * w];
        for (int x = 0; x < w; x++) {
            dst[x] = lut[src[x] & 7];
        }
    }
    return intensity;
}
