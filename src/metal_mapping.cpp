#include "tgles/metal_mapping.h"

#include <cstring>

namespace tgles {
namespace metal {

namespace {

const FeatureMapping kMappings[] = {
    {"glDrawArrays", "drawPrimitives", MappingKind::kDirect,
     "Topology must be translated (TRIANGLES, LINES, POINTS, PATCHES)."},
    {"glDrawElements", "drawIndexedPrimitives", MappingKind::kDirect,
     "Index type translation required."},
    {"glDrawArraysInstanced", "drawPrimitives with instanceCount",
     MappingKind::kDirect, "Maps to instanced draw."},
    {"glDrawElementsInstanced", "drawIndexedPrimitives with instanceCount",
     MappingKind::kDirect, "Maps to instanced draw."},
    {"glDispatchCompute", "dispatchThreadgroups", MappingKind::kDirect,
     "Compute pipeline; needs memory barriers."},
    {"glFenceSync", "MTLSharedEvent / commit + wait", MappingKind::kDirect,
     "Emulate SYNC_GPU_COMMANDS_COMPLETE with command buffer tracking."},
    {"glPatchParameteri", "MTLTessellationFactorBuffer", MappingKind::kDirect,
     "Requires tessellation pipeline (Apple3+)."},
    {"glPolygonOffset", "depthBias in render pipeline descriptor",
     MappingKind::kDirect, "POLYGON_OFFSET_FILL maps to depth bias state."},
    // Geometry stage has no direct Metal counterpart (pre-mesh Metal).
    {"geometry_shader", "compute fallback or multi-pass", MappingKind::kEmulated,
     "Metal has no geometry stage; use compute/CPU expansion or mesh (Metal 3)."},
    // Tessellation exists on Apple GPUs but cannot drive desktop-class
    // tessellation fully; keep as emulated/conditional direct.
    {"tessellation_shader", "MTLRenderPipeline tessellation stage",
     MappingKind::kEmulated,
     "Available from Apple3 but with limits; needs factor buffer setup."},
    // Point size / line width are implementation-limited ranges in ES,
    // not free widths; Metal effectively supports width 1.
    {"glLineWidth", "fixed width 1 (no wide lines)", MappingKind::kEmulated,
     "ES exposes ALIASED_LINE_WIDTH_RANGE; Metal has no wide-line state."},
    {"gl_PointSize", "point_sprite expansion via quads", MappingKind::kEmulated,
     "Large points need billboard quads in shader."},
    {"occlusion_query", "counting occlusion query (Apple3+)",
     MappingKind::kDirect,
     "ANY_SAMPLES_PASSED maps to Metal visibility / occlusion query."},
    {"timer_query", "no core mapping", MappingKind::kUnsupported,
     "GL_TIME_ELAPSED/GL_TIMESTAMP are EXT only; Metal counters differ."},
};

const GpuFamilyInfo kFamilies[] = {
    // From Metal-Feature-Set-Tables.pdf (May 21, 2026).
    // BC compression starts at Apple9; mesh shading needs Metal 3 (Apple7+).
    {"Apple3", "A9, A10", true, false, false, false},
    {"Apple4", "A11", true, false, false, false},
    {"Apple5", "A12", true, false, false, false},
    {"Apple6", "A13", true, false, false, false},
    {"Apple7", "A14, M1", true, false, true, true},
    {"Apple8", "A15, A16, M2", true, false, true, true},
    {"Apple9", "A17, A18, M3, M4", true, true, true, true},
    {"Apple10", "A19, M5", true, true, true, true},
};

}  // namespace

const FeatureMapping* MappingTable(std::size_t* out_count) {
  if (out_count) *out_count = sizeof(kMappings) / sizeof(kMappings[0]);
  return kMappings;
}

const FeatureMapping* FindMapping(const char* gles_feature) {
  if (gles_feature == nullptr) return nullptr;
  for (const auto& entry : kMappings) {
    if (std::strcmp(entry.gles_feature, gles_feature) == 0) return &entry;
  }
  return nullptr;
}

const GpuFamilyInfo* GpuFamilyTable(std::size_t* out_count) {
  if (out_count) *out_count = sizeof(kFamilies) / sizeof(kFamilies[0]);
  return kFamilies;
}

const GpuFamilyInfo* FindGpuFamily(const char* family) {
  if (family == nullptr) return nullptr;
  for (const auto& entry : kFamilies) {
    if (std::strcmp(entry.family, family) == 0) return &entry;
  }
  return nullptr;
}

}  // namespace metal
}  // namespace tgles
