#include "tgles/gpu/backend.h"

#include <tuple>

#include "tgles/state/buffer.h"   // Usage hints.
#include "tgles/state/texture.h"  // Sized formats + kMaxSamplesValue.
namespace tgles {
namespace backend {

// Field-by-field comparison (never memcmp): the struct has padding bytes
// that memcmp would read as significant, so two default keys could compare
// unequal and the PSO cache would recompile every frame. Listing every field
// also documents exactly which GL state busts the cache — a reader can derive
// the cache behaviour from this function alone.
bool PsoKey::operator==(const PsoKey& other) const {
  return vertex_program == other.vertex_program &&
         fragment_program == other.fragment_program &&
         blend_eq_rgb == other.blend_eq_rgb &&
         blend_eq_alpha == other.blend_eq_alpha &&
         blend_src_rgb == other.blend_src_rgb &&
         blend_dst_rgb == other.blend_dst_rgb &&
         blend_src_alpha == other.blend_src_alpha &&
         blend_dst_alpha == other.blend_dst_alpha &&
         cull_mode == other.cull_mode && depth_func == other.depth_func &&
         depth_write == other.depth_write &&
         blend_enabled == other.blend_enabled &&
         color_write_mask == other.color_write_mask &&
         blend_eq_rgb_per == other.blend_eq_rgb_per &&
         blend_eq_alpha_per == other.blend_eq_alpha_per &&
         blend_src_rgb_per == other.blend_src_rgb_per &&
         blend_dst_rgb_per == other.blend_dst_rgb_per &&
         blend_src_alpha_per == other.blend_src_alpha_per &&
         blend_dst_alpha_per == other.blend_dst_alpha_per &&
         blend_enabled_per == other.blend_enabled_per &&
         color_write_mask_per == other.color_write_mask_per &&
         cull_enabled == other.cull_enabled &&
         depth_enabled == other.depth_enabled &&
         rasterizer_discard == other.rasterizer_discard &&
         tessellation == other.tessellation &&
         sample_count == other.sample_count &&
         textured == other.textured && translated == other.translated &&
         program_id == other.program_id && slot2 == other.slot2 &&
         slot3 == other.slot3 && mrt_count == other.mrt_count;
}

bool PsoKey::operator<(const PsoKey& other) const {
  // std::map needs a strict weak ordering; tuple order matches the == list
  // above so equal keys are never ordered.
  return std::tie(vertex_program, fragment_program, blend_eq_rgb,
                  blend_eq_alpha, blend_src_rgb, blend_dst_rgb,
                  blend_src_alpha, blend_dst_alpha, cull_mode, depth_func,
                  depth_write, blend_enabled, color_write_mask,
                  blend_eq_rgb_per, blend_eq_alpha_per, blend_src_rgb_per,
                  blend_dst_rgb_per, blend_src_alpha_per, blend_dst_alpha_per,
                  blend_enabled_per, color_write_mask_per, cull_enabled,
                  depth_enabled, rasterizer_discard, tessellation,
                  sample_count, textured, translated, program_id, slot2,
                  slot3, mrt_count) <
         std::tie(other.vertex_program, other.fragment_program,
                  other.blend_eq_rgb, other.blend_eq_alpha,
                  other.blend_src_rgb, other.blend_dst_rgb,
                  other.blend_src_alpha, other.blend_dst_alpha,
                  other.cull_mode, other.depth_func, other.depth_write,
                  other.blend_enabled, other.color_write_mask,
                  other.blend_eq_rgb_per, other.blend_eq_alpha_per,
                  other.blend_src_rgb_per, other.blend_dst_rgb_per,
                  other.blend_src_alpha_per, other.blend_dst_alpha_per,
                  other.blend_enabled_per, other.color_write_mask_per,
                  other.cull_enabled, other.depth_enabled,
                  other.rasterizer_discard, other.tessellation,
                   other.sample_count, other.textured, other.translated,
                   other.program_id, other.slot2, other.slot3,
                   other.mrt_count);
}

PsoKey PsoKeyForDraw(Context& ctx, const RasterState& raster) {
  PsoKey key;
  // Effective enable per buffer = global BLEND OR indexed BLEND (spec: Enable
  // sets all, Enablei sets one; OR preserves legacy single-buffer tests that
  // enable via Context only while letting indexed enables win for MRT).
  // Uses the error-free IsBlendEnabledForBuffer so planning never pollutes
  // GetError.
  const bool global_blend = ctx.IsEnabled(kGlBlend) != kGlFalse;
  for (int i = 0; i < 8; ++i) {
    const BlendState b = raster.GetBlend(static_cast<GLuint>(i));
    key.blend_eq_rgb_per[static_cast<std::size_t>(i)] = b.equation_rgb;
    key.blend_eq_alpha_per[static_cast<std::size_t>(i)] = b.equation_alpha;
    key.blend_src_rgb_per[static_cast<std::size_t>(i)] = b.src_rgb;
    key.blend_dst_rgb_per[static_cast<std::size_t>(i)] = b.dst_rgb;
    key.blend_src_alpha_per[static_cast<std::size_t>(i)] = b.src_alpha;
    key.blend_dst_alpha_per[static_cast<std::size_t>(i)] = b.dst_alpha;
    const bool indexed =
        raster.IsBlendEnabledForBuffer(static_cast<GLuint>(i));
    key.blend_enabled_per[static_cast<std::size_t>(i)] =
        global_blend || indexed;
    bool mask[4] = {true, true, true, true};
    raster.GetColorMask(static_cast<GLuint>(i), mask);
    std::uint8_t bits = 0;
    if (mask[0]) bits |= 0x8;
    if (mask[1]) bits |= 0x4;
    if (mask[2]) bits |= 0x2;
    if (mask[3]) bits |= 0x1;
    key.color_write_mask_per[static_cast<std::size_t>(i)] = bits;
  }
  // Buffer-0 mirrors keep single-target code readable.
  key.blend_enabled = key.blend_enabled_per[0];
  key.blend_eq_rgb = key.blend_eq_rgb_per[0];
  key.blend_eq_alpha = key.blend_eq_alpha_per[0];
  key.blend_src_rgb = key.blend_src_rgb_per[0];
  key.blend_dst_rgb = key.blend_dst_rgb_per[0];
  key.blend_src_alpha = key.blend_src_alpha_per[0];
  key.blend_dst_alpha = key.blend_dst_alpha_per[0];
  key.color_write_mask = key.color_write_mask_per[0];
  key.depth_enabled = ctx.IsEnabled(kGlDepthTest) != kGlFalse;
  key.depth_func = raster.GetDepthFunc();
  key.depth_write = raster.GetDepthMask();
  return key;
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
