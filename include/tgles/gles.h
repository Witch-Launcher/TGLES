#ifndef TGLES_GLES_H
#define TGLES_GLES_H

// Full-context facade (step 10): owns every stage manager, wires the three
// cross-module couplings, unifies error polling (distributed flag-code pairs,
// spec 2.3.1) and exposes the conformance checklist + version report.

#include <string>
#include <vector>

#include "tgles/backend.h"
#include "tgles/buffer.h"
#include "tgles/compute.h"
#include "tgles/context.h"
#include "tgles/debug.h"
#include "tgles/draw.h"
#include "tgles/framebuffer.h"
#include "tgles/geometry.h"
#include "tgles/image_units.h"
#include "tgles/metal_bridge.h"
#include "tgles/program.h"
#include "tgles/query.h"
#include "tgles/raster.h"
#include "tgles/sampler.h"
#include "tgles/shader.h"
#include "tgles/sync.h"
#include "tgles/tessellation.h"
#include "tgles/texture.h"
#include "tgles/vertex_array.h"

namespace tgles {

class GlesContext {
 public:
  static GlesContext Create(bool debug = false);

  // Unified error poll: first pending code in pipeline order wins.
  GLenum GetError();

  // Cross-module wirings.
  void BindBuffer(GLenum target, GLuint buffer);  // Syncs VAO EAB state.
  void SyncIndirectState();  // Pushes DRAW_INDIRECT binding into validator.
  void SyncComputeProgram();  // Pushes current compute stage into dispatcher.
  void DrawElementsIndirect(GLenum mode, GLenum type, std::uintptr_t indirect);
  void DispatchCompute(GLuint x, GLuint y, GLuint z);

  // Draws VAO arrays through a MetalBridge frame (step 11 wiring):
  // decodes float attribs 0/1 (position/color) from their ARRAY_BUFFER
  // stores, uploads interleaved float4 pairs + the current program's
  // u_modelViewProj (or identity), and sizes the target from the bound
  // draw-FBO COLOR_ATTACHMENT0. Only TRIANGLES executes; count 0 is a
  // no-op success. Failures record INVALID_* in draw_ (see GetError).
  bool RenderFrame(metal_bridge::MetalBridge& bridge, GLenum mode,
                   GLint first, GLsizei count);

  // Conformance: empty vector means every gate passes.
  std::vector<std::string> ConformanceChecklist() const;
  std::string VersionReport() const;

  // MobileGL-DirectGLES host integration: work items still missing before a
  // launcher (e.g. Minecraft Java on iOS) can run end-to-end. Empty means
  // the host side is fully ready.
  static std::vector<std::string> HostIntegrationGaps();

  // Stage access for app code and tests.
  Context& foundation() { return foundation_; }
  BufferManager& buffers() { return buffers_; }
  VertexArrayManager& vertex_arrays() { return vertex_arrays_; }
  ShaderManager& shaders() { return shaders_; }
  ProgramManager& programs() { return programs_; }
  TextureManager& textures() { return textures_; }
  SamplerManager& samplers() { return samplers_; }
  RenderbufferManager& renderbuffers() { return renderbuffers_; }
  FramebufferManager& framebuffers() { return framebuffers_; }
  TessellationState& tessellation() { return tessellation_; }
  ComputeState& compute() { return compute_; }
  SyncManager& sync() { return sync_; }
  QueryManager& queries() { return queries_; }
  DebugManager& debug() { return debug_; }
  DrawValidator& draw() { return draw_; }
  RasterState& raster() { return raster_; }
  ImageUnitManager& images() { return images_; }
  backend::PsoCache& pso_cache() { return pso_cache_; }
  backend::CommandPlan& command_plan() { return command_plan_; }

 private:
  explicit GlesContext(bool debug)
      : foundation_(Context::Create(debug)),
        programs_(&shaders_),
        framebuffers_(&textures_, &renderbuffers_) {}

  Context foundation_;
  BufferManager buffers_;
  VertexArrayManager vertex_arrays_;
  ShaderManager shaders_;
  ProgramManager programs_;
  TextureManager textures_;
  SamplerManager samplers_;
  RenderbufferManager renderbuffers_;
  FramebufferManager framebuffers_;
  TessellationState tessellation_;
  GeometryState geometry_;
  ComputeState compute_;
  SyncManager sync_;
  QueryManager queries_;
  DebugManager debug_;
  DrawValidator draw_;
  RasterState raster_;
  ImageUnitManager images_;
  backend::PsoCache pso_cache_;
  backend::CommandPlan command_plan_;
};

}  // namespace tgles

#endif  // TGLES_GLES_H
