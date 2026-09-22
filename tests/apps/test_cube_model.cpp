// CPU tests for the trial 3D model + mat4 math (no GPU needed).
// All expectations are closed-form math facts, not guesses:
// - 36 vertices / 12 triangles, corners in [-1, 1], flat face colors.
// - RotateY(0) == Identity; RotateY(90deg) maps +X to -Z.
// - Multiply is associative-safe on the checked case: R(90)*R(90) == R(180).
// - Perspective(90deg, 1, 0.1, 100) has exact closed-form entries.

#include "test_framework.h"

#include <cmath>

#include "cube_model.h"

namespace {

bool Near(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

TEST(CubeModel, VertexCountAndBounds) {
  using tgles::trial::CubeModel;
  EXPECT_EQ(CubeModel::kVertexCount, 36u);
  for (std::size_t v = 0; v < CubeModel::kVertexCount; ++v) {
    for (int c = 0; c < 3; ++c) {
      const float p = CubeModel::kPositions[v * 3 + c];
      EXPECT_TRUE(p == -1.0f || p == 1.0f);
      const float col = CubeModel::kColors[v * 3 + c];
      EXPECT_TRUE(col == 0.0f || col == 1.0f);
    }
  }
}

TEST(CubeModel, FacesHaveFlatColors) {
  using tgles::trial::CubeModel;
  // Each 6-vertex face shares one color; all 6 face colors differ.
  float faces[6][3];
  for (int f = 0; f < 6; ++f) {
    for (int c = 0; c < 3; ++c) faces[f][c] = CubeModel::kColors[f * 18 + c];
    for (int v = 1; v < 6; ++v) {
      for (int c = 0; c < 3; ++c) {
        EXPECT_TRUE(CubeModel::kColors[f * 18 + v * 3 + c] == faces[f][c]);
      }
    }
  }
  static const float kWant[6][3] = {
      {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1},
  };
  for (int f = 0; f < 6; ++f) {
    for (int c = 0; c < 3; ++c) EXPECT_TRUE(faces[f][c] == kWant[f][c]);
  }
}

TEST(CubeModel, RotateYIdentityAndNinety) {
  using tgles::trial::Mat4;
  const Mat4 id = Mat4::Identity();
  const Mat4 r0 = Mat4::RotateY(0.0f);
  for (int i = 0; i < 16; ++i) EXPECT_TRUE(Near(r0.m[i], id.m[i]));
  // +X axis maps to -Z under +90 degrees about +Y (right-handed).
  const float kHalfPi = 1.5707963267948966f;
  const Mat4 r90 = Mat4::RotateY(kHalfPi);
  EXPECT_TRUE(Near(r90.m[0], 0.0f));
  EXPECT_TRUE(Near(r90.m[2], -1.0f));
  EXPECT_TRUE(Near(r90.m[8], 1.0f));
  EXPECT_TRUE(Near(r90.m[10], 0.0f));
  EXPECT_TRUE(Near(r90.m[5], 1.0f));
  EXPECT_TRUE(Near(r90.m[15], 1.0f));
}

TEST(CubeModel, MultiplyNinetyTwiceIsOneEighty) {
  using tgles::trial::Mat4;
  const float kHalfPi = 1.5707963267948966f;
  const Mat4 r90 = Mat4::RotateY(kHalfPi);
  const Mat4 r180 = Mat4::Multiply(r90, r90);
  EXPECT_TRUE(Near(r180.m[0], -1.0f));
  EXPECT_TRUE(Near(r180.m[2], 0.0f));
  EXPECT_TRUE(Near(r180.m[8], 0.0f));
  EXPECT_TRUE(Near(r180.m[10], -1.0f));
  EXPECT_TRUE(Near(r180.m[5], 1.0f));
  EXPECT_TRUE(Near(r180.m[15], 1.0f));
}

TEST(CubeModel, PerspectiveClosedForm) {
  using tgles::trial::Mat4;
  const float kHalfPi = 1.5707963267948966f;
  const Mat4 p = Mat4::Perspective(kHalfPi, 1.0f, 0.1f, 100.0f);
  EXPECT_TRUE(Near(p.m[0], 1.0f));
  EXPECT_TRUE(Near(p.m[5], 1.0f));
  EXPECT_TRUE(Near(p.m[10], (100.1f) / (-99.9f)));
  EXPECT_TRUE(Near(p.m[11], -1.0f));
  EXPECT_TRUE(Near(p.m[14], 20.0f / (-99.9f)));
  EXPECT_TRUE(Near(p.m[15], 0.0f));
}

TEST(CubeModel, MvpKeepsCubeInFront) {
  using tgles::trial::Mat4;
  // MVP = P * Translate(0,0,-5) * RotateY(0): cube center maps to NDC z
  // inside (-1, 1) and x/y to 0.
  const float kHalfPi = 1.5707963267948966f;
  const Mat4 mvp = Mat4::Multiply(
      Mat4::Perspective(kHalfPi, 1.0f, 0.1f, 100.0f),
      Mat4::Multiply(Mat4::Translate(0, 0, -5), Mat4::RotateY(0)));
  float x = mvp.m[12], y = mvp.m[13], z = mvp.m[14], w = mvp.m[15];
  // Column-major translation column is m[12..15] of P*T.
  EXPECT_TRUE(Near(x, 0.0f));
  EXPECT_TRUE(Near(y, 0.0f));
  EXPECT_TRUE(w > 0.0f);
  const float ndc_z = z / w;
  EXPECT_TRUE(ndc_z > -1.0f && ndc_z < 1.0f);
}
