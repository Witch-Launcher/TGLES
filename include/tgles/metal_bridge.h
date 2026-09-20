#ifndef TGLES_METAL_BRIDGE_H
#define TGLES_METAL_BRIDGE_H

// Metal execution + CAMetalLayer present bridge contract (step 11).
// Pure C++ on purpose: this header must NEVER include <Metal/*>,
// <QuartzCore/*> or <UIKit/*> so tgles_core keeps cross-compiling for the
// macOS-Intel host, iphoneos/arm64 device and iphonesimulator builds.
// The real MTL calls live in src/metal_bridge_apple.mm (OBJCXX target
// tgles_metal_bridge, -framework Metal/QuartzCore/Foundation, iOS SDK).
// NullMetalBridge is the safe host no-op; MockMetalBridge records calls and
// simulates the drawable/present + frame-fence serials that MobileGL
// DirectGLES.cpp:10601 Present() requires (fence BEFORE eglSwapBuffers,
// poll AFTER to advance the completed watermark gating buffer recycling).

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "tgles/backend.h"
#include "tgles/error.h"
#include "tgles/gl_types.h"
#include "tgles/raster.h"  // kGlLess default + compare-func range.

namespace tgles {
namespace metal_bridge {

// Depth-test configuration for one frame (spec 9.4 attachments + §13-15
// per-fragment state, executed by the bridge because it owns the GPU depth
// target). The facade fills this from the draw-FBO depth attachment +
// DEPTH_TEST enable + DepthFunc/Mask/ClearDepth state on every RenderFrame
// (disabled config when the test is off or no depth is attached).
// Stencil testing is NOT executed: a DEPTH24_STENCIL8 attachment only lends
// its depth aspect + size.
// Metal NDC constraint: clip-space z must already be in [0, 1] (GL-style
// negative z is clipped away before the depth test ever runs — probed on
// host Intel, see RealDepthResolvesOverlap). Perspective matrices aimed at
// GL's [-1, 1] range need remapping for near-camera geometry.
struct DepthConfig {
  bool enabled = false;
  GLenum func = kGlLess;  // NEVER (0x0200) .. ALWAYS (0x0207).
  bool write_mask = true;
  GLfloat clear_depth = 1.0f;
};

class MetalBridge {
 public:
  virtual ~MetalBridge() = default;

  virtual bool Initialize(const char* family) = 0;
  virtual bool IsInitialized() const = 0;

  // Execution: PSO + resources. 0 == failure (with GetError set).
  virtual std::uint32_t CreateRenderPipeline(const backend::PsoKey& key) = 0;
  virtual std::uint32_t CreateBuffer(GLsizeiptr size,
                                     backend::StorageMode mode) = 0;
  virtual std::uint32_t CreateTexture(GLsizei width, GLsizei height,
                                      GLenum internalformat,
                                      const char* family) = 0;

  // Frame + encoder ordering (mirrors backend::CommandPlan).
  virtual bool BeginFrame(GLsizei width, GLsizei height) = 0;
  virtual void BeginRenderPass() = 0;
  virtual void Draw() = 0;
  virtual void EndRenderPass() = 0;
  virtual void Blit() = 0;
  virtual bool CommitFrame() = 0;
  virtual bool Committed() const = 0;
  virtual GLuint DrawCount() const = 0;

  // Vertex input for Draw (production path): interleaved float4
  // position+color pairs matching the MSL VertexIn layout
  // (attribute(0) position, attribute(1) color, stride bytes).
  // Bindings persist across frames (GL VAO/uniform semantics); the Apple
  // bridge fails Draw closed without vertex data, while the Mock validates
  // encoder ordering only and draws without vertex uploads.
  virtual void SetVertexBytes(const void* data, std::size_t bytes,
                              std::uint32_t stride) = 0;
  virtual void SetMVP(const float mvp[16]) = 0;

  // Depth target for the next frame. Must precede BeginFrame so the bridge
  // can size the depth texture with the color target and bake the matching
  // pipeline state. An out-of-range func fails closed (INVALID_ENUM).
  virtual void ConfigureDepth(const DepthConfig& config) = 0;
  virtual bool DepthEnabled() const = 0;

  // Pixel readback of the current frame target (device-only; the Mock
  // has no rasterizer and fails these closed with INVALID_OPERATION).
  virtual bool ReadbackPixel(GLint x, GLint y, std::uint8_t out_rgba[4]) = 0;
  virtual bool BlitToCpu(void* dst, std::size_t bytes) = 0;

  // GPU-completion fence for a present serial (glFinish/FenceSync host
  // model; the real bridge waits on the command buffer / shared event).
  virtual bool WaitForCompletion(std::uint64_t serial) = 0;

  // Present: requires an attached layer with non-zero area, like MobileGL
  // WSI zero-area suspend. Advances frame serial + swap count; completed
  // serial follows via the fence ring.
  virtual void SetLayer(void* ca_metal_layer, GLsizei width,
                        GLsizei height) = 0;
  virtual void Resize(GLsizei width, GLsizei height) = 0;
  virtual bool Present() = 0;

  virtual std::uint64_t FrameSerial() const = 0;
  virtual std::uint64_t CompletedSerial() const = 0;
  virtual std::uint64_t SwapCount() const = 0;

  virtual GLenum GetError() = 0;
};

// Safe host no-op: every creation returns 0, every frame op fails closed.
class NullMetalBridge : public MetalBridge {
 public:
  bool Initialize(const char* family) override;
  bool IsInitialized() const override;
  std::uint32_t CreateRenderPipeline(const backend::PsoKey& key) override;
  std::uint32_t CreateBuffer(GLsizeiptr size,
                             backend::StorageMode mode) override;
  std::uint32_t CreateTexture(GLsizei width, GLsizei height,
                              GLenum internalformat,
                              const char* family) override;
  bool BeginFrame(GLsizei width, GLsizei height) override;
  void BeginRenderPass() override;
  void Draw() override;
  void EndRenderPass() override;
  void Blit() override;
  bool CommitFrame() override;
  bool Committed() const override;
  GLuint DrawCount() const override;
  void SetVertexBytes(const void* data, std::size_t bytes,
                       std::uint32_t stride) override;
  void SetMVP(const float mvp[16]) override;
  void ConfigureDepth(const DepthConfig& config) override;
  bool DepthEnabled() const override;
  bool ReadbackPixel(GLint x, GLint y, std::uint8_t out_rgba[4]) override;
  bool BlitToCpu(void* dst, std::size_t bytes) override;
  bool WaitForCompletion(std::uint64_t serial) override;
  void SetLayer(void* ca_metal_layer, GLsizei width,
                GLsizei height) override;
  void Resize(GLsizei width, GLsizei height) override;
  bool Present() override;
  std::uint64_t FrameSerial() const override;
  std::uint64_t CompletedSerial() const override;
  std::uint64_t SwapCount() const override;
  GLenum GetError() override;

 private:
  ErrorQueue errors_;
};

// Test double: validates the contract without a GPU. Present advances the
// completed watermark immediately (fence-signal simulation); the real .mm
// polls MTLSharedEvent/fences instead.
class MockMetalBridge : public MetalBridge {
 public:
  MockMetalBridge();

  bool Initialize(const char* family) override;
  bool IsInitialized() const override;
  std::string DeviceFamily() const;
  std::uint32_t CreateRenderPipeline(const backend::PsoKey& key) override;
  std::size_t PipelineCount() const;
  std::uint32_t CreateBuffer(GLsizeiptr size,
                             backend::StorageMode mode) override;
  bool IsBufferShared(std::uint32_t handle) const;
  std::uint32_t CreateTexture(GLsizei width, GLsizei height,
                              GLenum internalformat,
                              const char* family) override;
  bool BeginFrame(GLsizei width, GLsizei height) override;
  void BeginRenderPass() override;
  void Draw() override;
  void EndRenderPass() override;
  void Blit() override;
  bool CommitFrame() override;
  bool Committed() const override;
  GLuint DrawCount() const override;
  void SetVertexBytes(const void* data, std::size_t bytes,
                       std::uint32_t stride) override;
  void SetMVP(const float mvp[16]) override;
  void ConfigureDepth(const DepthConfig& config) override;
  bool DepthEnabled() const override;
  DepthConfig LastDepthConfig() const;
  bool ReadbackPixel(GLint x, GLint y, std::uint8_t out_rgba[4]) override;
  bool BlitToCpu(void* dst, std::size_t bytes) override;
  bool WaitForCompletion(std::uint64_t serial) override;
  void SetLayer(void* ca_metal_layer, GLsizei width,
                GLsizei height) override;
  void Resize(GLsizei width, GLsizei height) override;
  bool Present() override;
  std::uint64_t FrameSerial() const override;
  std::uint64_t CompletedSerial() const override;
  std::uint64_t SwapCount() const override;
  GLenum GetError() override;

 private:
  ErrorQueue errors_;
  bool initialized_ = false;
  std::string family_;
  std::map<backend::PsoKey, std::uint32_t> pipelines_;
  std::uint32_t next_handle_ = 1;
  std::map<std::uint32_t, backend::StorageMode> buffers_;
  bool frame_open_ = false;
  bool render_open_ = false;
  bool committed_ = false;
  GLuint draws_ = 0;
  void* layer_ = nullptr;
  GLsizei layer_width_ = 0;
  GLsizei layer_height_ = 0;
  bool has_vertex_ = false;
  bool has_mvp_ = false;
  DepthConfig depth_cfg_;
  std::uint64_t frame_serial_ = 0;
  std::uint64_t completed_serial_ = 0;
  std::uint64_t swaps_ = 0;
};

}  // namespace metal_bridge
}  // namespace tgles

#endif  // TGLES_METAL_BRIDGE_H
