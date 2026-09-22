// macOS 3D render trial (Darwin-only): rotating unit cube through the
// full TGL path — GLES managers (buffers, VAO, program MVP, draw-FBO) +
// GlesContext::RenderFrame + Apple Metal bridge (MSL/PSO/draw/present).
// Writes build/cube_frameN.ppm (P6) and asserts rotation proof:
// every frame's center pixel is non-black and frame 0 differs from frame 4.
// Exit 0 on successor deterministic SKIP without a Metal device; exit 1 on
// any render failure. No MobileGL needed: this proves TGL's own 3D path,
// which is the host side MobileGL DirectGLES would drive (see docs below).

#import <Foundation/Foundation.h>
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#endif

#include <cmath>
#include <cstdio>
#include <vector>

#include "cube_model.h"
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
      std::printf("TRIAL FAIL: %s\n", msg);               \
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
      std::printf("TRIAL SKIP: no Metal bridge on this host\n");
      return 0;
    }
    if (!bridge->Initialize("Apple3")) {
      std::printf("TRIAL SKIP: no Metal device on this host\n");
      return 0;
    }

    tgles::GlesContext ctx = tgles::GlesContext::Create(false);

    // Position stream (xyz) and color stream (rgb -> rgba) in GPU buffers.
    tgles::GLuint pb = 0, cb = 0;
    ctx.buffers().GenBuffers(1, &pb);
    ctx.buffers().GenBuffers(1, &cb);
    ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, pb);
    ctx.buffers().BufferData(tgles::kGlArrayBuffer,
                             sizeof(CubeModel::kPositions),
                             CubeModel::kPositions, tgles::kGlStaticDraw);
    std::vector<float> colors_rgba(CubeModel::kVertexCount * 4);
    for (std::size_t v = 0; v < CubeModel::kVertexCount; ++v) {
      colors_rgba[v * 4 + 0] = CubeModel::kColors[v * 3 + 0];
      colors_rgba[v * 4 + 1] = CubeModel::kColors[v * 3 + 1];
      colors_rgba[v * 4 + 2] = CubeModel::kColors[v * 3 + 2];
      colors_rgba[v * 4 + 3] = 1.0f;
    }
    ctx.buffers().BindBuffer(tgles::kGlArrayBuffer, cb);
    ctx.buffers().BufferData(tgles::kGlArrayBuffer,
                             colors_rgba.size() * sizeof(float),
                             colors_rgba.data(), tgles::kGlStaticDraw);

    tgles::GLuint vao = 0;
    ctx.vertex_arrays().GenVertexArrays(1, &vao);
    ctx.vertex_arrays().BindVertexArray(vao);
    ctx.vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                            tgles::kGlFalse, 0, 0, pb);
    ctx.vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                            tgles::kGlFalse, 0, 0, cb);
    ctx.vertex_arrays().EnableVertexAttribArray(0);
    ctx.vertex_arrays().EnableVertexAttribArray(1);

    // MVP program: the uniform name RenderFrame looks up.
    const char* vs =
        "#version 320 es\nuniform mat4 u_modelViewProj;\nvoid main() {}";
    const char* fs = "#version 320 es\nvoid main() {}";
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
    TRIAL_CHECK(ctx.programs().LinkSucceeded(prog), "program link");
    ctx.programs().UseProgram(prog);
    const tgles::GLint mvp_loc =
        ctx.programs().GetUniformLocation(prog, "u_modelViewProj");
    TRIAL_CHECK(mvp_loc >= 0, "mvp uniform location");

    // Draw-FBO sized target.
    tgles::GLuint t = 0;
    ctx.textures().GenTextures(1, &t);
    ctx.textures().BindTexture(tgles::kGlTexture2d, t);
    ctx.textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8,
                              kSize, kSize, 0, tgles::kGlRgba,
                              tgles::kGlUnsignedByte, nullptr);
    tgles::GLuint fbo = 0;
    ctx.framebuffers().GenFramebuffers(1, &fbo);
    ctx.framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, fbo);
    ctx.framebuffers().FramebufferTexture2D(
        tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
        tgles::kGlTexture2d, t, 0);
    TRIAL_CHECK(ctx.GetError() == tgles::kGlNoError, "setup has GL error");

#if __has_include(<QuartzCore/QuartzCore.h>)
    CAMetalLayer* layer = [CAMetalLayer layer];
#else
    void* layer = nullptr;
#endif
    bridge->SetLayer((__bridge void*)layer, kSize, kSize);

    const float kPi = 3.141592653589793f;
    std::vector<std::uint8_t> pixels(kSize * kSize * 4);
    std::uint64_t sums[kFrames] = {};
    for (int i = 0; i < kFrames; ++i) {
      const float angle = (kPi / 180.0f) * (15.0f * i);
      const Mat4 mvp = Mat4::Multiply(
          Mat4::Perspective(kPi / 3.0f, 1.0f, 0.1f, 100.0f),
          Mat4::Multiply(Mat4::Translate(0, 0, -5), Mat4::RotateY(angle)));
      ctx.programs().UniformMatrix4fv(mvp_loc, 1, tgles::kGlFalse, mvp.m);
      TRIAL_CHECK(ctx.GetError() == tgles::kGlNoError, "mvp upload");
      TRIAL_CHECK(ctx.RenderFrame(*bridge, tgles::kGlTriangles, 0, 36),
                  "RenderFrame");
      TRIAL_CHECK(bridge->Present(), "Present");
      TRIAL_CHECK(
          bridge->WaitForCompletion(bridge->FrameSerial()), "fence wait");
      TRIAL_CHECK(bridge->BlitToCpu(pixels.data(), pixels.size()),
                  "readback");
      const std::size_t c = (kSize / 2 * kSize + kSize / 2) * 4;
      TRIAL_CHECK(pixels[c] != 0 || pixels[c + 1] != 0 ||
                      pixels[c + 2] != 0,
                  "center pixel is black");
      sums[i] = Checksum(pixels.data(), pixels.size());
      char path[64];
      std::snprintf(path, sizeof(path), "cube_frame%d.ppm", i);
      TRIAL_CHECK(WritePpm(path, pixels.data(), kSize, kSize), "ppm write");
      std::printf("frame %d: checksum=%llu center=(%u,%u,%u)\n", i,
                  (unsigned long long)sums[i], pixels[c], pixels[c + 1],
                  pixels[c + 2]);
    }
    // Rotation proof: opposite frames must differ.
    TRIAL_CHECK(sums[0] != sums[4], "frames 0 and 4 identical");
    TRIAL_CHECK(bridge->GetError() == tgles::kGlNoError, "bridge error");
    TRIAL_CHECK(ctx.GetError() == tgles::kGlNoError, "ctx error");
    std::printf("TRIAL PASS: %d frames, 36-vert cube, rotation proven\n",
                kFrames);
    return 0;
  }
}
