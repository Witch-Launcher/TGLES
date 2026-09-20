#ifndef TGLES_APPS_CUBE_RENDER_H
#define TGLES_APPS_CUBE_RENDER_H

// CPU frame builder for lit 3D rendering without a depth buffer.
// Pipeline per frame: rotate vertices by the model matrix, compute
// per-triangle world normals, shade with a directional diffuse light,
// depth-sort back-to-front, emit interleaved xyz+rgba streams for the
// bridge (float4 layout, stride 32).
//
// Conventions (all documented, all unit-tested):
// - Camera sits at +Z looking toward -Z, so SMALLER world z is FARTHER;
//   sort is stable ascending by average triangle world z.
// - Flat shading: one normal/color per triangle (correct look for a cube).
// - Lighting: out = base * (kAmbient + (1-kAmbient) * max(dot(n,l),0))
//   with kAmbient = 0.25. Light dir is used as-is (pass it normalized).
// - Degenerate (zero-area) triangles get normal +Z rather than NaN.

#include <cmath>
#include <cstddef>
#include <vector>

#include "cube_model.h"

namespace tgles {
namespace trial {

inline constexpr float kRenderAmbient = 0.25f;

inline void TriNormal(const float a[3], const float b[3], const float c[3],
                      float out_n[3]) {
  const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
  const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
  float nx = uy * vz - uz * vy;
  float ny = uz * vx - ux * vz;
  float nz = ux * vy - uy * vx;
  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len <= 0.0f) {
    out_n[0] = 0.0f;
    out_n[1] = 0.0f;
    out_n[2] = 1.0f;
    return;
  }
  out_n[0] = nx / len;
  out_n[1] = ny / len;
  out_n[2] = nz / len;
}

inline void ShadeFace(const float base_rgb[3], const float normal[3],
                      const float light_dir[3], float out_rgb[3]) {
  const float d = normal[0] * light_dir[0] + normal[1] * light_dir[1] +
                  normal[2] * light_dir[2];
  const float f =
      kRenderAmbient + (1.0f - kRenderAmbient) * (d > 0.0f ? d : 0.0f);
  out_rgb[0] = base_rgb[0] * f;
  out_rgb[1] = base_rgb[1] * f;
  out_rgb[2] = base_rgb[2] * f;
}

// Stable insertion sort of triangle indices by ascending average world z.
// `positions` holds tri_count*9 floats (xyz per vert); `model` maps them
// to world space. `order` is input 0..n-1, output sorted.
inline void SortedTriOrder(const float* positions, std::size_t tri_count,
                           const Mat4& model, unsigned* order) {
  for (std::size_t i = 1; i < tri_count; ++i) {
    // Average world z of triangle i (column-major model, affine).
    float zi = 0.0f;
    for (int v = 0; v < 3; ++v) {
      const float* p = positions + (order[i] * 3 + v) * 3;
      zi += model.m[2] * p[0] + model.m[6] * p[1] + model.m[10] * p[2] +
            model.m[14];
    }
    zi /= 3.0f;
    unsigned key = order[i];
    std::size_t j = i;
    while (j > 0) {
      float zj = 0.0f;
      for (int v = 0; v < 3; ++v) {
        const float* p = positions + (order[j - 1] * 3 + v) * 3;
        zj += model.m[2] * p[0] + model.m[6] * p[1] + model.m[10] * p[2] +
              model.m[14];
      }
      zj /= 3.0f;
      if (zj <= zi) break;
      order[j] = order[j - 1];
      --j;
    }
    order[j] = key;
  }
}

// Full frame: model-transformed, lit, depth-sorted xyz+rgba streams.
// `vertex_count` must be a multiple of 3. MVP stays P*V in the shader;
// the model matrix is baked on the CPU.
inline void BuildFrameStreams(const float* positions_xyz,
                              const float* colors_rgb,
                              std::size_t vertex_count, const Mat4& model,
                              const float light_dir[3], float* out_xyz,
                              float* out_rgba) {
  const std::size_t tris = vertex_count / 3;
  std::vector<unsigned> order(tris);
  for (std::size_t t = 0; t < tris; ++t) {
    order[t] = static_cast<unsigned>(t);
  }
  SortedTriOrder(positions_xyz, tris, model, order.data());
  for (std::size_t e = 0; e < tris; ++e) {
    const unsigned t = order[e];
    float world[3][3];
    for (int v = 0; v < 3; ++v) {
      const float* p = positions_xyz + (t * 3 + v) * 3;
      world[v][0] = model.m[0] * p[0] + model.m[4] * p[1] +
                    model.m[8] * p[2] + model.m[12];
      world[v][1] = model.m[1] * p[0] + model.m[5] * p[1] +
                    model.m[9] * p[2] + model.m[13];
      world[v][2] = model.m[2] * p[0] + model.m[6] * p[1] +
                    model.m[10] * p[2] + model.m[14];
    }
    float n[3];
    TriNormal(world[0], world[1], world[2], n);
    const float* base = colors_rgb + t * 9;
    float lit[3];
    ShadeFace(base, n, light_dir, lit);
    for (int v = 0; v < 3; ++v) {
      float* op = out_xyz + (e * 3 + v) * 3;
      op[0] = world[v][0];
      op[1] = world[v][1];
      op[2] = world[v][2];
      float* oc = out_rgba + (e * 3 + v) * 4;
      oc[0] = lit[0];
      oc[1] = lit[1];
      oc[2] = lit[2];
      oc[3] = 1.0f;
    }
  }
}

}  // namespace trial
}  // namespace tgles

#endif  // TGLES_APPS_CUBE_RENDER_H
