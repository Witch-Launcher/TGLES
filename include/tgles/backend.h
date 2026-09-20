#ifndef TGLES_BACKEND_H
#define TGLES_BACKEND_H

// Metal backend planning layer (step 9): PSO cache keyed by baked GL state,
// resource placement advice, texture-format family gating and command-encoder
// ordering. Pure C++ (no MTL headers) so host and iOS builds share it; the
// ObjC++ bridge in the iOS target turns each plan into MTL calls.

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "tgles/error.h"
#include "tgles/gl_types.h"
#include "tgles/metal_mapping.h"

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
  GLenum blend_eq_rgb = 0x8006;  // FUNC_ADD.
  GLenum blend_eq_alpha = 0x8006;
  GLenum blend_src_rgb = 1;  // ONE.
  GLenum blend_dst_rgb = 0;  // ZERO.
  GLenum blend_src_alpha = 1;
  GLenum blend_dst_alpha = 0;
  GLenum cull_mode = 0x0405;  // BACK.
  GLenum depth_func = 0x0201;  // LESS.
  bool depth_write = true;
  bool blend_enabled = false;
  bool cull_enabled = false;
  bool depth_enabled = false;
  bool rasterizer_discard = false;
  bool tessellation = false;
  GLsizei sample_count = 0;

  bool operator==(const PsoKey& other) const;
  bool operator<(const PsoKey& other) const;
};

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