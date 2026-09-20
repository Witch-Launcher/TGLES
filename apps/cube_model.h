#ifndef TGLES_APPS_CUBE_MODEL_H
#define TGLES_APPS_CUBE_MODEL_H

// Test 3D model + column-major mat4 helpers for the macOS render trial
// (apps/tgl_cube_trial.mm). Pure header, no TGL deps: unit-tested in
// tests/test_cube_model.cpp. Matrices follow the GL column-major
// convention, matching the bridge MSL float4x4 uniforms byte-for-byte.

#include <cmath>
#include <cstddef>

namespace tgles {
namespace trial {

// Unit cube centered at the origin: 12 triangles, 36 vertices.
// Positions are xyz triples; colors are rgb triples (one flat color per
// outward face: +Z red, -Z green, +X blue, -X yellow, +Y magenta,
// -Y cyan). Winding is CCW seen from outside each face.
struct CubeModel {
  static constexpr std::size_t kVertexCount = 36;
  static const float kPositions[kVertexCount * 3];
  static const float kColors[kVertexCount * 3];
};

inline const float CubeModel::kPositions[CubeModel::kVertexCount * 3] = {
    // +Z front (red).
    -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, -1, 1, 1, 1, 1, -1, 1, 1,
    // -Z back (green).
    1, -1, -1, -1, -1, -1, -1, 1, -1, 1, -1, -1, -1, 1, -1, 1, 1, -1,
    // +X right (blue).
    1, -1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1, 1, 1, 1,
    // -X left (yellow).
    -1, -1, -1, -1, -1, 1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, 1, -1,
    // +Y top (magenta).
    -1, 1, 1, 1, 1, 1, 1, 1, -1, -1, 1, 1, 1, 1, -1, -1, 1, -1,
    // -Y bottom (cyan).
    -1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, 1,
};

inline const float CubeModel::kColors[CubeModel::kVertexCount * 3] = {
    1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0,  // +Z red
    0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0,  // -Z green
    0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,  // +X blue
    1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0,  // -X yellow
    1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1,  // +Y magenta
    0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1,  // -Y cyan
};

struct Mat4 {
  float m[16];
  static Mat4 Identity() {
    Mat4 o = {};
    o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1.0f;
    return o;
  }
  // Column-major multiply: out = a * b.
  static Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 o = {};
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        float s = 0.0f;
        for (int k = 0; k < 4; ++k) s += a.m[r + 4 * k] * b.m[k + 4 * c];
        o.m[r + 4 * c] = s;
      }
    }
    return o;
  }
  static Mat4 Translate(float x, float y, float z) {
    Mat4 o = Identity();
    o.m[12] = x;
    o.m[13] = y;
    o.m[14] = z;
    return o;
  }
  // Right-handed rotation about +Y, angle in radians.
  static Mat4 RotateY(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 o = {};
    o.m[0] = c;
    o.m[2] = -s;
    o.m[5] = 1.0f;
    o.m[8] = s;
    o.m[10] = c;
    o.m[15] = 1.0f;
    return o;
  }
  // GL-style perspective (fovy in radians). Depth range maps near->-1,
  // far->+1 with m[11] = -1.
  static Mat4 Perspective(float fovy_radians, float aspect, float z_near,
                          float z_far) {
    const float f = 1.0f / std::tan(fovy_radians * 0.5f);
    Mat4 o = {};
    o.m[0] = f / aspect;
    o.m[5] = f;
    o.m[10] = (z_far + z_near) / (z_near - z_far);
    o.m[11] = -1.0f;
    o.m[14] = (2.0f * z_far * z_near) / (z_near - z_far);
    return o;
  }
};

}  // namespace trial
}  // namespace tgles

#endif  // TGLES_APPS_CUBE_MODEL_H
