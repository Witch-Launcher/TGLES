// Milestone proof: app GLSL lighting executes ON THE GPU through the
// GLSL->MSL translator (not CPU-baked vertex colors). Ground truth: GLSL ES
// 3.20 §4 (normalize/dot/max built-ins) + ES 3.2 diffuse lighting semantics.
// The fragment below computes `0.25 + 0.75*max(dot(N,L),0)` per pixel from a
// normal varying and a light-dir uniform; two draws (light facing / away)
// must yield full-red vs ambient-only pixels. Uses ONLY public API + real
// Apple bridge pixels.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_TSL_HAS_METAL 1
#else
#define TGLES_TSL_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_TSL_HAS_QUARTZ 1
#else
#define TGLES_TSL_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_TSL_HAS_METAL && TGLES_TSL_HAS_QUARTZ

namespace {

void PresentAndWaitTsl(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, 16, 16);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

// Fullscreen triangle, white color, normal (0,0,1) in slot 2 (vec2 + implicit
// z=1 in-shader via vec3(v_n, 1.0)).
void BuildNormalScene(tgles::GlesContext& ctx) {
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  static const float kNrm[6] = {0, 0, 0, 0, 0, 0};
  tgles::GLuint pb = 0, cb = 0, nb = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().GenBuffers(1, &nb);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, nb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kNrm), kNrm,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, cb);
  ctx.vertex_arrays().VertexAttribPointer(2, 2, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, nb);
  ctx.vertex_arrays().EnableVertexAttribArray(0);
  ctx.vertex_arrays().EnableVertexAttribArray(1);
  ctx.vertex_arrays().EnableVertexAttribArray(2);
  tgles::GLuint t = 0;
  ctx.textures().GenTextures(1, &t);
  ctx.textures().BindTexture(tgles::kGlTexture2d, t);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 16, 16, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tgles::GLuint f = 0;
  ctx.framebuffers().GenFramebuffers(1, &f);
  ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, f);
  ctx.framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, t, 0);
}

const char* kLightVs =
    "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
    "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec2 a_n;\n"
    "out vec4 v_col; out vec2 v_n; uniform mat4 u_mvp;\n"
    "void main(){ v_col = a_col; v_n = a_n; gl_Position = u_mvp * a_pos; }";
const char* kLightFs =
    "#version 320 es\nprecision mediump float;\n"
    "in vec4 v_col; in vec2 v_n; uniform vec4 u_light; out vec4 o_col;\n"
    "void main(){ float d = max(dot(normalize(vec3(v_n, 1.0)), "
    "normalize(u_light.xyz)), 0.0);"
    " o_col = vec4(v_col.rgb * (0.25 + 0.75 * d), 1.0); }";

void DrawWithLight(tgles::GlesContext& ctx,
                   tgles::metal_bridge::MetalBridge& bridge,
                   tgles::GLint light_loc, float lz) {
  const float light[4] = {0, 0, lz, 0};
  ctx.programs().Uniform4f(light_loc, light[0], light[1], light[2], light[3]);
  EXPECT_TRUE(ctx.RenderFrame(bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitTsl(bridge);
}

}  // namespace

TEST(TranslatedShaderReal, GpuDiffuseLightingFacingAndAway) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildNormalScene(ctx);

  tgles::GLuint v = ctx.shaders().CreateShader(tgles::kGlVertexShader);
  ctx.shaders().ShaderSource(v, 1, &kLightVs, nullptr);
  ctx.shaders().CompileShader(v);
  tgles::GLuint f = ctx.shaders().CreateShader(tgles::kGlFragmentShader);
  ctx.shaders().ShaderSource(f, 1, &kLightFs, nullptr);
  ctx.shaders().CompileShader(f);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, v);
  ctx.programs().AttachShader(prog, f);
  ctx.programs().LinkProgram(prog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  tgles::GLint light_loc = ctx.programs().GetUniformLocation(prog, "u_light");
  EXPECT_TRUE(light_loc >= 0);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // Light facing the +Z normal: d=1 -> full red (CPU baking would also give
  // red, but here the GPU computed dot() from APP GLSL — the milestone).
  DrawWithLight(ctx, *bridge, light_loc, 1.0f);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);

  // Light behind: d=0 -> ambient 0.25 -> dark red (64,0,0). A passthrough
  // fragment ignoring the shader would still show full red, so this second
  // draw is what proves execution (not just compilation).
  DrawWithLight(ctx, *bridge, light_loc, -1.0f);
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_TRUE(px[0] >= 60u && px[0] <= 68u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

TEST(TranslatedShaderReal, UboTintReachesFragment) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildNormalScene(ctx);

  // Tint comes from a std140 uniform block, not a plain uniform: the facade
  // must upload the bound UNIFORM_BUFFER range to constant buffer 2.
  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec2 a_n;\n"
      "out vec4 v_col; out vec2 v_n; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; v_n = a_n; gl_Position = u_mvp * a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_n;\n"
      "layout(std140) uniform Frame { mat4 u_vp; vec4 u_tint; };\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = vec4(v_col.rgb, 1.0) * u_tint; }";
  tgles::GLuint v = ctx.shaders().CreateShader(tgles::kGlVertexShader);
  ctx.shaders().ShaderSource(v, 1, &vs, nullptr);
  ctx.shaders().CompileShader(v);
  tgles::GLuint f = ctx.shaders().CreateShader(tgles::kGlFragmentShader);
  ctx.shaders().ShaderSource(f, 1, &fs, nullptr);
  ctx.shaders().CompileShader(f);
  tgles::GLuint prog = ctx.programs().CreateProgram();
  ctx.programs().AttachShader(prog, v);
  ctx.programs().AttachShader(prog, f);
  ctx.programs().LinkProgram(prog);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  ctx.programs().UseProgram(prog);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  // UBO store: identity MVP + gray 0.5 tint (std140: mat4 then vec4 = 80B).
  // Gray discriminates three ways: broken/zero UBO -> black; dropped
  // multiply (passthrough) -> full red; correct -> half red (127).
  static const float kUbo[20] = {1, 0, 0, 0, 0, 1, 0, 0,
                                 0, 0, 1, 0, 0, 0, 0, 1,
                                 0.5f, 0.5f, 0.5f, 1};
  tgles::GLuint ub = 0;
  ctx.buffers().GenBuffers(1, &ub);
  ctx.buffers().BindBuffer(tgles::kGlUniformBuffer, ub);
  ctx.buffers().BufferData(tgles::kGlUniformBuffer, sizeof(kUbo), kUbo,
                           tgles::kGlStaticDraw);
  ctx.programs().UniformBlockBinding(prog, 0, 0);
  ctx.buffers().BindBufferBase(tgles::kGlUniformBuffer, 0, ub);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitTsl(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_TRUE(px[0] >= 124u && px[0] <= 131u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

#else

TEST(TranslatedShaderReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
