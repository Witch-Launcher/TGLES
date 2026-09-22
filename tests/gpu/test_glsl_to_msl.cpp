// GLSL->MSL translator unit tests (pure C++, no GPU): translation of the
// exact shader pairs the game window / trials / real tests use, plus honest
// failure gates for out-of-subset constructs. Ground truth: GLSL ES 3.20
// spec (docs/reference/GLSL_ES_Specification_3.20.pdf) for what the inputs
// mean; the MSL half is pinned by device tests (test_translated_shader_real).

#include "test_framework.h"

#include <string>

#include "tgles/gpu/glsl_to_msl.h"

namespace {

// The exact pair apps/tgl_game_window.mm + trials drive through the dylib.
const char* kGameVs =
    "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
    "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec2 a_uv;\n"
    "out vec4 v_col; out vec2 v_uv; uniform mat4 u_modelViewProj;\n"
    "void main(){ v_col=a_col; v_uv=a_uv;"
    " gl_Position=u_modelViewProj*a_pos; }";
const char* kGameFs =
    "#version 320 es\nprecision mediump float;\n"
    "in vec4 v_col; in vec2 v_uv; uniform sampler2D u_tex; out vec4 o_col;\n"
    "void main(){ o_col = v_col * texture(u_tex, v_uv); }";

const char* kLightVs =
    "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
    "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec2 a_n;\n"
    "out vec4 v_col; out vec2 v_n; uniform mat4 u_mvp;\n"
    "void main(){ v_col=a_col; v_n=a_n; gl_Position=u_mvp*a_pos; }";
const char* kLightFs =
    "#version 320 es\nprecision mediump float;\n"
    "in vec4 v_col; in vec2 v_n; uniform vec4 u_light; out vec4 o_col;\n"
    "void main(){ float d = max(dot(normalize(vec3(v_n, 1.0)), "
    "normalize(u_light.xyz)), 0.0);"
    " o_col = vec4(v_col.rgb * (0.25 + 0.75 * d), 1.0); }";

}  // namespace

TEST(GlslToMsl, GamePairTranslatesWithSampler) {
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(kGameVs, kGameFs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.uses_sampler);
  EXPECT_TRUE(t.needs_slot2);
  EXPECT_TRUE(t.mvp_name == "u_modelViewProj");
  EXPECT_EQ(t.sampler_names.size(), 1u);
  EXPECT_TRUE(t.sampler_names[0] == "u_tex");
  // MSL shape: struct varyings, sample() call, texture/sampler params.
  EXPECT_TRUE(t.library_source.find("vs_main") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("fs_main") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("[[attribute(2)]]") != std::string::npos);
  EXPECT_TRUE(t.library_source.find(".sample(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("[[texture(0)]]") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("out.position") != std::string::npos);
  // GL->Metal depth remap is injected (negative-z must survive to the depth
  // test, like the fixed pair and ANGLE do).
  EXPECT_TRUE(t.library_source.find("out.position.z = out.position.z * 0.5") !=
              std::string::npos);
  // No GLSL-isms leak into MSL (would fail Metal compilation). Note:
  // `[[texture(0)]]` attributes legitimately contain "texture(", so gate on
  // the GLSL call shape `texture(u_tex` instead.
  EXPECT_TRUE(t.library_source.find("gl_Position") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("texture(u_tex") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("mediump") == std::string::npos);
}

TEST(GlslToMsl, LightingPairTranslatesWithoutSampler) {
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(kLightVs, kLightFs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(!t.uses_sampler);
  EXPECT_TRUE(t.needs_slot2);  // Normal rides slot 2 like uv does.
  EXPECT_TRUE(t.mvp_name == "u_mvp");
  EXPECT_TRUE(t.library_source.find("normalize(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("dot(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("[[texture(0)]]") == std::string::npos);
  // MSL double trap: unsuffixed `0.0` would break max(float, double)
  // overload resolution at Metal compile time, so literals gain f.
  EXPECT_TRUE(t.library_source.find("0.0f") != std::string::npos);
  // MSL constructor trap: vec3/vec4 are undeclared identifiers in MSL.
  EXPECT_TRUE(t.library_source.find("float3(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("vec3(") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("vec4(") == std::string::npos);
}

TEST(GlslToMsl, TrivialMainsFailWithLegacyFallbackAvailable) {
  // Trial convention (empty mains): NOT translatable (no gl_Position), but
  // the facade must keep them on the legacy passthrough instead of failing.
  const char* vs =
      "#version 320 es\nuniform mat4 u_modelViewProj;\nvoid main() {}";
  const char* fs = "#version 320 es\nvoid main() {}";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(!t.ok);
  EXPECT_TRUE(!t.error.empty());
  EXPECT_TRUE(tgles::glsl::LooksLegacyTrivial(vs, fs));
  EXPECT_TRUE(!tgles::glsl::LooksLegacyTrivial(kGameVs, kGameFs));
}

TEST(GlslToMsl, OutOfSubsetFailsClosedWithReasons) {
  // sampler3D with a 2D-style vec2 coord is ill-formed usage; a samplerCube
  // that is never sampled is a dangling binding.
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(
      kGameVs,
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv; uniform samplerCube u_t;\n"
      "out vec4 o;\nvoid main(){ o = v_col * vec4(v_uv, 0.0, 1.0); }");
  EXPECT_TRUE(!t.ok);
  EXPECT_TRUE(t.error.find("never sampled") != std::string::npos);
  // Uniform arrays are SUPPORTED (Phase 4 item 7): per-element packing +
  // GetUniformElementFloats. Pixel proof lives in Phase4.UniformArrays*.
  t = tgles::glsl::TranslateProgram(
      "#version 320 es\nlayout(location=0) in vec4 a;\n"
      "layout(location=1) in vec4 b;\nuniform vec4 u_l[2];\n"
      "void main(){ gl_Position = a + u_l[0] + u_l[1]; }",
      "#version 320 es\nprecision mediump float;\n"
      "out vec4 o;\nvoid main(){ o = vec4(1.0); }");
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.uniforms.size(), 1u);
  EXPECT_TRUE(t.uniforms[0].array_size == 2);
  // Attrib at location 5.
  t = tgles::glsl::TranslateProgram(
      "#version 320 es\nlayout(location=5) in vec4 a;\n"
      "void main(){ gl_Position = a; }",
      "#version 320 es\nprecision mediump float;\n"
      "out vec4 o;\nvoid main(){ o = vec4(1.0); }");
  EXPECT_TRUE(!t.ok);
  // Fragment early return.
  t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "out vec4 o;\nvoid main(){ if (true) { return; } o = vec4(1.0); }");
  EXPECT_TRUE(!t.ok);
  // Duplicate fragment out location.
  t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "layout(location=0) out vec4 o0; layout(location=0) out vec4 o1;\n"
      "void main(){ o0 = vec4(1.0); o1 = vec4(0.0); }");
  EXPECT_TRUE(!t.ok);
  // Bitwise ops with integer attribs (float-converted stores would change
  // meaning).
  t = tgles::glsl::TranslateProgram(
      "#version 320 es\nlayout(location=0) in ivec4 a;\n"
      "layout(location=1) in vec4 b;\n"
      "out vec4 v;\nvoid main(){ v = vec4(a & 3); gl_Position = b; }",
      "#version 320 es\nprecision mediump float;\nin vec4 v;\n"
      "out vec4 o;\nvoid main(){ o = v; }");
  EXPECT_TRUE(!t.ok);
  EXPECT_TRUE(t.error.find("Bitwise") != std::string::npos ||
              t.error.find("bitwise") != std::string::npos);
}

TEST(GlslToMsl, V2UniformTypesPackOn16ByteStarts) {
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(
      "#version 320 es\nlayout(location=0) in vec4 a;\n"
      "layout(location=1) in vec4 b;\nuniform mat4 u_m;\nuniform vec3 u_v3;\n"
      "uniform float u_f;\n"
      "void main(){ gl_Position = u_m * a + vec4(u_v3, u_f); }",
      "#version 320 es\nprecision mediump float;\n"
      "out vec4 o;\nvoid main(){ o = vec4(1.0); }");
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.uniforms.size(), 3u);
  EXPECT_TRUE(t.uniforms[0].name == "u_m");
  EXPECT_EQ(t.uniforms[0].offset, 0);
  EXPECT_TRUE(t.uniforms[1].name == "u_v3");
  EXPECT_EQ(t.uniforms[1].offset, 64);
  EXPECT_TRUE(t.uniforms[2].name == "u_f");
  EXPECT_EQ(t.uniforms[2].offset, 80);
  EXPECT_EQ(t.uniform_block_bytes, 96);
}

TEST(GlslToMsl, V2MrtDualAndCubeShapes) {
  // MRT pair: locations 0+1, contiguous.
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "layout(location=0) out vec4 o0; layout(location=1) out vec4 o1;\n"
      "void main(){ o0 = vec4(1.0); o1 = vec4(0.0, 1.0, 0.0, 1.0); }");
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.is_mrt);
  EXPECT_EQ(t.mrt_count, 2);
  EXPECT_TRUE(!t.is_dual_source);
  EXPECT_TRUE(t.library_source.find("[[color(1)]]") != std::string::npos);
  // Dual-source pair: location 0, indices 0+1.
  t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "layout(location=0) out vec4 o0;\n"
      "layout(location=0, index=1) out vec4 o1;\n"
      "void main(){ o0 = vec4(1.0, 0.0, 0.0, 1.0);"
      " o1 = vec4(0.0, 1.0, 0.0, 1.0); }");
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.is_dual_source);
  EXPECT_TRUE(!t.is_mrt);
  EXPECT_TRUE(t.library_source.find("index(1)") != std::string::npos);
  // Cube sampler pair.
  t = tgles::glsl::TranslateProgram(
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "layout(location=2) in vec3 a_dir;\n"
      "out vec4 v_col; out vec3 v_dir; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; v_dir = a_dir;"
      " gl_Position = u_mvp * a_pos; }",
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec3 v_dir; uniform samplerCube u_cube;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_cube, v_dir); }");
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.uses_sampler);
  EXPECT_EQ(t.sampler_kinds.size(), 1u);
  EXPECT_EQ(t.sampler_kinds[0], 1);
  EXPECT_TRUE(t.library_source.find("texturecube<float>") !=
              std::string::npos);
  // UBO pair (varyings still consumed, as the facade requires).
  t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_n;\n"
      "layout(std140) uniform Frame { mat4 u_vp; vec4 u_tint; };\n"
      "out vec4 o;\nvoid main(){ o = Frame.u_tint * v_col + vec4(v_n, 0.0, "
      "1.0); }");
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.ubo_blocks.size(), 1u);
  EXPECT_TRUE(t.ubo_blocks[0].name == "Frame");
  EXPECT_TRUE(t.library_source.find("[[buffer(2)]]") != std::string::npos);
  // ivec attrib converts (no bitwise ops here).
  t = tgles::glsl::TranslateProgram(
      "#version 320 es\nlayout(location=0) in ivec4 a;\n"
      "layout(location=1) in vec4 b;\n"
      "out vec4 v;\nvoid main(){ v = vec4(a) / 255.0; gl_Position = b; }",
      "#version 320 es\nprecision mediump float;\nin vec4 v;\n"
      "out vec4 o;\nvoid main(){ o = v; }");
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("float4 a [[attribute(0)]]") !=
              std::string::npos);
}

TEST(GlslToMsl, UniformPackingOrderIsDocumented) {
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(kLightVs, kLightFs);
  EXPECT_TRUE(t.ok);
  // vs-first, declaration order: u_mvp (mat4) then u_light (vec4).
  EXPECT_EQ(t.uniforms.size(), 2u);
  EXPECT_TRUE(t.uniforms[0].name == "u_mvp");
  EXPECT_EQ(t.uniforms[0].float_count, 16);
  EXPECT_TRUE(t.uniforms[1].name == "u_light");
  EXPECT_EQ(t.uniforms[1].float_count, 4);
}
