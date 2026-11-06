#include "meshopt_hook.h"
#include <stdint.h>
#include <string.h>
#include <vector>

#ifdef LB_MESHOPT
#include "meshoptimizer.h"

bool lb_meshopt_available(void) { return true; }

bool lb_meshopt_vertex_cache(const void *in, void *out, bool is32, size_t n) {
    if (n < 3 || n % 3) return false;
    std::vector<uint32_t> src(n), dst(n);
    if (is32) {
        memcpy(src.data(), in, n * 4);
    } else {
        const uint8_t *p = (const uint8_t *)in;
        for (size_t i = 0; i < n; i++) { uint16_t v; memcpy(&v, p + 2 * i, 2); src[i] = v; }
    }
    uint32_t lo = ~0u, hi = 0;
    for (size_t i = 0; i < n; i++) { if (src[i] < lo) lo = src[i]; if (src[i] > hi) hi = src[i]; }
    if (hi - lo >= 0x7fffffffu) return false;
    // D3D9 indices are relative to a base/min vertex; meshoptimizer wants them from 0.
    for (size_t i = 0; i < n; i++) src[i] -= lo;
    meshopt_optimizeVertexCache(dst.data(), src.data(), n, (size_t)(hi - lo) + 1);
    if (is32) {
        for (size_t i = 0; i < n; i++) dst[i] += lo;
        memcpy(out, dst.data(), n * 4);
    } else {
        uint8_t *p = (uint8_t *)out;
        for (size_t i = 0; i < n; i++) { uint16_t v = (uint16_t)(dst[i] + lo); memcpy(p + 2 * i, &v, 2); }
    }
    return true;
}
#else
bool lb_meshopt_available(void) { return false; }
bool lb_meshopt_vertex_cache(const void *, void *, bool, size_t) { return false; }
#endif
