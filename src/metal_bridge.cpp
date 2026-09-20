#include "tgles/metal_bridge.h"

#include "tgles/buffer.h"  // Usage hints (kept for future storage checks).
#include "tgles/metal_mapping.h"

namespace tgles {
namespace metal_bridge {

bool NullMetalBridge::Initialize(const char* /*family*/) { return false; }
bool NullMetalBridge::IsInitialized() const { return false; }
std::uint32_t NullMetalBridge::CreateRenderPipeline(
    const backend::PsoKey& /*key*/) {
  return 0;
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
void NullMetalBridge::BeginRenderPass() {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::Draw() { errors_.Record(kGlInvalidOperation); }
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
void NullMetalBridge::SetVertexBytes(const void* /*data*/, std::size_t /*bytes*/,
                                     std::uint32_t /*stride*/) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::SetMVP(const float /*mvp*/[16]) {
  errors_.Record(kGlInvalidOperation);
}
void NullMetalBridge::ConfigureDepth(const DepthConfig& /*config*/) {
  errors_.Record(kGlInvalidOperation);
}
bool NullMetalBridge::DepthEnabled() const { return false; }
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
  frame_open_ = true;
  render_open_ = false;
  committed_ = false;
  draws_ = 0;
  // NOTE: vertex/MVP bindings persist across frames (GL VAO/uniform
  // semantics), mirroring the Apple bridge.
  return true;
}

void MockMetalBridge::BeginRenderPass() {
  if (!frame_open_ || render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  render_open_ = true;
}

void MockMetalBridge::Draw() {
  if (!frame_open_ || !render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  ++draws_;
}

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
}

void MockMetalBridge::SetMVP(const float mvp[16]) {
  if (mvp == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  has_mvp_ = true;
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
