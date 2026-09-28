#include "tgles/gpu/metal_bridge.h"

#include "tgles/state/buffer.h"  // Usage hints (kept for future storage checks).
#include "tgles/gpu/metal_mapping.h"
#include "tgles/pipeline/raster.h"  // Cull-mode constants for validation.

namespace tgles {
namespace metal_bridge {

bool NullMetalBridge::Initialize(const char* /*family*/) { return false; }
bool NullMetalBridge::IsInitialized() const { return false; }
std::uint32_t NullMetalBridge::CreateRenderPipeline(
    const backend::PsoKey& /*key*/) {
  return 0;
}

void NullMetalBridge::BindRenderPipeline(std::uint32_t /*handle*/) {
  errors_.Record(kGlInvalidOperation);
}
std::uint32_t NullMetalBridge::CreateBuffer(GLsizeiptr /*size*/,
                                            backend::StorageMode /*mode*/) {
  return 0;
}
std::uint32_t NullMetalBridge::CreateTexture(GLsizei /*width*/,
                                             GLsizei /*height*/,
                                             GLenum /*internalformat*/,
                                             const char* /*family*/) {
  return 0;
}
bool NullMetalBridge::BeginFrame(GLsizei /*width*/, GLsizei /*height*/) {
  errors_.Record(kGlInvalidOperation);
  return false;
}
bool NullMetalBridge::FrameOpen() const { return false; }
GLsizei NullMetalBridge::TargetWidth() const { return layer_width_; }
GLsizei NullMetalBridge::TargetHeight() const { return layer_height_; }
void NullMetalBridge::BeginRenderPass() {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::Draw(GLenum /*mode*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::EndRenderPass() {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::Blit() { errors_.Record(kGlInvalidOperation); }
bool NullMetalBridge::CommitFrame() {
  errors_.Record(kGlInvalidOperation);
  return false;
}
bool NullMetalBridge::Committed() const { return false; }
GLuint NullMetalBridge::DrawCount() const { return 0; }
void NullMetalBridge::SetClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetClearAttachments(bool /*color*/, bool /*depth*/) {}
void NullMetalBridge::SetVertexBytes(const void* /*data*/, std::size_t /*bytes*/,
                                     std::uint32_t /*stride*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetMVP(const float /*mvp*/[16]) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTexture(GLuint /*unit*/, GLsizei /*w*/,
                                          GLsizei /*h*/,
                                          const void* /*rgba8*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentSampler(GLuint /*unit*/, bool /*linear*/,
                                          bool /*repeat*/) {
  errors_.Record(kGlInvalidOperation);
}
bool NullMetalBridge::HasFragmentTexture(GLuint /*unit*/) const {
  return false;
}
bool NullMetalBridge::ComputeIncrementUint32(const std::uint32_t* input,
                                             std::uint32_t* output,
                                             std::size_t count) {
  if (input == nullptr || output == nullptr) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  (void)count;
  errors_.Record(kGlInvalidOperation);
  return false;
}
void NullMetalBridge::SetBlendColor(float /*r*/, float /*g*/, float /*b*/,
                                     float /*a*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetUniformBytes(const void* /*data*/,
                                       std::size_t /*bytes*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTextureCube(GLuint /*slot*/, GLsizei /*size*/,
                                              const void* const* /*faces*/,
                                              bool /*srgb*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTexture3D(GLuint /*slot*/, GLsizei /*w*/,
                                            GLsizei /*h*/, GLsizei /*d*/,
                                            const void* /*data*/,
                                            bool /*srgb*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTextureArray(
    GLuint /*slot*/, GLsizei /*w*/, GLsizei /*h*/, GLsizei /*layers*/,
    const void* /*data*/, bool /*srgb*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTextureMips(GLuint /*slot*/, GLsizei /*w*/,
                                              GLsizei /*h*/, int /*levels*/,
                                              const void* const* /*data*/,
                                              bool /*srgb*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTextureCubeMips(
    GLuint /*unit*/, GLsizei /*size*/, int /*levels*/,
    const void* const* /*faces*/, bool /*srgb*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTexture3DMips(
    GLuint /*unit*/, GLsizei /*w*/, GLsizei /*h*/, GLsizei /*d*/,
    int /*levels*/, const void* const* /*data*/, bool /*srgb*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentTextureArrayMips(
    GLuint /*unit*/, GLsizei /*w*/, GLsizei /*h*/, GLsizei /*layers*/,
    int /*levels*/, const void* const* /*data*/, bool /*srgb*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetFragmentSamplerDetail(GLuint /*slot*/,
                                                GLenum /*min*/,
                                                GLenum /*mag*/,
                                                GLenum /*wraps*/,
                                                GLenum /*wrapt*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetSamplerLod(GLuint /*unit*/, float /*min*/,
                                     float /*max*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetSamplerSlots(const GLuint* /*units*/,
                                       std::size_t /*count*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetVertexSamplerSlots(const GLuint* /*units*/,
                                             std::size_t /*count*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetMrtCount(GLsizei n) {
  if (n < 1 || n > 8) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  errors_.Record(kGlInvalidOperation);
}
bool NullMetalBridge::ReadbackAttachment(GLuint /*index*/, GLint /*x*/,
                                         GLint /*y*/,
                                         std::uint8_t /*out*/[4]) {
  errors_.Record(kGlInvalidOperation);
  return false;
}
void NullMetalBridge::BeginRenderPassNoClear() {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetUboBytes(GLuint /*block*/, const void* /*data*/,
                                   std::size_t /*bytes*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::ConfigureCull(const CullConfig& /*config*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::ConfigureViewport(const ViewportConfig& /*config*/) {
  errors_.Record(kGlInvalidOperation);
}
std::uint32_t NullMetalBridge::CreateTranslatedPipeline(
    const glsl::TranslatedProgram& /*prog*/, const backend::PsoKey& /*key*/) {
  errors_.Record(kGlInvalidOperation);
  return 0;
}
void NullMetalBridge::ConfigureDepth(const DepthConfig& /*config*/) {
  errors_.Record(kGlInvalidOperation);
}
bool NullMetalBridge::DepthEnabled() const { return false; }
void NullMetalBridge::ConfigureStencil(const StencilConfig& /*config*/) {
  errors_.Record(kGlInvalidOperation);
}
bool NullMetalBridge::StencilEnabled() const { return false; }
bool NullMetalBridge::ReadbackPixel(GLint /*x*/, GLint /*y*/,
                                    std::uint8_t /*out_rgba*/[4]) {
  errors_.Record(kGlInvalidOperation);
  return false;
}
bool NullMetalBridge::BlitToCpu(void* /*dst*/, std::size_t /*bytes*/) {
  errors_.Record(kGlInvalidOperation);
  return false;
}
bool NullMetalBridge::WaitForCompletion(std::uint64_t /*serial*/) {
  errors_.Record(kGlInvalidOperation);
  return false;
}
void NullMetalBridge::SetLayer(void* /*ca_metal_layer*/, GLsizei /*width*/,
                               GLsizei /*height*/) {}
void NullMetalBridge::Resize(GLsizei /*width*/, GLsizei /*height*/) {}
bool NullMetalBridge::Present() {
  errors_.Record(kGlInvalidOperation);
  return false;
}
std::uint64_t NullMetalBridge::FrameSerial() const { return 0; }
std::uint64_t NullMetalBridge::CompletedSerial() const { return 0; }
std::uint64_t NullMetalBridge::SwapCount() const { return 0; }
GLenum NullMetalBridge::GetError() { return errors_.Get(); }

MockMetalBridge::MockMetalBridge() = default;

bool MockMetalBridge::Initialize(const char* family) {
  if (family == nullptr || metal::FindGpuFamily(family) == nullptr) {
    errors_.Record(kGlInvalidEnum);
    return false;
  }
  initialized_ = true;
  family_ = family;
  return true;
}

bool MockMetalBridge::IsInitialized() const { return initialized_; }

std::string MockMetalBridge::DeviceFamily() const { return family_; }

std::uint32_t MockMetalBridge::CreateRenderPipeline(
    const backend::PsoKey& key) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  auto it = pipelines_.find(key);
  if (it != pipelines_.end()) return it->second;
  const std::uint32_t handle = next_handle_++;
  pipelines_[key] = handle;
  return handle;
}

void MockMetalBridge::BindRenderPipeline(std::uint32_t handle) {
  if (!initialized_ || handle == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  bool known = false;
  for (const auto& kv : pipelines_) {
    if (kv.second == handle) {
      known = true;
      break;
    }
  }
  if (!known) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  bound_pipeline_ = handle;
}

std::uint32_t MockMetalBridge::BoundPipeline() const { return bound_pipeline_; }

std::size_t MockMetalBridge::PipelineCount() const {
  return pipelines_.size();
}

std::uint32_t MockMetalBridge::CreateBuffer(GLsizeiptr size,
                                            backend::StorageMode mode) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  if (size <= 0) {
    errors_.Record(kGlInvalidValue);
    return 0;
  }
  const std::uint32_t handle = next_handle_++;
  buffers_[handle] = mode;
  return handle;
}

bool MockMetalBridge::IsBufferShared(std::uint32_t handle) const {
  auto it = buffers_.find(handle);
  if (it == buffers_.end()) return false;
  return it->second == backend::StorageMode::kShared;
}

std::uint32_t MockMetalBridge::CreateTexture(GLsizei width, GLsizei height,
                                             GLenum internalformat,
                                             const char* family) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  if (width <= 0 || height <= 0) {
    errors_.Record(kGlInvalidValue);
    return 0;
  }
  if (!backend::ResourcePlan::IsFormatSupported(internalformat, family)) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  return next_handle_++;
}

bool MockMetalBridge::BeginFrame(GLsizei width, GLsizei height) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  if (width <= 0 || height <= 0) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  if (frame_open_) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  // Multi-draw reopen at the same size (mirror Apple bridge).
  if (committed_ && target_width_ == width && target_height_ == height) {
    committed_ = false;
    frame_open_ = true;
    render_open_ = false;
    return true;
  }
  frame_open_ = true;
  render_open_ = false;
  committed_ = false;
  draws_ = 0;
  target_width_ = width;
  target_height_ = height;
  // NOTE: vertex/MVP bindings persist across frames (GL VAO/uniform
  // semantics), mirroring the Apple bridge.
  return true;
}

bool MockMetalBridge::FrameOpen() const { return frame_open_; }
GLsizei MockMetalBridge::TargetWidth() const { return target_width_; }
GLsizei MockMetalBridge::TargetHeight() const { return target_height_; }
void MockMetalBridge::SetClearColor(GLfloat r, GLfloat g, GLfloat b,
                                    GLfloat a) {
  clear_color_[0] = r;
  clear_color_[1] = g;
  clear_color_[2] = b;
  clear_color_[3] = a;
}

void MockMetalBridge::SetClearAttachments(bool color, bool depth) {
  clear_color_flag_ = color;
  clear_depth_flag_ = depth;
}

void MockMetalBridge::BeginRenderPass() {
  if (!frame_open_ || render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  render_open_ = true;
  // Color-less clears (the facade's frame-start depth reset) are counted
  // apart from real color clears: window-source tests assert PassClearCount
  // means "the color buffer was wiped".
  if (!clear_color_flag_ && clear_depth_flag_) {
    ++pass_depth_clear_count_;
    return;
  }
  ++pass_clear_count_;
}

void MockMetalBridge::Draw(GLenum mode) {
  if (!frame_open_ || !render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  switch (mode) {
    case kGlPoints:
    case kGlLines:
    case kGlLineStrip:
    case kGlTriangles:
    case kGlTriangleStrip:
      break;
    default:
      // LINE_LOOP/TRIANGLE_FAN never reach here (facade CPU-expands them);
      // adjacency (0x000A-0x000D) / PATCHES (0x000E) have no bridge
      // execution; anything else is an enum error.
      errors_.Record(mode == kGlLineLoop || mode == kGlTriangleFan ||
                             (mode >= 0x000Au && mode <= 0x000Eu)
                         ? kGlInvalidOperation
                         : kGlInvalidEnum);
      return;
  }
  last_mode_ = mode;
  ++draws_;
}

GLenum MockMetalBridge::LastPrimitiveMode() const { return last_mode_; }

void MockMetalBridge::EndRenderPass() {
  if (!frame_open_ || !render_open_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  render_open_ = false;
}

void MockMetalBridge::Blit() {
  if (!frame_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (render_open_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  // Outside a pass: legal blit-encoder slot, no state change.
}

bool MockMetalBridge::CommitFrame() {
  if (!frame_open_ || render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  committed_ = true;
  frame_open_ = false;
  return true;
}

bool MockMetalBridge::Committed() const { return committed_; }
GLuint MockMetalBridge::DrawCount() const { return draws_; }

void MockMetalBridge::SetVertexBytes(const void* data, std::size_t bytes,
                                     std::uint32_t stride) {
  if (data == nullptr || bytes == 0 || stride == 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  has_vertex_ = true;
  last_stride_ = stride;
}

void MockMetalBridge::SetMVP(const float mvp[16]) {
  if (mvp == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  has_mvp_ = true;
}

void MockMetalBridge::SetFragmentTexture(GLuint unit, GLsizei width,
                                          GLsizei height, const void* rgba8) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (rgba8 == nullptr || width <= 0 || height <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const std::size_t need =
      static_cast<std::size_t>(width) * height * 4;
  const auto* src = static_cast<const std::uint8_t*>(rgba8);
  frag_textures_[unit] = std::vector<std::uint8_t>(src, src + need);
  frag_tex_size_[unit] = {width, height};
  FragSlot s;
  s.kind = 0;
  s.w = width;
  s.h = height;
  s.d = 1;
  frag_slots_[unit] = s;
}

void MockMetalBridge::SetFragmentSampler(GLuint unit, bool linear,
                                          bool repeat) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  frag_samplers_[unit] = {linear, repeat};
}
void MockMetalBridge::SetFragmentTextureCube(GLuint unit, GLsizei size,
                                              const void* const* faces,
                                              bool srgb) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (faces == nullptr || size <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  FragSlot s;
  s.kind = 1;
  s.w = s.h = size;
  s.d = 6;
  s.srgb = srgb;
  s.levels = 1;
  frag_slots_[unit] = s;
}
void MockMetalBridge::SetFragmentTexture3D(GLuint unit, GLsizei w, GLsizei h,
                                            GLsizei d, const void* data,
                                            bool srgb) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data == nullptr || w <= 0 || h <= 0 || d <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  FragSlot s;
  s.kind = 2;
  s.w = w;
  s.h = h;
  s.d = d;
  s.srgb = srgb;
  s.levels = 1;
  frag_slots_[unit] = s;
}
void MockMetalBridge::SetFragmentTextureArray(GLuint unit, GLsizei w, GLsizei h,
                                               GLsizei layers,
                                               const void* data, bool srgb) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data == nullptr || w <= 0 || h <= 0 || layers <= 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  FragSlot s;
  s.kind = 3;
  s.w = w;
  s.h = h;
  s.d = layers;
  s.srgb = srgb;
  s.levels = 1;
  frag_slots_[unit] = s;
}
void MockMetalBridge::SetFragmentTextureMips(GLuint unit, GLsizei base_w,
                                              GLsizei base_h, int levels,
                                              const void* const* data,
                                              bool srgb) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data == nullptr || base_w <= 0 || base_h <= 0 || levels < 1) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  FragSlot s;
  s.kind = 4;
  s.w = base_w;
  s.h = base_h;
  s.d = 1;
  s.srgb = srgb;
  s.levels = levels;
  frag_slots_[unit] = s;
}
void MockMetalBridge::SetFragmentTextureCubeMips(
    GLuint unit, GLsizei base_size, int levels, const void* const* faces,
    bool srgb) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (faces == nullptr || base_size <= 0 || levels < 1) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  FragSlot s;
  s.kind = 5;  // cube-mips.
  s.w = s.h = base_size;
  s.d = 6;
  s.srgb = srgb;
  s.levels = levels;
  frag_slots_[unit] = s;
}
void MockMetalBridge::SetFragmentTexture3DMips(
    GLuint unit, GLsizei w, GLsizei h, GLsizei d, int levels,
    const void* const* data, bool srgb) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data == nullptr || w <= 0 || h <= 0 || d <= 0 || levels < 1) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  FragSlot s;
  s.kind = 6;  // 3D-mips.
  s.w = w;
  s.h = h;
  s.d = d;
  s.srgb = srgb;
  s.levels = levels;
  frag_slots_[unit] = s;
}
void MockMetalBridge::SetFragmentTextureArrayMips(
    GLuint unit, GLsizei w, GLsizei h, GLsizei layers, int levels,
    const void* const* data, bool srgb) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data == nullptr || w <= 0 || h <= 0 || layers <= 0 || levels < 1) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  FragSlot s;
  s.kind = 7;  // array-mips.
  s.w = w;
  s.h = h;
  s.d = layers;
  s.srgb = srgb;
  s.levels = levels;
  frag_slots_[unit] = s;
}
void MockMetalBridge::SetFragmentSamplerDetail(GLuint unit, GLenum min_filter,
                                                GLenum mag_filter,
                                                GLenum wrap_s, GLenum wrap_t) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  frag_sampler_detail_[unit] = {min_filter, mag_filter, wrap_s, wrap_t};
}
void MockMetalBridge::SetSamplerLod(GLuint unit, float min_lod, float max_lod) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (min_lod > max_lod) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  sampler_lod_[unit] = {min_lod, max_lod};
}
float MockMetalBridge::SamplerMinLod(GLuint unit) const {
  auto it = sampler_lod_.find(unit);
  return it == sampler_lod_.end() ? -1000.0f : it->second.first;
}
float MockMetalBridge::SamplerMaxLod(GLuint unit) const {
  auto it = sampler_lod_.find(unit);
  return it == sampler_lod_.end() ? 1000.0f : it->second.second;
}
void MockMetalBridge::SetSamplerSlots(const GLuint* units, std::size_t count) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (units == nullptr && count != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  sampler_slots_.assign(units, units + count);
}
void MockMetalBridge::SetVertexSamplerSlots(const GLuint* units,
                                             std::size_t count) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (units == nullptr && count != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  vertex_sampler_slots_.assign(units, units + count);
}
void MockMetalBridge::SetMrtCount(GLsizei n) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (n < 1 || n > 8) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  mrt_count_ = n;
}
bool MockMetalBridge::ReadbackAttachment(GLuint /*index*/, GLint /*x*/,
                                         GLint /*y*/,
                                         std::uint8_t /*out*/[4]) {
  errors_.Record(kGlInvalidOperation);
  return false;
}
void MockMetalBridge::BeginRenderPassNoClear() {
  // Same ordering contract as BeginRenderPass (no pixels in the Mock).
  if (!frame_open_ || render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  render_open_ = true;
  ++pass_load_count_;
}
void MockMetalBridge::SetUboBytes(GLuint block, const void* data,
                                   std::size_t bytes) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data == nullptr && bytes != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  ubo_bytes_[block].assign(static_cast<const std::uint8_t*>(data),
                           static_cast<const std::uint8_t*>(data) + bytes);
}
void MockMetalBridge::ConfigureCull(const CullConfig& config) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (config.mode != kGlFront && config.mode != kGlBackFace &&
      config.mode != kGlFrontAndBack) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  cull_cfg_ = config;
}
void MockMetalBridge::ConfigureViewport(const ViewportConfig& config) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (config.width < 0 || config.height < 0 || config.scissor_width < 0 ||
      config.scissor_height < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  viewport_cfg_ = config;
}

bool MockMetalBridge::HasFragmentTexture(GLuint unit) const {
  if (!initialized_) return false;
  return frag_textures_.find(unit) != frag_textures_.end();
}

bool MockMetalBridge::ComputeIncrementUint32(const std::uint32_t* input,
                                             std::uint32_t* output,
                                             std::size_t count) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  if ((input == nullptr || output == nullptr) && count != 0) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  for (std::size_t i = 0; i < count; ++i) output[i] = input[i] + 1u;
  return true;
}
void MockMetalBridge::SetBlendColor(float r, float g, float b, float a) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  blend_color_[0] = r;
  blend_color_[1] = g;
  blend_color_[2] = b;
  blend_color_[3] = a;
}
void MockMetalBridge::SetUniformBytes(const void* data, std::size_t bytes) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (data == nullptr && bytes != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  uniform_bytes_.assign(static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + bytes);
}
std::uint32_t MockMetalBridge::CreateTranslatedPipeline(
    const glsl::TranslatedProgram& prog, const backend::PsoKey& key) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  if (!prog.ok || prog.library_source.empty() || prog.vertex_fn.empty() ||
      prog.fragment_fn.empty() || !key.translated) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  // Program shape must agree with the key (a multi-out shader on a single
  // target — or vice versa — is a Metal validation error, failed here).
  if ((key.mrt_count > 1) != prog.is_mrt) {
    errors_.Record(kGlInvalidOperation);
    return 0;
  }
  auto it = pipelines_.find(key);
  if (it != pipelines_.end()) return it->second;
  const std::uint32_t handle = next_handle_++;
  pipelines_[key] = handle;
  handle_needs_texture_[handle] = prog.uses_sampler;
  ++translated_count_;
  last_vertex_fn_ = prog.vertex_fn;
  last_fragment_fn_ = prog.fragment_fn;
  return handle;
}

void MockMetalBridge::ConfigureDepth(const DepthConfig& config) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (config.func < kGlNever || config.func > kGlAlways) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  depth_cfg_ = config;
}
bool MockMetalBridge::DepthEnabled() const {
  return initialized_ && depth_cfg_.enabled;
}
DepthConfig MockMetalBridge::LastDepthConfig() const { return depth_cfg_; }

namespace {

bool IsCompareFunc(GLenum func) {
  return func >= kGlNever && func <= kGlAlways;
}

bool IsStencilOp(GLenum op) {
  switch (op) {
    case 0x1E00u:  // KEEP
    case 0x0000u:  // ZERO
    case 0x1E01u:  // REPLACE
    case 0x1E02u:  // INCR
    case 0x1E03u:  // DECR
    case 0x150Au:  // INVERT
    case 0x8507u:  // INCR_WRAP
    case 0x8508u:  // DECR_WRAP
      return true;
    default:
      return false;
  }
}

}  // namespace

void MockMetalBridge::ConfigureStencil(const StencilConfig& config) {
  if (!initialized_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (!IsCompareFunc(config.func_front) || !IsCompareFunc(config.func_back)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  const GLenum ops[] = {config.sfail_front,  config.sfail_back,
                        config.dpfail_front, config.dpfail_back,
                        config.dppass_front, config.dppass_back};
  for (GLenum op : ops) {
    if (!IsStencilOp(op)) {
      errors_.Record(kGlInvalidOperation);  // Cannot compile to Metal op.
      return;
    }
  }
  if (config.enabled && config.ref_front != config.ref_back) {
    // Mirrors the Apple bridge: one reference value per encoder.
    errors_.Record(kGlInvalidOperation);
    return;
  }
  stencil_cfg_ = config;
}
bool MockMetalBridge::StencilEnabled() const {
  return initialized_ && stencil_cfg_.enabled;
}
StencilConfig MockMetalBridge::LastStencilConfig() const {
  return stencil_cfg_;
}

bool NullMetalBridge::TextureProbe(GLuint /*unit*/, GLint /*x*/, GLint /*y*/,
                                   std::uint8_t /*out_rgba*/[4]) {
  errors_.Record(kGlInvalidOperation);
  return false;
}
bool MockMetalBridge::TextureProbe(GLuint /*unit*/, GLint /*x*/, GLint /*y*/,
                                   std::uint8_t /*out_rgba*/[4]) {
  // Device-only (the Mock keeps no rasterized/uploaded pixels).
  errors_.Record(kGlInvalidOperation);
  return false;
}
bool MockMetalBridge::ReadbackPixel(GLint /*x*/, GLint /*y*/,
                                    std::uint8_t /*out_rgba*/[4]) {
  // No rasterizer in the Mock; pixels are device-only (Apple bridge).
  errors_.Record(kGlInvalidOperation);
  return false;
}

bool MockMetalBridge::BlitToCpu(void* /*dst*/, std::size_t /*bytes*/) {
  errors_.Record(kGlInvalidOperation);
  return false;
}

bool MockMetalBridge::WaitForCompletion(std::uint64_t serial) {
  // Mock fence ring signals immediately: every presented serial is complete.
  if (serial > frame_serial_) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  return true;
}

void MockMetalBridge::SetLayer(void* ca_metal_layer, GLsizei width,
                               GLsizei height) {
  layer_ = ca_metal_layer;
  layer_width_ = width;
  layer_height_ = height;
}

void MockMetalBridge::Resize(GLsizei width, GLsizei height) {
  layer_width_ = width;
  layer_height_ = height;
}

bool MockMetalBridge::Present() {
  if (layer_ == nullptr || layer_width_ <= 0 || layer_height_ <= 0) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  // Multi-draw: SubmitVertices may leave the frame open; seal before present.
  if (frame_open_ && !render_open_ && !committed_) {
    if (!CommitFrame()) return false;
  }
  if (!committed_) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  ++frame_serial_;
  ++swaps_;
  // Mock fence ring: signal is immediate, so completed follows current.
  // The real .mm polls MTLSharedEvent/fences instead of advancing here.
  completed_serial_ = frame_serial_;
  committed_ = false;
  return true;
}

std::uint64_t MockMetalBridge::FrameSerial() const { return frame_serial_; }
std::uint64_t MockMetalBridge::CompletedSerial() const {
  return completed_serial_;
}
std::uint64_t MockMetalBridge::SwapCount() const { return swaps_; }
GLenum MockMetalBridge::GetError() { return errors_.Get(); }

}  // namespace metal_bridge
}  // namespace tgles
