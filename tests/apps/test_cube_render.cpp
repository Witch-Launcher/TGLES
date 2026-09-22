// CPU frame builder tests (no GPU): per-triangle world normals,
// directional diffuse lighting, back-to-front depth sort, interleaved
// stream emission. Closed-form facts, shared by the trial CLI and the
// window app through apps/cube_render.h.

#include "test_framework.h"

#include <cmath>

#include "cube_model.h"
#include "cube_render.h"

namespace {

bool Near(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

TEST(CubeRender, TriNormalFrontFace) {
  using tgles::trial::TriNormal;
  const float a[3] = {-1, -1, 0}, b[3] = {1, -1, 0}, c[3] = {0, 1, 0};
  float n[3] = {0, 0, 0};
  TriNormal(a, b, c, n);
  EXPECT_TRUE(Near(n[0], 0.0f));
  EXPECT_TRUE(Near(n[1], 0.0f));
  EXPECT_TRUE(Near(n[2], 1.0f));
}

TEST(CubeRender, DiffuseLightingClosedForm) {
  using tgles::trial::ShadeFace;
  const float base[3] = {1.0f, 0.5f, 0.25f};
  const float facing[3] = {0, 0, 1};
  const float away[3] = {0, 0, -1};
  float out[3] = {0, 0, 0};
  // Facing the light: full brightness.
  ShadeFace(base, facing, facing, out);
  EXPECT_TRUE(Near(out[0], 1.0f) && Near(out[1], 0.5f) &&
              Near(out[2], 0.25f));
  // Facing away: ambient 0.25 only.
  ShadeFace(base, away, facing, out);
  EXPECT_TRUE(Near(out[0], 0.25f) && Near(out[1], 0.125f) &&
              Near(out[2], 0.0625f));
  // 45 degrees: 0.25 + 0.75 * cos(45).
  const float k = 0.70710678f;
  const float tilted[3] = {0, k, k};
  ShadeFace(base, tilted, facing, out);
  const float f = 0.25f + 0.75f * k;
  EXPECT_TRUE(Near(out[0], f) && Near(out[1], 0.5f * f) &&
              Near(out[2], 0.25f * f));
}

TEST(CubeRender, IdentitySortPutsBackFaceFirst) {
  using tgles::trial::CubeModel;
  using tgles::trial::Mat4;
  using tgles::trial::SortedTriOrder;
  // Identity model: -Z face (z=-1) farthest with camera at +Z.
  // Stable ascending order by average triangle z. The side/top/bottom tris
  // do NOT tie at z=0: each spans the full depth, so their averages are
  // -1/3 (tris 4,7,9,10) or +1/3 (tris 5,6,8,11). Under identity the keys
  // are sums of -1/0/1, so this exact order is bit-deterministic.
  unsigned order[12];
  for (unsigned i = 0; i < 12; ++i) order[i] = i;  // Contract input.
  SortedTriOrder(CubeModel::kPositions, 12, Mat4::Identity(), order);
  const unsigned kWant[12] = {2, 3, 4, 7, 9, 10, 5, 6, 8, 11, 0, 1};
  for (int i = 0; i < 12; ++i) EXPECT_TRUE(order[i] == kWant[i]);
}

TEST(CubeRender, RotatedSortPutsSideFaceFirst) {
  using tgles::trial::CubeModel;
  using tgles::trial::Mat4;
  using tgles::trial::SortedTriOrder;
  // RotateY(90 deg): the +X face lands at z=-1 (farthest), -X at z=+1.
  // Trig floats are inexact, so assert robust properties instead of exact
  // indices: far set {4,5} first, near set {6,7} last, and the per-tri
  // average-z keys are non-decreasing along the returned order.
  const float kHalfPi = 3.141592653589793f * 0.5f;
  const Mat4 rot = Mat4::RotateY(kHalfPi);
  unsigned order[12];
  for (unsigned i = 0; i < 12; ++i) order[i] = i;
  SortedTriOrder(CubeModel::kPositions, 12, rot, order);
  auto avg_z = [&](unsigned tri) {
    float z = 0.0f;
    for (int v = 0; v < 3; ++v) {
      const float* p = CubeModel::kPositions + (tri * 3u + v) * 3;
      z += rot.m[2] * p[0] + rot.m[6] * p[1] + rot.m[10] * p[2] + rot.m[14];
    }
    return z / 3.0f;
  };
  // Far set first (within-set order is float noise, accept either).
  EXPECT_TRUE((order[0] == 4 && order[1] == 5) ||
              (order[0] == 5 && order[1] == 4));
  // Near set last.
  EXPECT_TRUE((order[10] == 6 && order[11] == 7) ||
              (order[10] == 7 && order[11] == 6));
  // Global painter property: keys never decrease along the order.
  for (int i = 1; i < 12; ++i) {
    EXPECT_TRUE(avg_z(order[i - 1]) <= avg_z(order[i]) + 1e-6f);
  }
  // All 12 tris present exactly once.
  bool seen[12] = {};
  for (int i = 0; i < 12; ++i) seen[order[i]] = true;
  for (int i = 0; i < 12; ++i) EXPECT_TRUE(seen[i]);
}

TEST(CubeRender, PurePrimariesKeepHueUnderLight) {
  using tgles::trial::ShadeFace;
  using tgles::trial::kRenderAmbient;
  // The viewer paints every block with pure R/G/B bases. Diffuse lighting
  // scales all channels by one scalar, so the zero channels must stay
  // exactly zero (hue preserved) and the hot channel must equal the
  // ambient+diffuse factor.
  const float kPrimaries[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const float kNormals[3][3] = {{0, 0, 1},
                                {0, 0, -1},
                                {0, 0.70710678f, 0.70710678f}};
  float light[3] = {0.45f, 0.75f, 0.55f};
  const float len =
      std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
  light[0] /= len;
  light[1] /= len;
  light[2] /= len;
  for (int p = 0; p < 3; ++p) {
    for (int n = 0; n < 3; ++n) {
      float out[3] = {0, 0, 0};
      ShadeFace(kPrimaries[p], kNormals[n], light, out);
      const float d = kNormals[n][0] * light[0] +
                      kNormals[n][1] * light[1] + kNormals[n][2] * light[2];
      const float f =
          kRenderAmbient + (1.0f - kRenderAmbient) * (d > 0.0f ? d : 0.0f);
      for (int c = 0; c < 3; ++c) {
        if (kPrimaries[p][c] == 0.0f) {
          EXPECT_TRUE(out[c] == 0.0f);
        } else {
          EXPECT_TRUE(Near(out[c], f));
        }
      }
    }
  }
}

TEST(CubeRender, FrameStreamsAreLitAndSorted) { using tgles::trial::CubeModel;
  using tgles::trial::Mat4;
  using tgles::trial::BuildFrameStreams;
  float pos[36 * 3];
  float col[36 * 4];
  // Light from the front-top-right: +Z face fully lit is impossible with a
  // tilted light; use straight +Z light so face 0 stays pure red.
  const float light[3] = {0, 0, 1};
  BuildFrameStreams(CubeModel::kPositions, CubeModel::kColors, 36,
                    Mat4::Identity(), light, pos, col);
  // First emitted tri is -Z (green, away from light -> ambient only).
  EXPECT_TRUE(Near(col[0], 0.0f) && Near(col[1], 0.25f) &&
              Near(col[2], 0.0f) && Near(col[3], 1.0f));
  // Last emitted tri is +Z (red, fully lit).
  EXPECT_TRUE(Near(col[35 * 4 + 0], 1.0f) &&
              Near(col[35 * 4 + 1], 0.0f) &&
              Near(col[35 * 4 + 2], 0.0f) &&
              Near(col[35 * 4 + 3], 1.0f));
  // Positions are rotated-identity: first vert is tri-2 vert 0 (1,-1,-1).
  EXPECT_TRUE(Near(pos[0], 1.0f) && Near(pos[1], -1.0f) &&
              Near(pos[2], -1.0f));
}
