#ifndef TGLES_METAL_MAPPING_H
#define TGLES_METAL_MAPPING_H

#include <cstddef>

namespace tgles {
namespace metal {

// How one GLES feature maps to Metal.
enum class MappingKind {
  kDirect,     // One-to-one Metal API exists.
  kEmulated,   // Must be emulated (multi-pass, compute fallback, CPU assist).
  kUnsupported // No reasonable mapping; must report error or skip.
};

struct FeatureMapping {
  const char* gles_feature;  // e.g. "glDrawArrays"
  const char* metal_equivalent;  // e.g. "drawPrimitives"
  MappingKind kind;
  const char* note;
};

// Canonical mapping table (subset covering plan-01.md Table III.3).
const FeatureMapping* MappingTable(std::size_t* out_count);
const FeatureMapping* FindMapping(const char* gles_feature);

// Apple GPU family knowledge from Metal-Feature-Set-Tables.pdf (May 21, 2026).
struct GpuFamilyInfo {
  const char* family;      // "Apple3" .. "Apple10"
  const char* representative_socs;  // e.g. "A9, A10"
  bool supports_tessellation;
  bool supports_bc_compression;  // BC/DXT pixel formats.
  bool supports_argument_buffers_tier2;
  bool supports_mesh_shader;  // Metal 3 mesh stage.
};

const GpuFamilyInfo* GpuFamilyTable(std::size_t* out_count);
const GpuFamilyInfo* FindGpuFamily(const char* family);

}  // namespace metal
}  // namespace tgles

#endif  // TGLES_METAL_MAPPING_H
