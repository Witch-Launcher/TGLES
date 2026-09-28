// GLSL->MSL translator unit tests (pure C++, no GPU): translation of the
// exact shader pairs the game window / trials / real tests use, plus honest
// failure gates for out-of-subset constructs. Ground truth: GLSL ES 3.20
// spec (docs/reference/GLSL_ES_Specification_3.20.pdf) for what the inputs
// mean; the MSL half is pinned by device tests (test_translated_shader_real).

#include "test_framework.h"

#include <map>
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
  // Fragment early return in main.
  t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "out vec4 o;\nvoid main(){ if (true) { return; } o = vec4(1.0); }");
  EXPECT_TRUE(!t.ok);
  // Helper function returns (fog.glsl pattern) must NOT fail closed.
  t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "float fog_val(float d){ if (d <= 0.0) return 0.0; return d; }\n"
      "out vec4 o;\nvoid main(){ o = vec4(fog_val(1.0)); }");
  EXPECT_TRUE(t.ok);
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
  EXPECT_TRUE(t.ubo_blocks[0].block_name == "Frame");
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

TEST(GlslToMsl, DerivativesRewriteToLowercaseMsl) {
  // Device-proven (Intel KBL Metal compiler): MSL spells them `dfdx`/`dfdy`
  // (lowercase); GLSL `dFdx`/`dFdy` passed through verbatim would fail Metal
  // compilation at draw time. `fwidth` matches in both languages (no-op).
  // Coarse variants stay denied (kDeny).
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_n; out vec4 o;\n"
      "void main(){ float g = dFdx(v_n.x) + dFdy(v_n.y) + fwidth(v_col.x);"
      " o = vec4(g); }");
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("dfdx(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("dfdy(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("dFdx(") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("dFdy(") == std::string::npos);
  // Coarse derivatives are still out of subset (fail closed, not rewritten).
  t = tgles::glsl::TranslateProgram(
      kLightVs,
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_n; out vec4 o;\n"
      "void main(){ o = vec4(dFdxCoarse(v_n.x)); }");
  EXPECT_TRUE(!t.ok);
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

TEST(GlslToMsl, PositionOnlyPadsColorAndMapsSemanticLocs) {
  // MC POSITION formats: no Color/UV0 in the program. attrib_locs forces
  // Position→0 so the facade Metal slots stay fixed even when the source
  // has no layout() and declaration order would otherwise put things wrong.
  const char* vs =
      "#version 330 core\nin vec4 Position;\n"
      "void main(){ gl_Position = Position; }";
  const char* fs =
      "#version 330\nout vec4 fragColor;\nvoid main(){ fragColor = "
      "vec4(1.0); }";
  std::map<std::string, int> locs = {{"Position", 0}};
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(vs, fs, &locs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(!t.needs_slot2);
  EXPECT_TRUE(t.library_source.find("float4 Position [[attribute(0)]]") !=
              std::string::npos);
  EXPECT_TRUE(t.library_source.find("_tgl_pad_col [[attribute(1)]]") !=
              std::string::npos);
}

TEST(GlslToMsl, GlVertexIdOnlyNeedsNoAttribs) {
  const char* vs =
      "#version 330 core\nvoid main(){\n"
      "  float x = float(gl_VertexID & 1) * 2.0 - 1.0;\n"
      "  gl_Position = vec4(x, 0.0, 0.0, 1.0);\n}";
  const char* fs =
      "#version 330\nout vec4 fragColor;\nvoid main(){ fragColor = "
      "vec4(1.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.uses_vertex_id);
  EXPECT_TRUE(t.library_source.find("_tgl_pad_pos [[attribute(0)]]") !=
              std::string::npos);
  EXPECT_TRUE(t.library_source.find("_tgl_pad_col [[attribute(1)]]") !=
              std::string::npos);
}

TEST(GlslToMsl, AttribLocsOverrideUv0ToSlot2) {
  // MC BindAttribLocation puts UV0 at GL location 1 on POSITION_TEX; the
  // facade must re-pack into Metal slot 2 via semantic attrib_locs.
  const char* vs =
      "#version 330 core\nin vec4 Position;\nin vec2 UV0;\n"
      "out vec2 v_uv;\nvoid main(){ v_uv = UV0; gl_Position = Position; }";
  const char* fs =
      "#version 330\nprecision mediump float;\nin vec2 v_uv;\n"
      "uniform sampler2D Sampler0;\nout vec4 fragColor;\n"
      "void main(){ fragColor = texture(Sampler0, v_uv); }";
  std::map<std::string, int> locs = {{"Position", 0}, {"UV0", 2}};
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(vs, fs, &locs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.needs_slot2);
  EXPECT_EQ(t.slot2_comps, 2);
  EXPECT_TRUE(t.library_source.find("float2 UV0 [[attribute(2)]]") !=
              std::string::npos);
  EXPECT_TRUE(t.library_source.find("_tgl_pad_col [[attribute(1)]]") !=
              std::string::npos);
}

TEST(GlslToMsl, VertexUboWithVec3ModelOffset) {
  // MC gui/position_tex_color: vertex-stage std140 block with mat4 + vec3.
  const char* vs =
      "#version 330 core\nlayout(location=0) in vec4 Position;\n"
      "layout(location=1) in vec4 Color;\n"
      "layout(std140) uniform DynamicTransforms {\n"
      "  mat4 ModelViewMat;\n"
      "  mat4 ProjMat;\n"
      "  vec3 ModelOffset;\n"
      "};\n"
      "out vec4 v_col;\n"
      "void main(){ v_col = Color;\n"
      "  gl_Position = ProjMat * ModelViewMat * vec4(Position.xyz + "
      "ModelOffset, 1.0);\n}";
  const char* fs =
      "#version 330\nprecision mediump float;\nin vec4 v_col;\n"
      "out vec4 fragColor;\nvoid main(){ fragColor = v_col; }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.ubo_blocks.size(), 1u);
  EXPECT_TRUE(t.ubo_blocks[0].name == "DynamicTransforms");
  EXPECT_TRUE(t.ubo_blocks[0].block_name == "DynamicTransforms");
  EXPECT_EQ(t.ubo_blocks[0].members.size(), 3u);
  EXPECT_TRUE(t.ubo_blocks[0].members[2].msl_type == "float3");
  EXPECT_TRUE(t.library_source.find("DynamicTransforms_t&") !=
              std::string::npos);
  EXPECT_TRUE(t.library_source.find("[[buffer(2)]]") != std::string::npos);
  // Bare member access rewritten to the block param (both stages share the
  // rewrite; vertex body must not keep a bare `ProjMat *`).
  EXPECT_TRUE(t.library_source.find("ProjMat *") == std::string::npos ||
              t.library_source.find("DynamicTransforms.ProjMat") !=
                  std::string::npos);
  EXPECT_TRUE(t.library_source.find("DynamicTransforms.ModelOffset") !=
              std::string::npos);
}

TEST(GlslToMsl, MojImportAndPreprocessorLinesAreStripped) {
  // MC expands #moj_import before glShaderSource; if any line survives it
  // must not reach the decl parsers.
  const char* vs =
      "#version 330 core\n#moj_import <light.glsl>\n"
      "#define FOO 1\nlayout(location=0) in vec4 a_pos;\n"
      "void main(){ gl_Position = a_pos; }";
  const char* fs =
      "#version 330\n#moj_import <fog.glsl>\nout vec4 fragColor;\n"
      "void main(){ fragColor = vec4(1.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("moj_import") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("#define") == std::string::npos);
}

TEST(GlslToMsl, SharedUboInBothStagesDeduped) {
  const char* vs =
      "#version 330 core\nlayout(location=0) in vec4 a_pos;\n"
      "layout(std140) uniform Frame { mat4 u_vp; };\n"
      "void main(){ gl_Position = u_vp * a_pos; }";
  const char* fs =
      "#version 330\nprecision mediump float;\n"
      "layout(std140) uniform Frame { mat4 u_vp; };\n"
      "out vec4 o;\nvoid main(){ o = vec4(u_vp[0][0]); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.ubo_blocks.size(), 1u);
  EXPECT_TRUE(t.library_source.find("Frame_t& Frame") != std::string::npos);
}

TEST(GlslToMsl, UboInstanceNameKeepsBlockNameForReflection) {
  // `uniform Block {..} inst;` — MSL param is the instance; reflection must
  // keep the block name so GetUniformBlockIndex(inst) and (Block) both work.
  const char* vs =
      "#version 330 core\nlayout(location=0) in vec4 a_pos;\n"
      "layout(std140) uniform Camera { mat4 u_vp; } cam;\n"
      "void main(){ gl_Position = cam.u_vp * a_pos; }";
  const char* fs =
      "#version 330\nprecision mediump float;\n"
      "layout(std140) uniform Camera { mat4 u_vp; } cam;\n"
      "out vec4 o;\nvoid main(){ o = vec4(cam.u_vp[0][0]); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.ubo_blocks.size(), 1u);
  EXPECT_TRUE(t.ubo_blocks[0].name == "cam");
  EXPECT_TRUE(t.ubo_blocks[0].block_name == "Camera");
  EXPECT_TRUE(t.library_source.find("cam_t& cam") != std::string::npos);
}

TEST(GlslToMsl, UboIntVec3MemberTranslates) {
  // MC terrain shaders put ivec3 CameraBlockPos in a std140 block; the
  // whitelist used to reject int3 and fail the whole program translate.
  const char* vs =
      "#version 330 core\nlayout(location=0) in vec4 a_pos;\n"
      "layout(std140) uniform CameraBlock {\n"
      "  mat4 u_view;\n"
      "  ivec3 CameraBlockPos;\n"
      "};\n"
      "void main(){ gl_Position = u_view * a_pos;\n"
      "  gl_Position.xyz += vec3(CameraBlockPos) * 0.0; }";
  const char* fs =
      "#version 330\nprecision mediump float;\n"
      "layout(std140) uniform CameraBlock {\n"
      "  mat4 u_view;\n"
      "  ivec3 CameraBlockPos;\n"
      "};\n"
      "out vec4 o;\nvoid main(){ o = vec4(vec3(CameraBlockPos), 1.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.ubo_blocks.size(), 1u);
  EXPECT_EQ(t.ubo_blocks[0].members.size(), 2u);
  EXPECT_TRUE(t.ubo_blocks[0].members[1].msl_type == "int3");
  EXPECT_TRUE(t.library_source.find("int3 CameraBlockPos") !=
              std::string::npos);
}

TEST(GlslToMsl, UboUIntVecMembersTranslate) {
  const char* vs =
      "#version 330 core\nlayout(location=0) in vec4 a_pos;\n"
      "layout(std140) uniform Meta { uvec2 range; uvec4 mask; };\n"
      "void main(){ gl_Position = a_pos; }";
  const char* fs =
      "#version 330\nprecision mediump float;\n"
      "layout(std140) uniform Meta { uvec2 range; uvec4 mask; };\n"
      "out vec4 o;\nvoid main(){ o = vec4(mask); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_EQ(t.ubo_blocks.size(), 1u);
  EXPECT_TRUE(t.ubo_blocks[0].members[0].msl_type == "uint2");
  EXPECT_TRUE(t.ubo_blocks[0].members[1].msl_type == "uint4");
  EXPECT_TRUE(t.library_source.find("uint2 range") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("uint4 mask") != std::string::npos);
}

TEST(GlslToMsl, TextureLodRewritesToMetalLevel) {
  // MC core shaders use textureLod for explicit mip selection (terrain
  // blur / GUI). Denylist used to reject it; now maps to MSL level().
  const char* fs =
      "#version 330\nprecision mediump float;\nin vec2 v_uv;\n"
      "uniform sampler2D Sampler0;\nout vec4 fragColor;\n"
      "void main(){ fragColor = textureLod(Sampler0, v_uv, 2.0); }";
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(kGameVs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.uses_sampler);
  EXPECT_TRUE(t.library_source.find("level(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("textureLod") == std::string::npos);
}

TEST(GlslToMsl, GlobalConstArrayOutsideMainEmitted) {
  // MC animate_sprite.vsh: `const vec2 positions[]` lives outside main;
  // MainBody used to drop it so MSL referenced an undeclared `_60`/positions.
  const char* vs =
      "#version 330\n"
      "layout(std140) uniform SpriteAnimationInfo {\n"
      "  mat4 ProjectionMatrix;\n  mat4 SpriteMatrix;\n"
      "  float UPadding;\n  float VPadding;\n  int MipMapLevel;\n};\n"
      "out vec2 texCoord0;\n"
      "const vec2 positions[] = vec2[](\n"
      "    vec2(0, 0), vec2(1, 0), vec2(0, 1),\n"
      "    vec2(0, 1), vec2(1, 0), vec2(1, 1)\n"
      ");\n"
      "void main() {\n"
      "  int index = gl_VertexID & 7;\n"
      "  gl_Position = ProjectionMatrix * SpriteMatrix *"
      " vec4(positions[index], 0, 1);\n"
      "  texCoord0 = positions[index];\n"
      "}";
  const char* fs =
      "#version 330\nprecision mediump float;\nin vec2 texCoord0;\n"
      "uniform sampler2D Sprite;\nout vec4 fragColor;\n"
      "void main(){ fragColor = textureLod(Sprite, texCoord0, 0.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("positions") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("float2 positions") != std::string::npos);
  // GLSL/SPIRV-Cross array ctor must become an MSL brace initializer.
  EXPECT_TRUE(t.library_source.find("float2[](") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("vec2[](") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("SpriteAnimationInfo") != std::string::npos ||
              t.library_source.find("ProjectionMatrix") != std::string::npos);
}

TEST(GlslToMsl, SpiRVCrossRenamedGlobalConstArrayEmitted) {
  // Same shape after SPIRV-Cross: anonymous UBO instance `_30`, array `_60`.
  const char* vs =
      "#version 300 es\n"
      "layout(std140) uniform _30 {\n"
      "  mat4 ProjectionMatrix;\n  mat4 SpriteMatrix;\n"
      "  float UPadding;\n  float VPadding;\n};\n"
      "out vec2 texCoord0;\n"
      "const vec2 _60[] = vec2[](\n"
      "  vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),\n"
      "  vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)\n"
      ");\n"
      "void main(){\n"
      "  int index = int(gl_VertexID) & 7;\n"
      "  gl_Position = (ProjectionMatrix * SpriteMatrix) *"
      " vec4(_60[index], 0.0, 1.0);\n"
      "  vec2 uv = _60[index];\n"
      "  texCoord0 = uv;\n"
      "}";
  const char* fs =
      "#version 300 es\nprecision mediump float;\nin vec2 texCoord0;\n"
      "out vec4 fragColor;\nvoid main(){ fragColor = vec4(texCoord0, 0.0, 1.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("_60") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("float2 _60") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("float2[](") == std::string::npos);
}

TEST(GlslToMsl, GlobalFogHelpersEmittedOutsideMain) {
  // fog.glsl helpers live outside main; dropping them left
  // fog_spherical_distance undeclared in MSL for position/terrain draws.
  const char* vs =
      "#version 330\nlayout(location=0) in vec3 Position;\n"
      "layout(std140) uniform DynamicTransforms {\n"
      "  mat4 ModelViewMat;\n  mat4 ProjMat_unused;\n};\n"
      "out float sphericalVertexDistance;\n"
      "float fog_spherical_distance(vec3 pos) { return length(pos); }\n"
      "void main(){ gl_Position = vec4(Position, 1.0);\n"
      "  sphericalVertexDistance = fog_spherical_distance(Position); }";
  const char* fs =
      "#version 330\nprecision mediump float;\nin float sphericalVertexDistance;\n"
      "out vec4 o;\nvoid main(){ o = vec4(sphericalVertexDistance); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("fog_spherical_distance") !=
              std::string::npos);
  EXPECT_TRUE(t.library_source.find("float fog_spherical_distance") !=
              std::string::npos);
}

TEST(GlslToMsl, TextureBiasRewritesToMetalBias) {
  // texture(s, P, bias) is legal ES 3.20 fragment sampling; maps to MSL bias().
  const char* fs =
      "#version 330\nprecision mediump float;\nin vec2 v_uv;\n"
      "uniform sampler2D Sampler0;\nout vec4 fragColor;\n"
      "void main(){ fragColor = texture(Sampler0, v_uv, 1.5); }";
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(kGameVs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("bias(") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("texture(Sampler0") == std::string::npos);
}

TEST(GlslToMsl, BoolMixFromSpirvCrossClampBecomesSelect) {
  // SPIRV-Cross lowers clamp() to mix(x, y, isnan(...)). Metal mix needs a
  // float weight; bool third arg must become select(a, b, cond). Nested form
  // (outer + inner both use isnan weight) must rewrite BOTH levels.
  // Float-weight mix (component of a varying) must stay mix.
  const char* fs =
      "#version 330\nprecision mediump float;\nin vec2 v_uv;\n"
      "out vec4 o;\n"
      "void main(){\n"
      "  float2 x = v_uv;\n"
      "  float2 lo = float2(0.03125);\n"
      "  float2 c = mix(mix(max(x, lo), lo, isnan(x)), x, isnan(lo));\n"
      "  o = mix(vec4(c, 0.0), vec4(1.0), v_uv.x);\n"
      "}";
  tgles::glsl::TranslatedProgram t =
      tgles::glsl::TranslateProgram(kGameVs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.library_source.find("select(") != std::string::npos);
  // Both isnan-weighted levels must be select; no mix(..., isnan) remains.
  EXPECT_TRUE(t.library_source.find("mix(mix(max") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("select(select(max") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("select(mix(") == std::string::npos);
  // Float-weight mix must stay mix (constructors already rewritten to float4).
  EXPECT_TRUE(t.library_source.find("mix(float4(") != std::string::npos);
}

TEST(GlslToMsl, Uv2LocationThreeTranslatesWithSlot3) {
  // MC terrain/block vertex format: Position, Color, UV0, UV2 (ivec2 lightmap)
  // at location 3. Must accept loc 3, decode as float2, set needs_slot3.
  const char* vs =
      "#version 320 es\n"
      "layout(location=0) in vec3 Position;\n"
      "layout(location=1) in vec4 Color;\n"
      "layout(location=2) in vec2 UV0;\n"
      "layout(location=3) in ivec2 UV2;\n"
      "out vec4 v_col; out vec2 v_uv0;\n"
      "void main(){ v_col=Color; v_uv0=UV0; gl_Position=vec4(Position,1.0); }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv0; out vec4 o;\n"
      "void main(){ o = v_col * vec4(v_uv0, 0.0, 1.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.needs_slot3);
  EXPECT_EQ(t.slot3_comps, 2);
  EXPECT_TRUE(t.library_source.find("[[attribute(3)]]") != std::string::npos);
}

TEST(GlslToMsl, LocationFourStillFailsClosed) {
  // Entity UV1/Normal (loc 4,5) remain out of subset this round.
  const char* vs =
      "#version 320 es\n"
      "layout(location=0) in vec4 Position;\n"
      "layout(location=4) in vec2 UV1;\n"
      "void main(){ gl_Position = Position; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\nout vec4 o;\n"
      "void main(){ o = vec4(1.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(!t.ok);
  EXPECT_TRUE(t.error.find("0..3") != std::string::npos);
}

TEST(GlslToMsl, VertexTextureFetchSampleLightmapTranslates) {
  // terrain.vsh: free helper sample_lightmap(sampler2D, ivec2) called from
  // VS with uniform Sampler2 + attr UV2. Must rewrite helper + call site and
  // emit VS texture params (not FS).
  const char* vs =
      "#version 320 es\n"
      "layout(location=0) in vec3 Position;\n"
      "layout(location=1) in vec4 Color;\n"
      "layout(location=2) in vec2 UV0;\n"
      "layout(location=3) in ivec2 UV2;\n"
      "uniform sampler2D Sampler2;\n"
      "out vec4 v_col; out vec2 v_uv0;\n"
      "vec4 sample_lightmap(sampler2D lightMap, ivec2 uv) {\n"
      "  return texture(lightMap, clamp((uv / 256.0) + 0.5 / 16.0,\n"
      "    vec2(0.5 / 16.0), vec2(15.5 / 16.0)));\n"
      "}\n"
      "void main(){\n"
      "  v_col = Color * sample_lightmap(Sampler2, UV2);\n"
      "  v_uv0 = UV0;\n"
      "  gl_Position = vec4(Position, 1.0);\n"
      "}";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv0; uniform sampler2D Sampler0;\n"
      "out vec4 o;\n"
      "void main(){ o = v_col * texture(Sampler0, v_uv0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.uses_vertex_sampler);
  EXPECT_TRUE(t.uses_fragment_sampler);
  EXPECT_TRUE(t.uses_sampler);
  EXPECT_EQ(t.sampler_names.size(), 2u);
  // Helper signature became texture2d/sampler pair + float coord.
  EXPECT_TRUE(t.library_source.find("sample_lightmap") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("texture2d<float>") != std::string::npos);
  EXPECT_TRUE(t.library_source.find("sample_lightmap(Sampler2_tex, "
                                    "Sampler2_smp,") != std::string::npos);
  // Regression: the expander used to re-append the bare uniform name after
  // `_smp` (`Sampler2_smpSampler2` — undeclared, MSL compile fails).
  EXPECT_TRUE(t.library_source.find("Sampler2_smpSampler2") ==
              std::string::npos);
  EXPECT_TRUE(t.library_source.find(".sample(") != std::string::npos);
  // VS emits Sampler2 at texture(0); FS emits Sampler0 at texture(0)
  // (independent stage index spaces).
  EXPECT_TRUE(t.library_source.find("vs_main") != std::string::npos);
  const auto vs_at = t.library_source.find("vertex Varyings vs_main");
  const auto fs_at = t.library_source.find("fragment float4 fs_main");
  EXPECT_TRUE(vs_at != std::string::npos);
  EXPECT_TRUE(fs_at != std::string::npos);
  const std::string vs_sig =
      t.library_source.substr(vs_at, fs_at - vs_at);
  EXPECT_TRUE(vs_sig.find("Sampler2_tex [[texture(0)]]") != std::string::npos);
  EXPECT_TRUE(vs_sig.find("Sampler0_tex") == std::string::npos);
}

TEST(GlslToMsl, VertexOnlySamplerNotEmittedInFsMain) {
  // Sampler declared only in VS must not appear as an FS texture param and
  // must not trip "never sampled" on the fragment body.
  const char* vs =
      "#version 320 es\n"
      "layout(location=0) in vec4 a_pos;\n"
      "layout(location=3) in vec2 a_uv2;\n"
      "uniform sampler2D Sampler2;\n"
      "out float v_l;\n"
      "void main(){ v_l = texture(Sampler2, a_uv2).r; gl_Position = a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in float v_l; out vec4 o;\n"
      "void main(){ o = vec4(v_l); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  EXPECT_TRUE(t.uses_vertex_sampler);
  EXPECT_TRUE(!t.uses_fragment_sampler);
  const auto fs_at = t.library_source.find("fragment float4 fs_main");
  EXPECT_TRUE(fs_at != std::string::npos);
  const std::string fs_sig = t.library_source.substr(fs_at);
  EXPECT_TRUE(fs_sig.find("Sampler2_tex") == std::string::npos);
  EXPECT_TRUE(t.library_source.find("Sampler2_tex.sample") != std::string::npos);
}

TEST(GlslToMsl, FreeFnGlobalTextureStillFailsClosed) {
  // Free helper that samples a global (non-param) sampler cannot see stage
  // texture bindings in MSL — fail closed with a clear reason.
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "uniform sampler2D u_s;\n"
      "out vec2 v_uv;\n"
      "vec4 helper(vec2 p) { return texture(u_s, p); }\n"
      "void main(){ v_uv = a_pos.xy; gl_Position = a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\nin vec2 v_uv;\n"
      "out vec4 o;\nvoid main(){ o = vec4(v_uv, 0.0, 1.0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(!t.ok);
  EXPECT_TRUE(t.error.find("texture") != std::string::npos ||
              t.error.find("never sampled") != std::string::npos);
}

TEST(GlslToMsl, IntAttrLocalIntoSampleLightmapMatchesDeviceLog) {
  // MC 26.1.2 terrain shader shape from the device log (black-screen bug):
  //   ivec2 param_2 = UV2;
  //   vertexColor = Color * sample_lightmap(Sampler2, param_2);
  // VertexIn stores UV2 as float2, so the local read needs int2(in.UV2),
  // and the helper's coord param widened to float2, so the call needs
  // float2(param_2). The expander also used to emit `Sampler2_smpSampler2`.
  const char* vs =
      "#version 320 es\n"
      "layout(location=0) in vec4 Position;\n"
      "layout(location=1) in vec4 Color;\n"
      "layout(location=2) in vec2 UV0;\n"
      "layout(location=3) in ivec2 UV2;\n"
      "uniform sampler2D Sampler2;\n"
      "out vec4 vertexColor;\n"
      "out vec2 texCoord0;\n"
      "vec4 sample_lightmap(sampler2D Sampler, ivec2 texCoord) {\n"
      "  return texture(Sampler, clamp((texCoord + 0.5) / 256.0,\n"
      "    vec2(0.5 / 16.0), vec2(15.5 / 16.0)));\n"
      "}\n"
      "void main(){\n"
      "  ivec2 param_2 = UV2;\n"
      "  vertexColor = Color * sample_lightmap(Sampler2, param_2);\n"
      "  texCoord0 = UV0;\n"
      "  gl_Position = Position;\n"
      "}";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 vertexColor;\nin vec2 texCoord0;\n"
      "uniform sampler2D Sampler0;\n"
      "out vec4 fragColor;\n"
      "void main(){ fragColor = vertexColor * texture(Sampler0, texCoord0); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  const std::string& msl = t.library_source;
  // Device-log failure signatures must be gone.
  EXPECT_TRUE(msl.find("Sampler2_smpSampler2") == std::string::npos);
  EXPECT_TRUE(msl.find("int2 param_2 = in.UV2") == std::string::npos);
  // Fixed forms.
  EXPECT_TRUE(msl.find("int2 param_2 = int2(in.UV2)") != std::string::npos);
  EXPECT_TRUE(msl.find("sample_lightmap(Sampler2_tex, Sampler2_smp,") !=
              std::string::npos);
  EXPECT_TRUE(msl.find("float2(param_2)") != std::string::npos);
  // The helper's own definition head must not be expanded as a call site
  // (float_cast used to wrap signature params: `float2(sampler lightMap_smp)`).
  EXPECT_TRUE(msl.find("float2(sampler") == std::string::npos);
  EXPECT_TRUE(msl.find("sample_lightmap(texture2d<float> Sampler_tex, sampler "
                       "Sampler_smp,") != std::string::npos);
}

TEST(GlslToMsl, TextureGradFreeFnRewritesToGradient2d) {
  // terrain.fsh sampleNearest: textureGrad on a sampler param must rewrite
  // to MSL gradient2d sampling, not fail closed via kDeny.
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "out vec2 v_uv;\n"
      "void main(){ v_uv = a_pos.xy; gl_Position = a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec2 v_uv; uniform sampler2D Sampler0;\n"
      "out vec4 o;\n"
      "vec4 sampleGrad(sampler2D source, vec2 uv, vec2 du, vec2 dv) {\n"
      "  return textureGrad(source, uv, du, dv);\n"
      "}\n"
      "void main(){\n"
      "  vec2 du = dFdx(v_uv);\n"
      "  vec2 dv = dFdy(v_uv);\n"
      "  o = sampleGrad(Sampler0, v_uv, du, dv);\n"
      "}";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  const std::string& msl = t.library_source;
  EXPECT_TRUE(msl.find("gradient2d(") != std::string::npos);
  EXPECT_TRUE(msl.find("textureGrad") == std::string::npos);
  EXPECT_TRUE(msl.find("sampleGrad(Sampler0_tex, Sampler0_smp,") !=
              std::string::npos);
}

TEST(GlslToMsl, OverloadedFreeFnsBothEmitted) {
  // terrain.fsh declares sampleNearest twice (3-param wrapper + 6-param
  // implementation). Dedupe must key on the signature, not the name.
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "out vec2 v_uv;\n"
      "void main(){ v_uv = a_pos.xy; gl_Position = a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec2 v_uv; uniform sampler2D Sampler0;\n"
      "out vec4 o;\n"
      "vec4 sampleTwice(sampler2D source, vec2 uv, vec2 px, vec2 du, vec2 dv) {\n"
      "  return textureGrad(source, uv, du, dv);\n"
      "}\n"
      "vec4 sampleTwice(sampler2D source, vec2 uv, vec2 px) {\n"
      "  return sampleTwice(source, uv, px, dFdx(uv), dFdy(uv));\n"
      "}\n"
      "void main(){ o = sampleTwice(Sampler0, v_uv, vec2(1.0)); }";
  tgles::glsl::TranslatedProgram t = tgles::glsl::TranslateProgram(vs, fs);
  EXPECT_TRUE(t.ok);
  const std::string& msl = t.library_source;
  // Both overload bodies emitted (6-param impl contains gradient2d; the
  // 3-param wrapper only forwards).
  EXPECT_TRUE(msl.find("gradient2d(") != std::string::npos);
  EXPECT_TRUE(msl.find("sampleTwice(texture2d<float> source_tex, sampler "
                       "source_smp, float2 uv, float2 px)") !=
              std::string::npos);
  // The wrapper's forward passes its own sampler params as tex/smp
  // (rebuilt args may carry original spacing after the join).
  EXPECT_TRUE(msl.find("sampleTwice(source_tex, source_smp,") !=
              std::string::npos);
  EXPECT_TRUE(msl.find("dfdx(uv)") != std::string::npos);
  EXPECT_TRUE(msl.find("dfdy(uv)") != std::string::npos);
  // Main call site expands the stage uniform.
  EXPECT_TRUE(msl.find("sampleTwice(Sampler0_tex, Sampler0_smp,") !=
              std::string::npos);
  // The definition head must never be rewritten as a call site
  // (float_cast used to wrap signature params: `float2(sampler x_smp)`).
  EXPECT_TRUE(msl.find("float2(sampler") == std::string::npos);
  EXPECT_TRUE(msl.find("sampler source_smp, float2 uv") != std::string::npos);
}
