// meshoptimizer glue for the D3D9 bridge (see d3d9_bridge.cpp, "meshoptimizer").
#pragma once
#include <stddef.h>

// True when the port was built with meshoptimizer (third_party/meshoptimizer/src present).
bool lb_meshopt_available(void);

// Reorders the triangles of a triangle-list index range for post-transform
// vertex cache efficiency. `in` and `out` hold `indexCount` 16- or 32-bit
// indices (indexCount % 3 == 0) and must not overlap. The set of triangles and
// every triangle's winding are unchanged. Returns false (out untouched) if the
// range cannot be processed.
bool lb_meshopt_vertex_cache(const void *in, void *out, bool is32, size_t indexCount);
