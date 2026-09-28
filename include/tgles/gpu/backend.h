#ifndef TGLES_BACKEND_H
#define TGLES_BACKEND_H

// Metal backend planning layer (step 9): PSO cache keyed by baked GL state,
// resource placement advice, texture-format family gating and command-encoder
// ordering. Pure C++ (no MTL headers) so host and iOS builds share it; the
// ObjC++ bridge in the iOS target turns each plan into MTL calls.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"
#include "tgles/gpu/metal_mapping.h"
#include "tgles/pipeline/raster.h"
#include "tgles/state/context.h"

namespace tgles {
namespace backend {

// Storage advice for a buffer (maps to MTLStorageMode in the bridge).
enum class StorageMode {
  kShared,   // CPU-visible (dynamic/streaming data).
  kPrivate,  // GPU-only (static data uploaded once via blit).
};

struct PsoKey {
  GLuint vertex_program = 0;
  GLuint fragment_program = 0;
  GLenum blend_eq_rgb = 0x8006;  // FUNC_ADD (buffer 0 mirror).
  GLenum blend_eq_alpha = 0x8006;
  GLenum blend_src_rgb = 1;  // ONE.
  GLenum blend_dst_rgb = 0;  // ZERO.
  GLenum blend_src_alpha = 1;
  GLenum blend_dst_alpha = 0;
  GLenum cull_mode = 0x0405;  // BACK.
  GLenum depth_func = 0x0201;  // LESS.
  bool depth_write = true;
  bool blend_enabled = false;  // Buffer-0 mirror (see per-buffer below).
  // Color write mask as RGBA bits (Red=0x8 .. Alpha=0x1, the Metal order —
  // see metal_translate::ColorWriteMask). Part of the key: masking red out
  // must not reuse a PSO compiled with red on.
  std::uint8_t color_write_mask = 0x0F;
  // Per-buffer blend (Phase 4 item 5, ES 3.2 §14.1 indexed state): equations,
  // factors, enables and masks for draw buffers 0..7. Buffer-0 entries mirror
  // the scalar fields above so single-target keys stay readable; the bridge
  // loops `mrt_count` descriptors from these arrays.
  std::array<GLenum, 8> blend_eq_rgb_per = {0x8006, 0x8006, 0x8006, 0x8006,
                                            0x8006, 0x8006, 0x8006, 0x8006};
  std::array<GLenum, 8> blend_eq_alpha_per = {0x8006, 0x8006, 0x8006, 0x8006,
                                              0x8006, 0x8006, 0x8006, 0x8006};
  std::array<GLenum, 8> blend_src_rgb_per = {1, 1, 1, 1, 1, 1, 1, 1};
  std::array<GLenum, 8> blend_dst_rgb_per = {0, 0, 0, 0, 0, 0, 0, 0};
  std::array<GLenum, 8> blend_src_alpha_per = {1, 1, 1, 1, 1, 1, 1, 1};
  std::array<GLenum, 8> blend_dst_alpha_per = {0, 0, 0, 0, 0, 0, 0, 0};
  std::array<bool, 8> blend_enabled_per = {false, false, false, false,
                                           false, false, false, false};
  std::array<std::uint8_t, 8> color_write_mask_per = {0x0F, 0x0F, 0x0F, 0x0F,
                                                      0x0F, 0x0F, 0x0F, 0x0F};
  bool cull_enabled = false;
  bool depth_enabled = false;
  bool rasterizer_discard = false;
  bool tessellation = false;
  GLsizei sample_count = 0;
  // Textured pipeline (ES 3.2 §8 sampling): true selects the
  // vert_tex_main/frag_tex_main MSL pair (pos@0+col@1+uv@2, stride 40,
  // fragment samples texture unit 0). False is the legacy color-only pair
  // (stride 32). Part of the key: sampling vs non-sampling PSOs differ in
  // vertex descriptor + fragment function and must never alias.
  // For translated programs (GLSL->MSL) `textured` means "slot 2 present"
  // while sampling is decided by the translated fragment; `translated`+
  // `program_id` keep per-program MSL from aliasing each other.
  // `slot2` is the attrib-2 component count (0 = absent, else 2/3/4):
  // vertex stride is 32 + 4*slot2, so cube directions (vec3) and uvs share
  // one layout rule. `slot3` is the attrib-3 (UV2/lightmap) component count
  // (0 = absent, else 2/3/4): stride becomes 32 + 4*slot2 + 4*slot3 and
  // attribute 3 sits at offset 32+4*slot2. `mrt_count` (1..8) declares extra
  // color attachments.
  bool textured = false;
  bool translated = false;
  std::uint32_t program_id = 0;
  std::uint8_t slot2 = 0;
  std::uint8_t slot3 = 0;
  std::uint8_t mrt_count = 1;

   bool operator==(const PsoKey& other) const;
   bool operator<(const PsoKey& other) const;
};

// Builds the PSO key for one draw from live GL state (type-1 path): blend
// enable + per-buffer equations/factors/mask from the raster unit, depth
// enable/func/mask from the enable cap + raster unit. Stored state is always
// valid (setters validate), so the key never carries an untranslatable enum
// — the bridge compiles it without re-validating.
PsoKey PsoKeyForDraw(Context& ctx, const RasterState& raster);

// Fixed-capacity PSO cache: creating MTLRenderPipelineState is expensive,
// so compiled states are reused (FIFO eviction, documented).
class PsoCache {
 public:
  explicit PsoCache(std::size_t capacity = 256);

  // Returns the PSO handle; sets was_hit true on cache hit.
  GLuint GetOrCreate(const PsoKey& key, bool* was_hit);
  std::size_t Size() const;
  void Clear();

 private:
  std::size_t capacity_;
  GLuint next_handle_ = 1;
  std::map<PsoKey, GLuint> entries_;
  std::vector<PsoKey> order_;  // Insertion order for FIFO eviction.
};

// Resource placement + format gating.
class ResourcePlan {
 public:
  // Dynamic/streaming usage prefers Shared; static draw prefers Private.
  static StorageMode AdviseBufferStorage(GLenum usage);

  // True when the family can back the format without CPU decompression.
  static bool IsFormatSupported(GLenum internalformat, const char* family);

  // Clamp a requested multisample count to what the family allows (0/1/2/4).
  static GLsizei ClampSampleCount(GLsizei requested, const char* family);
};

// Command-encoder ordering model.
class CommandPlan {
 public:
  CommandPlan();

  GLenum GetError();
  bool HasPending() const;

  void BeginRenderPass();
  void Draw();  // Requires an open render pass.
  void EndRenderPass();
  void Blit();  // Requires the render pass to be closed.
  void Commit();

  bool RenderPassOpen() const;
  GLuint DrawCount() const;
  bool Committed() const;

 private:
  ErrorQueue errors_;
  bool render_open_ = false;
  bool committed_ = false;
  GLuint draws_ = 0;
};

}  // namespace backend
}  // namespace tgles

#endif  // TGLES_BACKEND_H