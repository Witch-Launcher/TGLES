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

#include "tgles/gpu/backend.h"
#include "tgles/gpu/glsl_to_msl.h"
#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"
#include "tgles/pipeline/draw.h"  // kGlPoints/Lines/.../Triangles for Draw().
#include "tgles/pipeline/raster.h"  // kGlLess default + compare-func range.

namespace tgles {
namespace metal_bridge {

// Depth-test configuration for one frame (spec 9.4 attachments + §13-15
// per-fragment state, executed by the bridge because it owns the GPU depth
// target). The facade fills this from the draw-FBO depth attachment +
// DEPTH_TEST enable + DepthFunc/Mask/ClearDepth state on every RenderFrame
// (disabled config when the test is off or no depth is attached).
// Stencil testing is NOT executed: a DEPTH24_STENCIL8 attachment only lends
// its depth aspect + size.
// GL depth semantics: apps submit GL clip-space z in [-1, 1]; the bridge MSL
// remaps z*0.5+w*0.5 onto Metal NDC [0, 1] (order-preserving), so LESS and
// friends behave exactly like on a GL driver.
struct DepthConfig {
  bool enabled = false;
  GLenum func = kGlLess;  // NEVER (0x0200) .. ALWAYS (0x0207).
  bool write_mask = true;
  GLfloat clear_depth = 1.0f;
  // Polygon offset (spec 14.6, glPolygonOffset): encoder depth bias, applied
  // only when bias_enabled (POLYGON_OFFSET_FILL). Slope-scaled + constant.
  bool bias_enabled = false;
  GLfloat bias_factor = 0.0f;
  GLfloat bias_units = 0.0f;
};

// Cull + viewport/scissor raster state (spec 13-14): encoder state applied
// every pass. Viewport maps NDC to the target rect; scissor clips to its
// rect only while scissor_enabled (otherwise the full target).
struct CullConfig {
  bool enabled = false;
  GLenum mode = 0x0405;        // BACK default.
  GLenum front_face = 0x0901;  // CCW default.
};
struct ViewportConfig {
  GLint x = 0, y = 0;
  GLsizei width = 0, height = 0;
  bool scissor_enabled = false;
  GLint scissor_x = 0, scissor_y = 0;
  GLsizei scissor_width = 0, scissor_height = 0;
};

// Stencil-test configuration for one frame (spec §13-15, executed by the
// bridge which owns the stencil target). The facade fills this from the
// draw-FBO stencil/depth-stencil attachment + STENCIL_TEST enable +
// StencilFunc/Mask/Op/ClearStencil state on every RenderFrame (disabled
// config when the test is off or no stencil attachment is bound).
// Face state is front/back exactly like GL (Metal has front/back stencil
// descriptors); ref values are clamped to [0, 255] for the encoder.
struct StencilConfig {
  bool enabled = false;
  GLenum func_front = kGlAlways;
  GLenum func_back = kGlAlways;
  GLint ref_front = 0;
  GLint ref_back = 0;
  GLuint value_mask_front = 0xFFFFFFFFu;
  GLuint value_mask_back = 0xFFFFFFFFu;
  GLuint write_mask_front = 0xFFFFFFFFu;
  GLuint write_mask_back = 0xFFFFFFFFu;
  GLenum sfail_front = 0x1E00;  // KEEP.
  GLenum sfail_back = 0x1E00;
  GLenum dpfail_front = 0x1E00;
  GLenum dpfail_back = 0x1E00;
  GLenum dppass_front = 0x1E00;
  GLenum dppass_back = 0x1E00;
  GLint clear_stencil = 0;
};

class MetalBridge {
 public:
  virtual ~MetalBridge() = default;

  virtual bool Initialize(const char* family) = 0;
  virtual bool IsInitialized() const = 0;

  // Execution: PSO + resources. 0 == failure (with GetError set).
  virtual std::uint32_t CreateRenderPipeline(const backend::PsoKey& key) = 0;
  // Selects a created pipeline for the next render pass (type-1 path: the
  // facade binds the PsoKey it compiled from live GL state). Unknown handles
  // fail closed (INVALID_OPERATION); the Apple bridge resolves the handle to
  // its MTLRenderPipelineState immediately, so BeginRenderPass needs no map.
  virtual void BindRenderPipeline(std::uint32_t handle) = 0;
  virtual std::uint32_t CreateBuffer(GLsizeiptr size,
                                     backend::StorageMode mode) = 0;
  virtual std::uint32_t CreateTexture(GLsizei width, GLsizei height,
                                      GLenum internalformat,
                                      const char* family) = 0;

  // Frame + encoder ordering (mirrors backend::CommandPlan).
  // BeginFrame starts a frame and sizes the offscreen target. When a frame
  // is already open at the SAME size (multi-draw before eglSwapBuffers),
  // BeginFrame is not called again — the facade reuses the open frame and
  // opens subsequent passes with BeginRenderPassNoClear (first pass of a
  // fresh frame clears with SetClearColor / glClearColor).
  virtual bool BeginFrame(GLsizei width, GLsizei height) = 0;
  virtual bool FrameOpen() const = 0;
  virtual GLsizei TargetWidth() const = 0;
  virtual GLsizei TargetHeight() const = 0;
  virtual void BeginRenderPass() = 0;
  // Draws the uploaded vertex stream with the given GL topology. Executable:
  // POINTS, LINES, LINE_STRIP, TRIANGLES, TRIANGLE_STRIP (LINE_LOOP and
  // TRIANGLE_FAN are CPU-expanded by the facade before reaching here).
  // Anything else (adjacency, PATCHES, unknown) fails closed.
  virtual void Draw(GLenum mode) = 0;
  virtual void EndRenderPass() = 0;
  virtual void Blit() = 0;
  virtual bool CommitFrame() = 0;
  virtual bool Committed() const = 0;
  virtual GLuint DrawCount() const = 0;
  // Clear color for the next BeginRenderPass (spec 17.3 glClearColor).
  // Applied only when the pass clears (first pass of a frame); subsequent
  // passes in the same frame use MTLLoadActionLoad and ignore this.
  virtual void SetClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) = 0;
  // What the NEXT BeginRenderPass clears. Both default to true (a bare
  // bridge clears color+depth like the first GL pass of a frame). The facade
  // turns COLOR off for a frame that only needs its depth buffer reset: the
  // app's own per-frame clear lands on a different FBO and is deliberately
  // not a window clear, so without this the depth buffer keeps last frame's
  // values and every depth-tested draw fails silently.
  virtual void SetClearAttachments(bool color, bool depth) = 0;

  // Vertex input for Draw (production path): interleaved float4
  // position+color pairs matching the MSL VertexIn layout
  // (attribute(0) position, attribute(1) color, stride bytes).
  // Textured draws use pos(float4)+col(float4)+uv(float2), stride 40,
  // matching VertexInTex (attribute(2) uv). Bindings persist across frames
  // (GL VAO/uniform semantics); the Apple bridge fails Draw closed without
  // vertex data, while the Mock validates encoder ordering only and draws
  // without vertex uploads.
  virtual void SetVertexBytes(const void* data, std::size_t bytes,
                              std::uint32_t stride) = 0;
  virtual void SetMVP(const float mvp[16]) = 0;

  // Fragment texture sampling (ES 3.2 §8, honest v1): RGBA8-unpacked bytes
  // for texture unit `unit` + sampler state for the next Draw. The facade
  // uploads the sampler uniform's bound TEXTURE_2D level 0 (views resolve
  // via shared storage, so sampling through a view sees the same bytes).
  // v1 scope: 2D RGBA8 level 0, NEAREST/LINEAR + CLAMP/REPEAT; other targets
  // fail closed at facade selection time (untextured fallback documented).
  // Bindings persist across frames like vertex/uniform state.
  virtual void SetFragmentTexture(GLuint unit, GLsizei width, GLsizei height,
                                  const void* rgba8) = 0;
  virtual void SetFragmentSampler(GLuint unit, bool linear,
                                  bool repeat) = 0;
  virtual bool HasFragmentTexture(GLuint unit) const = 0;

  // Extended sampling uploads, keyed by GL texture unit (like above):
  // cube (6 face pointers, size*size*4 each), 3D / 2D-array (contiguous),
  // mipmapped 2D (level pointers, levels>=1). srgb selects the hardware
  // sRGB format (SRGB8_ALPHA8 decode). Sampler detail carries full GL enums
  // (incl. mipmap filters and mirrored repeat; BORDER fails at facade
  // selection — Metal has no border color). SetSamplerSlots maps MSL
  // slot i <- GL units[i] (translator emits slots in sampler order); until
  // called, slot 0 reads the last-uploaded unit (legacy behavior).
  virtual void SetFragmentTextureCube(GLuint unit, GLsizei size,
                                      const void* const* faces_rgba8,
                                      bool srgb) = 0;
  virtual void SetFragmentTexture3D(GLuint unit, GLsizei width, GLsizei height,
                                    GLsizei depth, const void* rgba8,
                                    bool srgb) = 0;
  virtual void SetFragmentTextureArray(GLuint unit, GLsizei width,
                                       GLsizei height, GLsizei layers,
                                       const void* rgba8, bool srgb) = 0;
  virtual void SetFragmentTextureMips(GLuint unit, GLsizei base_width,
                                      GLsizei base_height, int levels,
                                      const void* const* level_rgba8,
                                      bool srgb) = 0;
  // Cube/3D/array mipmap chains (Phase 4 item 6): level pointers per face /
  // per level (3D/array: contiguous per-level bytes, depth halved). State
  // already generates the chain (GenerateMipmap box filter); the facade
  // uploads it here so min_mipmap filters sample complete chains.
  virtual void SetFragmentTextureCubeMips(GLuint unit, GLsizei base_size,
                                          int levels,
                                          const void* const* faces_per_level,
                                          bool srgb) = 0;
  virtual void SetFragmentTexture3DMips(GLuint unit, GLsizei base_width,
                                        GLsizei base_height, GLsizei base_depth,
                                        int levels,
                                        const void* const* level_rgba8,
                                        bool srgb) = 0;
  virtual void SetFragmentTextureArrayMips(GLuint unit, GLsizei base_width,
                                           GLsizei base_height,
                                           GLsizei layers, int levels,
                                           const void* const* level_rgba8,
                                           bool srgb) = 0;
  virtual void SetFragmentSamplerDetail(GLuint unit, GLenum min_filter,
                                        GLenum mag_filter, GLenum wrap_s,
                                        GLenum wrap_t) = 0;
  // LOD clamps (Phase 4 item 10): MIN/MAX_LOD -> lodMin/MaxClamp. Must follow
  // SetFragmentSamplerDetail for the same unit (the bridge folds them into
  // the MTLSamplerDescriptor). Out-of-range (min>max) fails closed.
  virtual void SetSamplerLod(GLuint unit, float min_lod, float max_lod) = 0;
  virtual void SetSamplerSlots(const GLuint* gl_units,
                               std::size_t count) = 0;
  // Vertex-stage texture slot map (VTF / sample_lightmap): MSL vertex
  // [[texture(i)]]/[[sampler(i)]] reads GL unit vertex_slots[i]. Independent
  // of the fragment map (separate Metal index spaces). Empty clears the map.
  virtual void SetVertexSamplerSlots(const GLuint* gl_units,
                                     std::size_t count) = 0;

  // MRT: attachment count for the next frame (1..8, default 1). Must precede
  // BeginFrame (targets are sized there). Out-of-range fails closed.
  virtual void SetMrtCount(GLsizei n) = 0;
  // Per-attachment readback (device-only like ReadbackPixel; Mock fails
  // closed without a rasterizer).
  virtual bool ReadbackAttachment(GLuint index, GLint x, GLint y,
                                  std::uint8_t out_rgba[4]) = 0;
  // Second/subsequent pass of a frame without clearing (two-pass split
  // stencil emulation): color/depth/stencil all Load. Requires an open frame
  // with the render pass closed, like Blit.
  virtual void BeginRenderPassNoClear() = 0;
  // UBO block upload (GLSL->MSL v2): constant buffer (2+block_index) from
  // the bound UNIFORM_BUFFER range bytes (std140 layout matches the MSL
  // struct for mat4/vec4/float members). Persists like uniforms.
  virtual void SetUboBytes(GLuint block_index, const void* data,
                           std::size_t bytes) = 0;
  // Rasterizer state per frame (spec 13-14 + polygon offset): cull mode +
  // winding, viewport/scissor rects, depth bias. Persists like other state.
  virtual void ConfigureCull(const CullConfig& config) = 0;
  virtual void ConfigureViewport(const ViewportConfig& config) = 0;

  // Blend color for CONSTANT_* factors (spec 14.1): uploaded per frame from
  // RasterState, bound via setBlendColor on the render encoder. Persists
  // like MVP/uniform state. Default (0,0,0,0) matches a fresh GL context.
  virtual void SetBlendColor(float r, float g, float b, float a) = 0;

  // Uniform block for translated programs (GLSL->MSL v1): back-to-back
  // 16-byte fields in TranslatedProgram::uniforms order (mat4 = 16 floats,
  // vec4 = 4), bound at constant buffer 0 for BOTH stages. Persists like
  // MVP. The legacy path keeps using SetMVP (MVP-only, vertex stage).
  virtual void SetUniformBytes(const void* data, std::size_t bytes) = 0;

  // Translated graphics pipeline: compiles TranslatedProgram::library_source
  // (cached by content) and builds a PSO for `key` (blend/depth/stencil/
  // textured-layout from the key exactly like CreateRenderPipeline).
  // `key.translated` must be true and `key.program_id` the GL program, so
  // per-program MSL never aliases. 0 == failure (GetError set).
  virtual std::uint32_t CreateTranslatedPipeline(
      const glsl::TranslatedProgram& prog, const backend::PsoKey& key) = 0;

  // Compute execution (ES 3.2 ch.19, honest v1): increments every uint32 in
  // a CPU-side store via a real GPU kernel. The facade owns SSBO-store
  // semantics (marker-gated); this is the device proof that the dispatch
  // reaches a MTLComputeCommandEncoder and comes back bit-exact. Mock does
  // the same increment on the CPU so host tests stay deterministic.
  virtual bool ComputeIncrementUint32(const std::uint32_t* input,
                                      std::uint32_t* output,
                                      std::size_t count) = 0;

  // Depth target for the next frame. Must precede BeginFrame so the bridge
  // can size the depth texture with the color target and bake the matching
  // pipeline state. An out-of-range func fails closed (INVALID_ENUM).
  virtual void ConfigureDepth(const DepthConfig& config) = 0;
  virtual bool DepthEnabled() const = 0;
  // Stencil target for the next frame (same ordering contract as depth).
  // Out-of-range funcs fail closed (INVALID_ENUM); unknown ops fail closed
  // (INVALID_OPERATION, they cannot compile to MTLStencilOperation).
  virtual void ConfigureStencil(const StencilConfig& config) = 0;
  virtual bool StencilEnabled() const = 0;

  // Pixel readback of the current frame target (device-only; the Mock
  // has no rasterizer and fails these closed with INVALID_OPERATION).
  virtual bool ReadbackPixel(GLint x, GLint y, std::uint8_t out_rgba[4]) = 0;
  // Diagnostics: read one texel of the texture uploaded for GL unit `unit`
  // (Metal stores BGRA; converted to RGBA here). Separates "the app gave us
  // transparent bytes" from "the upload never reached the GPU" — the two
  // look identical on screen (a draw that paints nothing).
  virtual bool TextureProbe(GLuint unit, GLint x, GLint y,
                            std::uint8_t out_rgba[4]) = 0;
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
  void BindRenderPipeline(std::uint32_t handle) override;
  std::uint32_t CreateBuffer(GLsizeiptr size,
                             backend::StorageMode mode) override;
  std::uint32_t CreateTexture(GLsizei width, GLsizei height,
                              GLenum internalformat,
                              const char* family) override;
  bool BeginFrame(GLsizei width, GLsizei height) override;
  bool FrameOpen() const override;
  GLsizei TargetWidth() const override;
  GLsizei TargetHeight() const override;
  void BeginRenderPass() override;
  void Draw(GLenum mode) override;
  void EndRenderPass() override;
  void Blit() override;
  bool CommitFrame() override;
  bool Committed() const override;
  GLuint DrawCount() const override;
  void SetClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) override;
  void SetClearAttachments(bool color, bool depth) override;
  void SetVertexBytes(const void* data, std::size_t bytes,
                       std::uint32_t stride) override;
  void SetMVP(const float mvp[16]) override;
  void SetFragmentTexture(GLuint unit, GLsizei width, GLsizei height,
                          const void* rgba8) override;
  void SetFragmentSampler(GLuint unit, bool linear, bool repeat) override;
  bool HasFragmentTexture(GLuint unit) const override;
  bool ComputeIncrementUint32(const std::uint32_t* input,
                              std::uint32_t* output,
                              std::size_t count) override;
  void SetBlendColor(float r, float g, float b, float a) override;
  void SetUniformBytes(const void* data, std::size_t bytes) override;
  std::uint32_t CreateTranslatedPipeline(
      const glsl::TranslatedProgram& prog,
      const backend::PsoKey& key) override;
  void SetFragmentTextureCube(GLuint unit, GLsizei size,
                              const void* const* faces_rgba8,
                              bool srgb) override;
  void SetFragmentTexture3D(GLuint unit, GLsizei width, GLsizei height,
                            GLsizei depth, const void* rgba8,
                            bool srgb) override;
  void SetFragmentTextureArray(GLuint unit, GLsizei width, GLsizei height,
                               GLsizei layers, const void* rgba8,
                               bool srgb) override;
  void SetFragmentTextureMips(GLuint unit, GLsizei base_width,
                               GLsizei base_height, int levels,
                               const void* const* level_rgba8,
                               bool srgb) override;
  void SetFragmentTextureCubeMips(GLuint unit, GLsizei base_size, int levels,
                                  const void* const* faces_per_level,
                                  bool srgb) override;
  void SetFragmentTexture3DMips(GLuint unit, GLsizei base_width,
                                GLsizei base_height, GLsizei base_depth,
                                int levels, const void* const* level_rgba8,
                                bool srgb) override;
  void SetFragmentTextureArrayMips(GLuint unit, GLsizei base_width,
                                   GLsizei base_height, GLsizei layers,
                                   int levels, const void* const* level_rgba8,
                                   bool srgb) override;
  void SetFragmentSamplerDetail(GLuint unit, GLenum min_filter,
                                 GLenum mag_filter, GLenum wrap_s,
                                 GLenum wrap_t) override;
  void SetSamplerLod(GLuint unit, float min_lod, float max_lod) override;
  void SetSamplerSlots(const GLuint* gl_units, std::size_t count) override;
  void SetVertexSamplerSlots(const GLuint* gl_units,
                             std::size_t count) override;
  void SetMrtCount(GLsizei n) override;
  bool ReadbackAttachment(GLuint index, GLint x, GLint y,
                           std::uint8_t out_rgba[4]) override;
  void BeginRenderPassNoClear() override;
  void SetUboBytes(GLuint block_index, const void* data,
                   std::size_t bytes) override;
  void ConfigureCull(const CullConfig& config) override;
  void ConfigureViewport(const ViewportConfig& config) override;
  void ConfigureDepth(const DepthConfig& config) override;
  bool DepthEnabled() const override;
  void ConfigureStencil(const StencilConfig& config) override;
  bool StencilEnabled() const override;
  bool ReadbackPixel(GLint x, GLint y, std::uint8_t out_rgba[4]) override;
  bool BlitToCpu(void* dst, std::size_t bytes) override;
  bool TextureProbe(GLuint unit, GLint x, GLint y,
                    std::uint8_t out_rgba[4]) override;
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
  GLsizei layer_width_ = 0;
  GLsizei layer_height_ = 0;
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
  void BindRenderPipeline(std::uint32_t handle) override;
  std::uint32_t BoundPipeline() const;
  std::size_t PipelineCount() const;
  std::size_t TranslatedCount() const { return translated_count_; }
  std::size_t UniformBytesSize() const { return uniform_bytes_.size(); }
  std::string LastVertexFn() const { return last_vertex_fn_; }
  std::string LastFragmentFn() const { return last_fragment_fn_; }
  GLsizei MrtCount() const { return mrt_count_; }
  std::size_t UboBytesSize(GLuint block) const {
    auto it = ubo_bytes_.find(block);
    return it == ubo_bytes_.end() ? 0 : it->second.size();
  }
  int SlotKind(GLuint unit) const {
    auto it = frag_slots_.find(unit);
    return it == frag_slots_.end() ? -1 : it->second.kind;
  }
  std::size_t SamplerSlotCount() const { return sampler_slots_.size(); }
  std::size_t VertexSamplerSlotCount() const {
    return vertex_sampler_slots_.size();
  }
  std::uint32_t CreateBuffer(GLsizeiptr size,
                             backend::StorageMode mode) override;
  bool IsBufferShared(std::uint32_t handle) const;
  std::uint32_t CreateTexture(GLsizei width, GLsizei height,
                              GLenum internalformat,
                              const char* family) override;
  bool BeginFrame(GLsizei width, GLsizei height) override;
  bool FrameOpen() const override;
  GLsizei TargetWidth() const override;
  GLsizei TargetHeight() const override;
  void BeginRenderPass() override;
  void Draw(GLenum mode) override;
  GLenum LastPrimitiveMode() const;
  void EndRenderPass() override;
  void Blit() override;
  bool CommitFrame() override;
  bool Committed() const override;
  GLuint DrawCount() const override;
  void SetClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) override;
  void SetClearAttachments(bool color, bool depth) override;
  void SetVertexBytes(const void* data, std::size_t bytes,
                       std::uint32_t stride) override;
  void SetMVP(const float mvp[16]) override;
  void SetFragmentTexture(GLuint unit, GLsizei width, GLsizei height,
                          const void* rgba8) override;
  void SetFragmentSampler(GLuint unit, bool linear, bool repeat) override;
  bool HasFragmentTexture(GLuint unit) const override;
  bool ComputeIncrementUint32(const std::uint32_t* input,
                              std::uint32_t* output,
                              std::size_t count) override;
  void SetBlendColor(float r, float g, float b, float a) override;
  void SetUniformBytes(const void* data, std::size_t bytes) override;
  std::uint32_t CreateTranslatedPipeline(
      const glsl::TranslatedProgram& prog,
      const backend::PsoKey& key) override;
  void SetFragmentTextureCube(GLuint unit, GLsizei size,
                              const void* const* faces_rgba8,
                              bool srgb) override;
  void SetFragmentTexture3D(GLuint unit, GLsizei width, GLsizei height,
                            GLsizei depth, const void* rgba8,
                            bool srgb) override;
  void SetFragmentTextureArray(GLuint unit, GLsizei width, GLsizei height,
                               GLsizei layers, const void* rgba8,
                               bool srgb) override;
  void SetFragmentTextureMips(GLuint unit, GLsizei base_width,
                               GLsizei base_height, int levels,
                               const void* const* level_rgba8,
                               bool srgb) override;
  void SetFragmentTextureCubeMips(GLuint unit, GLsizei base_size, int levels,
                                  const void* const* faces_per_level,
                                  bool srgb) override;
  void SetFragmentTexture3DMips(GLuint unit, GLsizei base_width,
                                GLsizei base_height, GLsizei base_depth,
                                int levels, const void* const* level_rgba8,
                                bool srgb) override;
  void SetFragmentTextureArrayMips(GLuint unit, GLsizei base_width,
                                   GLsizei base_height, GLsizei layers,
                                   int levels, const void* const* level_rgba8,
                                   bool srgb) override;
  void SetFragmentSamplerDetail(GLuint unit, GLenum min_filter,
                                GLenum mag_filter, GLenum wrap_s,
                                GLenum wrap_t) override;
  void SetSamplerLod(GLuint unit, float min_lod, float max_lod) override;
  float SamplerMinLod(GLuint unit) const;
  float SamplerMaxLod(GLuint unit) const;
  void SetSamplerSlots(const GLuint* gl_units, std::size_t count) override;
  void SetVertexSamplerSlots(const GLuint* gl_units,
                             std::size_t count) override;
  void SetMrtCount(GLsizei n) override;
  bool ReadbackAttachment(GLuint index, GLint x, GLint y,
                           std::uint8_t out_rgba[4]) override;
  void BeginRenderPassNoClear() override;
  void SetUboBytes(GLuint block_index, const void* data,
                   std::size_t bytes) override;
  void ConfigureCull(const CullConfig& config) override;
  void ConfigureViewport(const ViewportConfig& config) override;
  CullConfig LastCullConfig() const { return cull_cfg_; }
  // Pass-kind counters: the facade decides clear-vs-load per frame, and that
  // decision (not pixels) is what the window-source clear tests assert.
  std::size_t PassClearCount() const { return pass_clear_count_; }
  std::size_t PassLoadCount() const { return pass_load_count_; }
  // Depth-only clear passes (color Loads): the facade's frame-start depth
  // reset when the app's own clear is not a window clear.
  std::size_t PassDepthClearCount() const { return pass_depth_clear_count_; }
  // Color of the most recent SetClearColor: which color a forced clear pass
  // used is what the "repaint with the last defined color" test asserts.
  const GLfloat* LastClearColor() const { return clear_color_; }
  ViewportConfig LastViewportConfig() const { return viewport_cfg_; }
  void ConfigureDepth(const DepthConfig& config) override;
  bool DepthEnabled() const override;
  void ConfigureStencil(const StencilConfig& config) override;
  bool StencilEnabled() const override;
  StencilConfig LastStencilConfig() const;
  DepthConfig LastDepthConfig() const;
  bool ReadbackPixel(GLint x, GLint y, std::uint8_t out_rgba[4]) override;
  bool BlitToCpu(void* dst, std::size_t bytes) override;
  bool TextureProbe(GLuint unit, GLint x, GLint y,
                    std::uint8_t out_rgba[4]) override;
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
  std::uint32_t bound_pipeline_ = 0;
  std::map<std::uint32_t, backend::StorageMode> buffers_;
  bool frame_open_ = false;
  bool render_open_ = false;
  bool committed_ = false;
  std::size_t pass_clear_count_ = 0;
  std::size_t pass_load_count_ = 0;
  std::size_t pass_depth_clear_count_ = 0;
  bool clear_color_flag_ = true;
  bool clear_depth_flag_ = true;
  GLuint draws_ = 0;
  GLenum last_mode_ = 0;
  void* layer_ = nullptr;
  GLsizei layer_width_ = 0;
  GLsizei layer_height_ = 0;
  bool has_vertex_ = false;
  bool has_mvp_ = false;
  float blend_color_[4] = {0, 0, 0, 0};
  std::vector<std::uint8_t> uniform_bytes_;
  std::map<std::uint32_t, bool> handle_needs_texture_;
  std::size_t translated_count_ = 0;
  std::string last_vertex_fn_;
  std::string last_fragment_fn_;
  std::uint32_t last_stride_ = 0;
  std::map<GLuint, std::vector<std::uint8_t>> frag_textures_;
  std::map<GLuint, std::pair<GLsizei, GLsizei>> frag_tex_size_;
  std::map<GLuint, std::pair<bool, bool>> frag_samplers_;  // linear, repeat.
  struct FragSlot {
    int kind = 0;  // 0=2D, 1=cube, 2=3D, 3=array, 4=mips, 5=cube-mips,
                   // 6=3D-mips, 7=array-mips.
    GLsizei w = 0, h = 0, d = 0;
    bool srgb = false;
    int levels = 1;
  };
  std::map<GLuint, FragSlot> frag_slots_;  // Keyed by GL unit.
  struct SamplerDetail {
    GLenum min_filter = 0, mag_filter = 0, wrap_s = 0, wrap_t = 0;
  };
  std::map<GLuint, SamplerDetail> frag_sampler_detail_;
  std::map<GLuint, std::pair<float, float>> sampler_lod_;  // min/max clamp.
  std::vector<GLuint> sampler_slots_;  // MSL slot -> GL unit.
  std::vector<GLuint> vertex_sampler_slots_;  // MSL VS slot -> GL unit.
  GLsizei mrt_count_ = 1;
  std::map<GLuint, std::vector<std::uint8_t>> ubo_bytes_;
  CullConfig cull_cfg_;
  ViewportConfig viewport_cfg_;
  DepthConfig depth_cfg_;
  StencilConfig stencil_cfg_;
  GLfloat clear_color_[4] = {0.f, 0.f, 0.f, 1.f};
  GLsizei target_width_ = 0;
  GLsizei target_height_ = 0;
  std::uint64_t frame_serial_ = 0;
  std::uint64_t completed_serial_ = 0;
  std::uint64_t swaps_ = 0;
};

}  // namespace metal_bridge
}  // namespace tgles

#endif  // TGLES_METAL_BRIDGE_H
