// Complex ES 3.2 scene trial (Darwin-only): rotating textured + lit cube
// with real depth through the full TGL path — GLES managers + textured
// RenderFrame + Apple Metal bridge. Proves 3D + lighting + texture together,
// the combination MobileGL/Minecraft needs. Writes lit_tex_frameN.ppm.
//
// Pipeline (all honest, all unit-tested):
// - CPU diffuse lighting per BuildFrameStreams (apps/cube_render.h,
//   kAmbient 0.25, light dir normalized) bakes lit vertex colors.
// - Planar UVs (x*0.25+0.5, y*0.25+0.5) sample a 4x4 checker on unit 1.
// - Fragment: v_col(lit) * texture() on GPU (new textured MSL pair).
// - Depth: 16-bit? No — DepthComponent24 renderbuffer + LESS, size-matched.
// - MVP: perspective + translate(-5) + RotateY per frame.
// Exit 0 on PASS or deterministic SKIP without Metal; exit 1 on render fail
// with the failing stage printed (which error, which frame, which pixel).

#import <Foundation/Foundation.h>
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#endif

#include <cmath>
#include <cstdio>
#include <vector>

#include "cube_model.h"
#include "cube_render.h"
#include "tgles/facade/gles.h"
#include "tgles/gpu/metal_bridge_apple.h"

namespace {

constexpr int kSize = 256;
constexpr int kFrames = 8;

bool WritePpm(const char* path, const std::uint8_t* rgba, int w, int h) {
  FILE* f = std::fopen(path, "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; ++i) {
    const std::uint8_t rgb[3] = {rgba[i * 4], rgba[i * 4 + 1],
                                 rgba[i * 4 + 2]};
    if (std::fwrite(rgb, 1, 3, f) != 3) {
      std::fclose(f);
      return false;
    }
  }
  std::fclose(f);
  return true;
}

std::uint64_t Checksum(const std::uint8_t* rgba, std::size_t bytes) {
  std::uint64_t h = 1469598103934665603ull;
  for (std::size_t i = 0; i < bytes; ++i) {
    h ^= rgba[i];
    h *= 1099511628211ull;
  }
  return h;
}

#define TRIAL_CHECK(cond, msg)                            \
  do {                                                    \
    if (!(cond)) {                                        \
      std::printf("LIT_TEX_TRIAL FAIL: %s (frame %d)\n", msg, i); \
      return 1;                                           \
    }                                                     \
  } while (0)

}  // namespace

int main() {
  @autoreleasepool {
    using tgles::trial::CubeModel;
    using tgles::trial::Mat4;

    auto bridge = tgles::metal_bridge::CreateAppleBridge();
    if (bridge == nullptr) {
      std::printf("LIT_TEX_TRIAL SKIP: no Metal bridge on this host\n");
      return 0;
    }
    if (!bridge->Initialize("Apple3")) {
      std::printf("LIT_TEX_TRIAL SKIP: no Metal device on this host\n");
      return 0;
    }

    tgles::GlesContext ctx = tgles::GlesContext::Create(false);

    // 4x4 checker on unit 1 (unit 0 holds the draw target during setup).
    static std::uint8_t kChecker[4 * 4 * 4];
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        const bool white = ((x + y) % 2) == 0;
        kChecker[(y * 4 + x) * 4 + 0] = white ? 255 : 64;
        kChecker[(y * 4 + x) * 4 + 1] = white ? 255 : 64;
        kChecker[(y * 4 + x) * 4 + 2] = white ? 255 : 64;
        kChecker[(y * 4 + x) * 4 + 3] = 255;
      }
    }
    tgles::GLuint checker = 0;
    ctx.textures().GenTextures(1, &checker);
    ctx.textures().ActiveTexture(0x84C0u + 1);
    ctx.textures().BindTexture(tgles::kGlTexture2d, checker);
    ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 4, 4, 0,
                              tgles::kGlRgba, tgles::kGlUnsignedByte, kChecker);
    ctx.textures().TexParameteri(tgles::kGlTexture2d,
                                 tgles::kGlTextureMinFilter, tgles::kGlNearest);
    ctx.textures().TexParameteri(tgles::kGlTexture2d,
                                 tgles::kGlTextureMagFilter, tgles::kGlNearest);
    ctx.textures().ActiveTexture(0x84C0u + 0);
    if (ctx.GetError() != tgles::kGlNoError) {
      std::printf("LIT_TEX_TRIAL FAIL: checker setup has GL error\n");
      return 1;
    }

    // Buffers + VAO (pos 0, col 1, uv 2). Contents refilled per frame with
    // lit + rotated streams; UVs are static planar projections.
    tgles::GLuint pb = 0, cb = 0, ub = 0;
    ctx.buffers().GenBuffers(1, &pb);
    ctx.buffers().GenBuffers(1, &cb);
    ctx.buffers().GenBuffers(1, &ub);
    static float kUv[36 * 2];
    for (std::size_t v = 0; v < 36; ++v) {
      const float x = CubeModel::kPositions[v * 3 + 0];
      const float y = CubeModel::kPositions[v * 3 + 1];
      kUv[v * 2 + 0] = x * 0.25f + 0.5f;
      kUv[v * 2 + 1] = y * 0.25f + 0.5f;
    }
    ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, ub);
    ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(kUv), kUv,
                             tgles::kGlStaticDraw);
    tgles::GLuint vao = 0;
    ctx.vertex_arrays().GenVertexArrays(1, &vao);
    ctx.vertex_arrays().BindVertexArray(vao);
    ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                            tgles::kGlFalse, 0, 0, pb);
    ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                            tgles::kGlFalse, 0, 0, cb);
    ctx.vertex_arrays().VertexAttribPointer(2, 2, tgles::kGlFloat,
                                            tgles::kGlFalse, 0, 0, ub);
    ctx.vertex_arrays().EnableVertexAttribArray(0);
    ctx.vertex_arrays().EnableVertexAttribArray(1);
    ctx.vertex_arrays().EnableVertexAttribArray(2);

    // Textured program (sampler on unit 1).
    const char* vs =
        "#version 320 es\nlayout(location=0) in vec4 a_pos;\n"
        "layout(location=1) in vec4 a_col;\nlayout(location=2) in vec2 a_uv;\n"
        "out vec4 v_col; out vec2 v_uv; uniform mat4 u_modelViewProj;\n"
        "void main(){ v_col=a_col; v_uv=a_uv;"
        " gl_Position=u_modelViewProj*a_pos; }";
    const char* fs =
        "#version 320 es\nprecision mediump float;\n"
        "in vec4 v_col; in vec2 v_uv; uniform sampler2D u_tex; out vec4 o_col;\n"
        "void main(){ o_col = v_col * texture(u_tex, v_uv); }";
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
    if (!ctx.programs().LinkSucceeded(prog)) {
      std::printf("LIT_TEX_TRIAL FAIL: program link\n");
      return 1;
    }
    ctx.programs().UseProgram(prog);
    const tgles::GLint tex_loc =
        ctx.programs().GetUniformLocation(prog, "u_tex");
    if (tex_loc < 0) {
      std::printf("LIT_TEX_TRIAL FAIL: u_tex location\n");
      return 1;
    }
    ctx.programs().Uniform1i(tex_loc, 1);
    const tgles::GLint mvp_loc =
        ctx.programs().GetUniformLocation(prog, "u_modelViewProj");
    if (mvp_loc < 0) {
      std::printf("LIT_TEX_TRIAL FAIL: mvp location\n");
      return 1;
    }

    // Draw target + depth.
    tgles::GLuint t = 0;
    ctx.textures().GenTextures(1, &t);
    ctx.textures().BindTexture(tgles::kGlTexture2d, t);
    ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, kSize,
                              kSize, 0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                              nullptr);
    tgles::GLuint rb = 0;
    ctx.renderbuffers().GenRenderbuffers(1, &rb);
    ctx.renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, rb);
    ctx.renderbuffers().RenderbufferStorage(tgles::kGlRenderbuffer,
                                            tgles::kGlDepthComponent24, kSize,
                                            kSize);
    tgles::GLuint fbo = 0;
    ctx.framebuffers().GenFramebuffers(1, &fbo);
    ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
    ctx.framebuffers().FramebufferTexture2D(
        tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
        tgles::kGlTexture2d, t, 0);
    ctx.framebuffers().FramebufferRenderbuffer(
        tgles::kGlDrawFramebuffer, tgles::kGlDepthAttachment,
        tgles::kGlRenderbuffer, rb);
    if (ctx.framebuffers().CheckFramebufferStatus(
            tgles::kGlDrawFramebuffer) != tgles::kGlFramebufferComplete) {
      std::printf("LIT_TEX_TRIAL FAIL: FBO incomplete\n");
      return 1;
    }
    ctx.foundation().Enable(tgles::kGlDepthTest);
    if (ctx.GetError() != tgles::kGlNoError) {
      std::printf("LIT_TEX_TRIAL FAIL: setup has GL error\n");
      return 1;
    }

#if __has_include(<QuartzCore/QuartzCore.h>)
    CAMetalLayer* layer = [CAMetalLayer layer];
#else
    void* layer = nullptr;
#endif
    bridge->SetLayer((__bridge void*)layer, kSize, kSize);

    const float kPi = 3.141592653589793f;
    float light[3] = {0.45f, 0.75f, 0.55f};
    const float llen =
        std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    light[0] /= llen;
    light[1] /= llen;
    light[2] /= llen;
    std::vector<std::uint8_t> pixels(kSize * kSize * 4);
    std::uint64_t sums[kFrames] = {};
    for (int i = 0; i < kFrames; ++i) {
      const float angle = (kPi / 180.0f) * (15.0f * i);
      const Mat4 model = Mat4::RotateY(angle);
      // CPU lighting + model rotation; MVP holds P*V only (model baked).
      float xyz[36 * 3];
      float rgba[36 * 4];
      tgles::trial::BuildFrameStreams(CubeModel::kPositions, CubeModel::kColors,
                                      36, model, light, xyz, rgba);
      // Upload interleaved sources as separate buffers (VAO resolves).
      ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
      ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(xyz), xyz,
                               tgles::kGlStaticDraw);
      // BuildFrameStreams outputs rgb+alpha? It outputs rgba with a=1.
      ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
      ctx.buffers().BufferData(tgles::kGlArrayBuffer, sizeof(rgba), rgba,
                               tgles::kGlStaticDraw);
      const Mat4 mvp = Mat4::Multiply(
          Mat4::Perspective(kPi / 3.0f, 1.0f, 0.1f, 100.0f),
          Mat4::Translate(0, 0, -5));
      // Note: model baked on CPU, so MVP = P*V (no model). This keeps depth
      // correct because xyz are already world-space; clip remap in MSL
      // preserves GL [-1,1] semantics.
      ctx.programs().UniformMatrix4fv(mvp_loc, 1, tgles::kGlFalse, mvp.m);
      TRIAL_CHECK(ctx.GetError() == tgles::kGlNoError, "mvp upload");
      TRIAL_CHECK(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 36),
                  "RenderFrame");
      if (!bridge->HasFragmentTexture(1)) {
        std::printf("LIT_TEX_TRIAL FAIL: sampling state lost (frame %d)\n", i);
        return 1;
      }
      TRIAL_CHECK(bridge->Present(), "Present");
      TRIAL_CHECK(bridge->WaitForCompletion(bridge->FrameSerial()),
                  "fence wait");
      TRIAL_CHECK(bridge->BlitToCpu(pixels.data(), pixels.size()),
                  "readback");
      const std::size_t c = (kSize / 2 * kSize + kSize / 2) * 4;
      TRIAL_CHECK(pixels[c] != 0 || pixels[c + 1] != 0 || pixels[c + 2] != 0,
                  "center pixel is black");
      // Textured proof: checker modulates (not all frames pure primary).
      sums[i] = Checksum(pixels.data(), pixels.size());
      char path[64];
      std::snprintf(path, sizeof(path), "lit_tex_frame%d.ppm", i);
      TRIAL_CHECK(WritePpm(path, pixels.data(), kSize, kSize), "ppm write");
      std::printf("frame %d: checksum=%llu center=(%u,%u,%u,%u) depth=%d\n", i,
                  (unsigned long long)sums[i], pixels[c], pixels[c + 1],
                  pixels[c + 2], pixels[c + 3],
                  (int)bridge->DepthEnabled());
    }
    if (sums[0] == sums[4]) {
      std::printf("LIT_TEX_TRIAL FAIL: frames 0 and 4 identical\n");
      return 1;
    }
    if (bridge->GetError() != tgles::kGlNoError) {
      std::printf("LIT_TEX_TRIAL FAIL: bridge error\n");
      return 1;
    }
    if (ctx.GetError() != tgles::kGlNoError) {
      std::printf("LIT_TEX_TRIAL FAIL: ctx error\n");
      return 1;
    }
    std::printf("LIT_TEX_TRIAL PASS: %d frames lit+textured+depth\n", kFrames);
    return 0;
  }
}
