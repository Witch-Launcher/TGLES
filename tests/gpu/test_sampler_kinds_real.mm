// Extended sampling proofs (Apple only, real pixels). Ground truth: ES 3.2
// spec §8 (cube/3D/array targets, sRGB decode, mipmap completeness 8.14).
// Each TEST binds one sampler kind on its own unit and asserts the sampled
// texel of a fullscreen triangle. Uses ONLY public API + real Apple bridge.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_SMP_HAS_METAL 1
#else
#define TGLES_SMP_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_SMP_HAS_QUARTZ 1
#else
#define TGLES_SMP_HAS_QUARTZ 0
#endif

#include "test_framework.h"

#include <cstdint>

#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

#if TGLES_SMP_HAS_METAL && TGLES_SMP_HAS_QUARTZ

namespace {

void PresentAndWaitSmp(tgles::metal_bridge::MetalBridge& bridge) {
  CAMetalLayer* layer = [CAMetalLayer layer];
  bridge.SetLayer((__bridge void*)layer, 16, 16);
  bridge.Present();
  bridge.WaitForCompletion(bridge.FrameSerial());
}

// Fullscreen triangle, white color, slot-2 float3 direction (0,0,1) at the
// barycenter (same math as the sampling tests: (0.5,0.25,0.25) weights over
// (-1,-1,1),(3,-1,1),(-1,3,1) give exactly (0,0,1)).
void BuildDirScene(tgles::GlesContext& ctx) {
  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  static const float kDir[9] = {-1, -1, 1, 3, -1, 1, -1, 3, 1};
  tgles::GLuint pb = 0, cb = 0, db = 0;
  ctx.buffers().GenBuffers(1, &pb);
  ctx.buffers().GenBuffers(1, &cb);
  ctx.buffers().GenBuffers(1, &db);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kPos), kPos,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kCol), kCol,
                           tgles::kGlStaticDraw);
  ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, db);
  ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kDir), kDir,
                           tgles::kGlStaticDraw);
  tgles::GLuint vao = 0;
  ctx.vertex_arrays().GenVertexArrays(1, &vao);
  ctx.vertex_arrays().BindVertexArray(vao);
  ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, pb);
  ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, cb);
  ctx.vertex_arrays().VertexAttribPointer(2, 3, tgles::kGlFloat,
                                          tgles::kGlFalse, 0, 0, db);
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

tgles::GLuint BuildProgram(tgles::GlesContext& ctx, const char* vs,
                           const char* fs, const char* sampler_name,
                           int unit) {
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
  ctx.programs().UseProgram(prog);
  tgles::GLint loc = ctx.programs().GetUniformLocation(prog, sampler_name);
  if (loc >= 0) ctx.programs().Uniform1i(loc, unit);
  return prog;
}

const char* kDirVs =
    "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
    "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec3 a_dir;\n"
    "out vec4 v_col; out vec3 v_dir; uniform mat4 u_mvp;\n"
    "void main(){ v_col = a_col; v_dir = a_dir;"
    " gl_Position = u_mvp * a_pos; }";

}  // namespace

// Cube faces: +X red, -X green, +Y blue, -Y yellow, +Z magenta, -Z cyan.
// Center direction (0,0,1) selects +Z = magenta.
TEST(SamplerKindsReal, CubePositiveZIsMagenta) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildDirScene(ctx);

  static const std::uint8_t kFaces[6][4] = {
      {255, 0, 0, 255},    // +X red
      {0, 255, 0, 255},    // -X green
      {0, 0, 255, 255},    // +Y blue
      {255, 255, 0, 255},  // -Y yellow
      {255, 0, 255, 255},  // +Z magenta
      {0, 255, 255, 255},  // -Z cyan
  };
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTextureCubeMap, tex);
  for (int f = 0; f < 6; ++f) {
    ctx.textures().TexImage2D(
        tgles::kGlTextureCubeMapPositiveX + static_cast<tgles::GLenum>(f), 0,
        tgles::kGlRgba8, 1, 1, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
        kFaces[f]);
  }
  ctx.textures().TexParameteri(tgles::kGlTextureCubeMap,
                               tgles::kGlTextureMinFilter, tgles::kGlNearest);
  ctx.textures().TexParameteri(tgles::kGlTextureCubeMap,
                               tgles::kGlTextureMagFilter, tgles::kGlNearest);
  ctx.textures().ActiveTexture(0x84C0u + 0);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);

  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec3 v_dir; uniform samplerCube u_cube;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_cube, v_dir); }";
  tgles::GLuint prog = BuildProgram(ctx, kDirVs, fs, "u_cube", 1);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitSmp(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 255u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 255u);
  EXPECT_EQ(px[3], 255u);
}

// 2x2x2 volume: slice 0 red, slice 1 blue. Coord (0.25,0.25,0.75) hits blue.
TEST(SamplerKindsReal, VolumeSlice1IsBlue) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildDirScene(ctx);

  static const std::uint8_t kVol[32] = {
      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,  // z=0 red
      0,   0, 255, 255, 0,   0, 255, 255, 0,   0, 255, 255, 0,   0, 255, 255,  // z=1 blue
  };
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTexture3d, tex);
  ctx.textures().TexImage3D(tgles::kGlTexture3d, 0, tgles::kGlRgba8, 2, 2, 2, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, kVol);
  ctx.textures().TexParameteri(tgles::kGlTexture3d, tgles::kGlTextureMinFilter,
                               tgles::kGlNearest);
  ctx.textures().TexParameteri(tgles::kGlTexture3d, tgles::kGlTextureMagFilter,
                               tgles::kGlNearest);
  ctx.textures().ActiveTexture(0x84C0u + 0);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);

  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec3 v_dir; uniform sampler3D u_vol;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_vol, vec3(0.25, 0.25, 0.75)); }";
  tgles::GLuint prog = BuildProgram(ctx, kDirVs, fs, "u_vol", 1);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitSmp(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 0u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_EQ(px[2], 255u);
  EXPECT_EQ(px[3], 255u);
}

// 2-layer array: layer 0 red, layer 1 green. Coord z=1 selects green.
TEST(SamplerKindsReal, ArrayLayer1IsGreen) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildDirScene(ctx);

  static const std::uint8_t kArr[32] = {
      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,  // red
      0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255,  // green
  };
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTexture2dArray, tex);
  ctx.textures().TexImage3D(tgles::kGlTexture2dArray, 0, tgles::kGlRgba8, 2, 2,
                            2, 0, tgles::kGlRgba, tgles::kGlUnsignedByte, kArr);
  ctx.textures().TexParameteri(tgles::kGlTexture2dArray,
                               tgles::kGlTextureMinFilter, tgles::kGlNearest);
  ctx.textures().TexParameteri(tgles::kGlTexture2dArray,
                               tgles::kGlTextureMagFilter, tgles::kGlNearest);
  ctx.textures().ActiveTexture(0x84C0u + 0);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);

  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec3 v_dir; uniform sampler2DArray u_arr;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_arr, vec3(0.25, 0.25, 1.0)); }";
  tgles::GLuint prog = BuildProgram(ctx, kDirVs, fs, "u_arr", 1);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitSmp(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_EQ(px[0], 0u);
  EXPECT_EQ(px[1], 255u);
  EXPECT_EQ(px[2], 0u);
  EXPECT_EQ(px[3], 255u);
}

// Mipmapped 2D: 4x4 top-half red / bottom-half blue + GenerateMipmap, min
// LINEAR_MIPMAP_LINEAR. The 1x1 tail is the exact box average (127,0,127);
// minified center samples near it. Asserts the chain executes (a
// mipmap-incomplete texture would fail the draw instead).
TEST(SamplerKindsReal, MipmapTailAverageBlends) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildDirScene(ctx);

  static std::uint8_t kTex[64];
  for (int i = 0; i < 16; ++i) {
    const bool top = i < 8;
    kTex[i * 4 + 0] = top ? 255 : 0;
    kTex[i * 4 + 1] = 0;
    kTex[i * 4 + 2] = top ? 0 : 255;
    kTex[i * 4 + 3] = 255;
  }
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTexture2d, tex);
  ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4, 0,
                            tgles::kGlRgba, tgles::kGlUnsignedByte, kTex);
  ctx.textures().GenerateMipmap(tgles::kGlTexture2d);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);
  // Chain proof on the CPU store: tail is the rounded box average.
  tgles::TextureLevel tail =
      ctx.textures().LevelState(tex, tgles::kGlTexture2d, 2);
  EXPECT_TRUE(tail.defined);
  EXPECT_EQ(tail.width, 1);
  EXPECT_EQ(tail.pixels.size(), 4u);
  EXPECT_TRUE(tail.pixels[0] >= 120u && tail.pixels[0] <= 135u);
  EXPECT_EQ(tail.pixels[1], 0u);
  EXPECT_TRUE(tail.pixels[2] >= 120u && tail.pixels[2] <= 135u);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMinFilter,
                               tgles::kGlLinearMipmapLinear);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMagFilter,
                               tgles::kGlLinear);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureWrapS,
                               tgles::kGlRepeat);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureWrapT,
                               tgles::kGlRepeat);
  ctx.textures().ActiveTexture(0x84C0u + 0);

  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "layout(location=2) in vec3 a_dir;\n"
      "out vec4 v_col; out vec2 v_uv; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; v_uv = a_dir.xy * 0.25 + 0.5;"
      " gl_Position = u_mvp * a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv; uniform sampler2D u_tex;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_tex, v_uv); }";
  tgles::GLuint prog = BuildProgram(ctx, vs, fs, "u_tex", 1);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitSmp(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  // Minified checker: blended purple, never a pure primary (which is what
  // level-0-only NEAREST would show) and never black (incomplete chain).
  EXPECT_TRUE(px[0] >= 90u && px[0] <= 165u);
  EXPECT_EQ(px[1], 0u);
  EXPECT_TRUE(px[2] >= 90u && px[2] <= 165u);
  EXPECT_EQ(px[3], 255u);
}

// sRGB hardware decode: texel 188 in an SRGB8_ALPHA8 texture is linear ~0.5
// (-> 127/128), while a naive upload would show 188. The gap proves decode.
TEST(SamplerKindsReal, SrgbDecodesToLinear) {
  auto bridge = tgles::metal_bridge::CreateAppleBridge();
  EXPECT_TRUE(bridge != nullptr);
  if (bridge == nullptr) return;
  if (!bridge->Initialize("Apple3")) return;
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  BuildDirScene(ctx);

  static const std::uint8_t kGray[16] = {
      188, 188, 188, 255, 188, 188, 188, 255,
      188, 188, 188, 255, 188, 188, 188, 255,
  };
  tgles::GLuint tex = 0;
  ctx.textures().GenTextures(1, &tex);
  ctx.textures().ActiveTexture(0x84C0u + 1);
  ctx.textures().BindTexture(tgles::kGlTexture2d, tex);
  ctx.textures().TexStorage2D(tgles::kGlTexture2d, 1, tgles::kGlSrgb8Alpha8, 2,
                              2);
  ctx.textures().TexSubImage2D(tgles::kGlTexture2d, 0, 0, 0, 2, 2,
                               tgles::kGlRgba, tgles::kGlUnsignedByte, kGray);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMinFilter,
                               tgles::kGlNearest);
  ctx.textures().TexParameteri(tgles::kGlTexture2d, tgles::kGlTextureMagFilter,
                               tgles::kGlNearest);
  ctx.textures().ActiveTexture(0x84C0u + 0);
  EXPECT_EQ(ctx.textures().GetError(), tgles::kGlNoError);

  const char* vs =
      "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
      "layout(location=1) in vec4 a_col;\n"
      "layout(location=2) in vec3 a_dir;\n"
      "out vec4 v_col; out vec2 v_uv; uniform mat4 u_mvp;\n"
      "void main(){ v_col = a_col; v_uv = a_dir.xy * 0.0;"
      " gl_Position = u_mvp * a_pos; }";
  const char* fs =
      "#version 320 es\nprecision mediump float;\n"
      "in vec4 v_col; in vec2 v_uv; uniform sampler2D u_tex;\n"
      "out vec4 o_col;\n"
      "void main(){ o_col = v_col * texture(u_tex, v_uv); }";
  tgles::GLuint prog = BuildProgram(ctx, vs, fs, "u_tex", 1);
  EXPECT_TRUE(ctx.programs().LinkSucceeded(prog));

  EXPECT_TRUE(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 3));
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
  PresentAndWaitSmp(*bridge);
  std::uint8_t px[4] = {0};
  EXPECT_TRUE(bridge->ReadbackPixel(8, 8, px));
  EXPECT_TRUE(px[0] >= 120u && px[0] <= 135u);
  EXPECT_EQ(px[1], px[0]);
  EXPECT_EQ(px[2], px[0]);
  EXPECT_EQ(px[3], 255u);
}

#else

TEST(SamplerKindsReal, NoMetalHostSkips) { EXPECT_TRUE(true); }

#endif
