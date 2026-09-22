#ifndef TGLES_GLES_H
#define TGLES_GLES_H

// Full-context facade (step 10): owns every stage manager, wires the three
// cross-module couplings, unifies error polling (distributed flag-code pairs,
// spec 2.3.1) and exposes the conformance checklist + version report.

#include <cstdint>
#include <string>
#include <vector>

#include "tgles/gpu/backend.h"
#include "tgles/state/buffer.h"
#include "tgles/state/compute.h"
#include "tgles/state/context.h"
#include "tgles/state/debug.h"
#include "tgles/pipeline/draw.h"
#include "tgles/state/framebuffer.h"
#include "tgles/state/geometry.h"
#include "tgles/state/image_units.h"
#include "tgles/gpu/metal_bridge.h"
#include "tgles/state/pixels.h"
#include "tgles/state/program.h"
#include "tgles/state/query.h"
#include "tgles/pipeline/raster.h"
#include "tgles/state/sampler.h"
#include "tgles/state/shader.h"
#include "tgles/state/sync.h"
#include "tgles/state/tessellation.h"
#include "tgles/state/texture.h"
#include "tgles/state/transform_feedback.h"
#include "tgles/state/vertex_array.h"

namespace tgles {

class GlesContext {
 public:
  static GlesContext Create(bool debug = false);

  // Unified error poll: first pending code in pipeline order wins.
  GLenum GetError();

  // Cross-module wirings.
  void BindBuffer(GLenum target, GLuint buffer);  // Syncs VAO EAB state.
  void SyncIndirectState();  // Pushes DRAW_INDIRECT binding into validator.
  void SyncElementState();  // Pushes VAO EAB presence into validator.
  // glClear on the CPU model (spec 17.3): forwards the raster clear color
  // into FramebufferManager::Clear (see its doc for the exact rules).
  void Clear(GLbitfield mask);
  // Indirect compute dispatch (spec 19): syncs the indirect-buffer binding,
  // then validates offset/alignment/program in ComputeState.
  void DispatchComputeIndirect(std::uintptr_t indirect);
  void SyncComputeProgram();  // Pushes current compute stage into dispatcher.
  void DrawElementsIndirect(GLenum mode, GLenum type, std::uintptr_t indirect);
  void DispatchCompute(GLuint x, GLuint y, GLuint z);
  // Instanced/indirect draws (spec 10.5): validation via DrawValidator, then
  // CPU-side expansion into SubmitVertices (per-instance stepping for
  // divisor>0 attribs included). Returns false + records when validation or
  // the bridge fails.
  bool RenderInstanced(metal_bridge::MetalBridge* bridge, GLenum mode,
                       GLint first, GLsizei count, GLsizei instanceCount);
  bool RenderElementsInstanced(metal_bridge::MetalBridge* bridge, GLenum mode,
                               GLsizei count, GLenum type,
                               std::uintptr_t indices, GLsizei instanceCount);
  bool RenderArraysIndirect(metal_bridge::MetalBridge* bridge, GLenum mode,
                            std::uintptr_t indirect);
  bool RenderElementsIndirect(metal_bridge::MetalBridge* bridge, GLenum mode,
                              GLenum type, std::uintptr_t indirect);
  // Pixel readback (spec 17.3, docs.gl/es3): validates via PixelState against
  // the READ FBO extents, checks completeness + PACK buffer bounds, then
  // copies RGBA8 store to client memory (UBYTE) or float (FLOAT). Only
  // RGBA/UBYTE and RGBA/FLOAT pairs are supported on normalized surfaces;
  // others are INVALID_OPERATION. ReadnPixels adds bufSize check.
  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void* pixels);
  void ReadnPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, GLsizei bufSize, void* pixels);
  // Framebuffer-sourced texture define (spec 8.10): checks READ FBO complete,
  // then defines the level via TextureManager with white fill (source content
  // is not modeled in the CPU store; validation is the contract).
  void CopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLsizei width, GLsizei height, GLint border);

  // Draws VAO arrays through a MetalBridge frame (step 11 wiring):
  // decodes attribs 0/1 (position/color) from their ARRAY_BUFFER stores
  // (fixed-point/packed/normalized/divisor aware), uploads interleaved
  // float4 pairs + the current program's u_modelViewProj (or identity),
  // and sizes the target from the bound draw-FBO COLOR_ATTACHMENT0.
  // Core topologies execute (POINTS/LINES/STRIP/TRIANGLES + CPU-expanded
  // LOOP/FAN); adjacency/PATCHES fail closed. Count 0 is a no-op success.
  // Failures record INVALID_* in draw_ (see GetError).
  bool RenderFrame(metal_bridge::MetalBridge& bridge, GLenum mode,
                   GLint first, GLsizei count);

  // Indexed sibling (spec 10.5): `indices` is a byte offset into the
  // ELEMENT_ARRAY_BUFFER bound in the current VAO (ES 3.2 core has no client
  // arrays, so no bound EAB is INVALID_OPERATION). Decodes `count` indices
  // of `type` then draws exactly like RenderFrame; count 0 is a no-op.
  bool RenderElements(metal_bridge::MetalBridge& bridge, GLenum mode,
                      GLsizei count, GLenum type, std::uintptr_t indices);
  // Base-vertex family (spec 10.5): like RenderElements but every decoded
  // index is shifted by `basevertex` (negative values allowed; results are
  // validated as GLint-addressable before submit). CPU-side index rebasing,
  // then the shared SubmitVertices path.
  bool RenderElementsBaseVertex(metal_bridge::MetalBridge& bridge, GLenum mode,
                                GLsizei count, GLenum type,
                                std::uintptr_t indices, GLint basevertex);
  bool RenderRangeElementsBaseVertex(metal_bridge::MetalBridge& bridge,
                                     GLenum mode, GLuint start, GLuint end,
                                     GLsizei count, GLenum type,
                                     std::uintptr_t indices, GLint basevertex);
  bool RenderElementsInstancedBaseVertex(metal_bridge::MetalBridge* bridge,
                                         GLenum mode, GLsizei count,
                                         GLenum type, std::uintptr_t indices,
                                         GLsizei instanceCount,
                                         GLint basevertex);
  // EXT_base_instance family (GL_EXT_base_instance): like the instanced
  // renders above, but every instance id is shifted by `baseinstance`
  // (divisor attribs fetch element (instance+base)/divisor, spec language).
  // DirectGLES emulates baseInstance with attribute offsets when the host
  // lacks these; TGL executes them natively.
  bool RenderArraysInstancedBaseInstance(metal_bridge::MetalBridge* bridge,
                                         GLenum mode, GLint first,
                                         GLsizei count, GLsizei instanceCount,
                                         GLuint baseInstance);
  bool RenderElementsInstancedBaseInstance(metal_bridge::MetalBridge* bridge,
                                           GLenum mode, GLsizei count,
                                           GLenum type, std::uintptr_t indices,
                                           GLsizei instanceCount,
                                           GLuint baseInstance);
  bool RenderElementsInstancedBaseVertexBaseInstance(
      metal_bridge::MetalBridge* bridge, GLenum mode, GLsizei count,
      GLenum type, std::uintptr_t indices, GLsizei instanceCount,
      GLint basevertex, GLuint baseInstance);
  // EXT_multi_draw_indirect family (GL_EXT_multi_draw_indirect): `drawcount`
  // commands spaced `stride` bytes apart in the DRAW_INDIRECT_BUFFER at byte
  // offset `indirect` (stride 0 means tightly packed). Each command executes
  // exactly like its single-draw sibling; a mid-list validation failure
  // stops the list and records (spec: draws before the failure stand).
  bool MultiDrawArraysIndirect(metal_bridge::MetalBridge* bridge, GLenum mode,
                               std::uintptr_t indirect, GLsizei drawcount,
                               GLsizei stride);
  bool MultiDrawElementsIndirect(metal_bridge::MetalBridge* bridge, GLenum mode,
                                 GLenum type, std::uintptr_t indirect,
                                 GLsizei drawcount, GLsizei stride);
  // EXT multi_draw_arrays family (client-side draw lists): `drawcount`
  // entries; entry i draws count[i] indices at EAB offset indices[i] shifted
  // by basevertex[i]. Offsets are byte offsets into the VAO's
  // ELEMENT_ARRAY_BUFFER (TGL has no client arrays, matching its indexed
  // model); a null table with drawcount>0 is INVALID_VALUE.
  bool MultiDrawElementsBaseVertex(metal_bridge::MetalBridge* bridge,
                                   GLenum mode, const GLsizei* count,
                                   GLenum type, const void* const* indices,
                                   GLsizei drawcount,
                                   const GLint* basevertex);

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
  PixelState& pixels() { return pixels_; }
  TransformFeedbackManager& transform_feedback() { return transform_feedback_; }
  backend::PsoCache& pso_cache() { return pso_cache_; }
  backend::CommandPlan& command_plan() { return command_plan_; }

 private:
  explicit GlesContext(bool debug)
      : foundation_(Context::Create(debug)),
        programs_(&shaders_),
        framebuffers_(&textures_, &renderbuffers_) {}

  // Shared frame submission: decodes attribs 0/1 for each vertex reference in
  // `verts` (fixed-point/packed/normalized/divisor aware), then MVP + FBO
  // sizing + depth wiring + bridge submit. Every Render* path funnels here,
  // so the GPU path cannot disagree with itself per draw type.
  struct VertexRef {
    GLint vertex;
    GLsizei instance;
  };
  bool SubmitVertices(metal_bridge::MetalBridge& bridge, GLenum mode,
                      const std::vector<VertexRef>& verts);
  // Shared index decode for the base-vertex family: reads `count` indices of
  // `type` at EAB offset `indices`, adds `basevertex`, appends to `out`.
  bool DecodeIndicesWithBase(GLsizei count, GLenum type,
                             std::uintptr_t indices, GLint basevertex,
                             std::vector<VertexRef>& out);

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
  PixelState pixels_;
  TransformFeedbackManager transform_feedback_;
  backend::PsoCache pso_cache_;
  backend::CommandPlan command_plan_;
};

}  // namespace tgles

#endif  // TGLES_GLES_H
