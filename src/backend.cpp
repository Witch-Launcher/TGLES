#include "tgles/backend.h"

#include <cstring>

#include "tgles/buffer.h"   // Usage hints.
#include "tgles/texture.h"  // Sized formats + kMaxSamplesValue.
namespace tgles {
namespace backend {

bool PsoKey::operator==(const PsoKey& other) const {
  return std::memcmp(this, &other, sizeof(PsoKey)) == 0;
}

bool PsoKey::operator<(const PsoKey& other) const {
  return std::memcmp(this, &other, sizeof(PsoKey)) < 0;
}

PsoCache::PsoCache(std::size_t capacity) : capacity_(capacity) {}

GLuint PsoCache::GetOrCreate(const PsoKey& key, bool* was_hit) {
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    if (was_hit != nullptr) *was_hit = true;
    return it->second;
  }
  if (was_hit != nullptr) *was_hit = false;
  if (entries_.size() >= capacity_ && !order_.empty()) {
    entries_.erase(order_.front());  // FIFO eviction.
    order_.erase(order_.begin());
  }
  const GLuint handle = next_handle_++;
  entries_[key] = handle;
  order_.push_back(key);
  return handle;
}

std::size_t PsoCache::Size() const { return entries_.size(); }

void PsoCache::Clear() {
  entries_.clear();
  order_.clear();
}

StorageMode ResourcePlan::AdviseBufferStorage(GLenum usage) {
  switch (usage) {
    case kGlStaticDraw:
      return StorageMode::kPrivate;  // Upload once, keep GPU-side.
    default:
      return StorageMode::kShared;  // Everything else stays CPU-visible.
  }
}

bool ResourcePlan::IsFormatSupported(GLenum internalformat, const char* family) {
  if (family == nullptr) return false;
  const metal::GpuFamilyInfo* info = metal::FindGpuFamily(family);
  if (info == nullptr) return false;
  // BC/DXT needs Apple9+ (Metal tables May 2026); ASTC/PVRTC/ETC are core.
  if (internalformat == 0x83F1 || internalformat == 0x83F2 ||
      internalformat == 0x83F3) {  // BC1/BC2/BC3.
    return info->supports_bc_compression;
  }
  return true;
}

GLsizei ResourcePlan::ClampSampleCount(GLsizei requested, const char* family) {
  if (family == nullptr) return 0;
  if (metal::FindGpuFamily(family) == nullptr) return 0;
  if (requested <= 1) return requested < 0 ? 0 : requested;
  if (requested <= 2) return 2;
  return 4;  // All Apple3+ families resolve up to 4x MSAA.
}

CommandPlan::CommandPlan() = default;

GLenum CommandPlan::GetError() { return errors_.Get(); }
bool CommandPlan::HasPending() const { return errors_.HasPending(); }

void CommandPlan::BeginRenderPass() {
  if (render_open_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  render_open_ = true;
}

void CommandPlan::Draw() {
  if (!render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  ++draws_;
}

void CommandPlan::EndRenderPass() {
  if (!render_open_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  render_open_ = false;
}

void CommandPlan::Blit() {
  if (render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);  // Blit needs its own encoder.
    return;
  }
}

void CommandPlan::Commit() {
  if (render_open_ || committed_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  committed_ = true;
}

bool CommandPlan::RenderPassOpen() const { return render_open_; }
GLuint CommandPlan::DrawCount() const { return draws_; }
bool CommandPlan::Committed() const { return committed_; }

}  // namespace backend
}  // namespace tgles
