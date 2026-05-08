//
// Abelian sandpile — WASM
//
// Drop a grain on a cell. If the cell has ≥4 grains it topples, sending
// one grain to each cardinal neighbour. Toppling cascades. The order of
// toppling does not affect the final configuration (the abelian property),
// which is the only reason the queue-based hot path below is safe.
//
// Hot-path design notes:
//
//  * Sentinel border. The grid is padded by one cell on every side, with
//    border cells permanently flagged `scheduled`. Interior cells' four
//    neighbours are always valid indices, so the topple loop has zero
//    bounds checks. Grains falling onto a border cell never re-enter the
//    queue — that is the open boundary.
//
//  * Batch drain. A cell can accumulate above 4 before the queue catches
//    it. Drain the whole multiple of 4 in one shot (k = g >> 2) and add k
//    to each neighbour rather than looping. With realistic drop patterns
//    cells transiently hold at most ~5 grains, so `unsigned char` is
//    safely wide; doubling the type would change the WASM memory layout
//    and the --initial-memory bound.
//
//  * Power-of-two ring queue. Wraparound is a mask (QMASK), not a modulo.
//    The `scheduled` flag dedupes, so concurrent queue entries are bounded
//    by N (one per interior cell) — well below QCAP.
//
//  * Early out. Most drops add to a cell in state 0..2 and return without
//    touching the queue at all.
//

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define MAX_W          2560
#define MAX_H          1440
#define MAX_STRIDE     (MAX_W + 2)
#define MAX_PADDED_N   (MAX_STRIDE * (MAX_H + 2))
#define MAX_INTERIOR_N (MAX_W * MAX_H)

// Smallest power of two ≥ MAX_PADDED_N (~3.69M cells). 2^22 = 4,194,304.
#define QCAP  (1u << 22)
#define QMASK (QCAP - 1u)

// Drop modes.
#define DROP_CENTER 0
#define DROP_RANDOM 1

static int w = 512, h = 512, n = 512 * 512;
static int stride = 514;

static unsigned char grid[MAX_PADDED_N];
static unsigned char scheduled[MAX_PADDED_N];
static int           queue[QCAP];
static unsigned char intensity[MAX_INTERIOR_N];

static int last_avalanche = 0;   // grains toppled by the most recent drop
static int total_grains   = 0;
static int drop_mode      = DROP_CENTER;

static unsigned int rng_state = 1337;
static unsigned int randu(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state & 0x7fffffff;
}

static inline int pidx(int x, int y) { return (y + 1) * stride + (x + 1); }

static void reset_padded(void) {
    int padded = stride * (h + 2);
    memset(grid, 0, padded);
    memset(scheduled, 0, padded);

    // Lock border cells as `scheduled` so they never enter the queue.
    for (int x = 0; x < stride; x++) {
        scheduled[x] = 1;
        scheduled[(h + 1) * stride + x] = 1;
    }
    for (int y = 0; y < h + 2; y++) {
        scheduled[y * stride] = 1;
        scheduled[y * stride + (w + 1)] = 1;
    }
}

__attribute__((export_name("sandpile_init")))
void sandpile_init(int width, int height) {
    if (width  > MAX_W) width  = MAX_W;
    if (height > MAX_H) height = MAX_H;
    w = width; h = height; n = w * h;
    stride = w + 2;

    reset_padded();
    memset(intensity, 0, n);
    last_avalanche = 0;
    total_grains   = 0;
}

__attribute__((export_name("sandpile_set_mode")))
void sandpile_set_mode(int m) { drop_mode = m ? DROP_RANDOM : DROP_CENTER; }

// Enqueue p if it is now over-full and not already in the queue. Border
// cells stay flagged `scheduled` permanently, so this is the boundary
// guard as well as the dedup guard. Macro-form because the four call
// sites are inside the hottest inner loop in the program.
#define ENQ_IF(p) do {                                  \
    if (grid[(p)] >= 4 && !scheduled[(p)]) {            \
        queue[tail++ & QMASK] = (p);                    \
        scheduled[(p)] = 1;                             \
    }                                                   \
} while (0)

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

        ENQ_IF(l);
        ENQ_IF(r);
        ENQ_IF(u);
        ENQ_IF(d);
    }

    last_avalanche = topples;
}

#undef ENQ_IF

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
        if (drop_mode == DROP_CENTER) {
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
    // last_avalanche after a multi-step batch reports the *summed* topple
    // count for the batch, so the host can use it as a single throughput
    // metric. After a single drop or step(1) it is per-drop.
    last_avalanche = total;
}

__attribute__((export_name("sandpile_last_avalanche_size")))
int sandpile_last_avalanche_size(void) { return last_avalanche; }

__attribute__((export_name("sandpile_total_grains")))
int sandpile_total_grains(void) { return total_grains; }

__attribute__((export_name("sandpile_pixels")))
unsigned char* sandpile_pixels(void) {
    // Map cell counts to brightness. Indices 0..3 are the four stable
    // levels; 4..7 saturate the output for cells caught mid-relax (we may
    // render between drops). The `& 7` masks the rare transient values.
    static const unsigned char lut[8] = { 0, 85, 170, 255, 255, 255, 255, 255 };

    for (int y = 0; y < h; y++) {
        const unsigned char *src = &grid[(y + 1) * stride + 1];
        unsigned char       *dst = &intensity[y * w];
        for (int x = 0; x < w; x++) dst[x] = lut[src[x] & 7];
    }
    return intensity;
}
