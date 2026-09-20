#include "tgles/gles.h"

#include <sstream>
#include <vector>

#include "tgles/gl_proc_names.h"
#include "tgles/image_units.h"
#include "tgles/metal_mapping.h"
#include "tgles/msl_translation.h"
#include "tgles/spec.h"

namespace tgles {

GlesContext GlesContext::Create(bool debug) {
  return GlesContext(debug);
}

GLenum GlesContext::GetError() {
  GLenum code = foundation_.GetError();
  if (code != kGlNoError) return code;
  code = buffers_.GetError();
  if (code != kGlNoError) return code;
  code = vertex_arrays_.GetError();
  if (code != kGlNoError) return code;
  code = shaders_.GetError();
  if (code != kGlNoError) return code;
  code = programs_.GetError();
  if (code != kGlNoError) return code;
  code = textures_.GetError();
  if (code != kGlNoError) return code;
  code = samplers_.GetError();
  if (code != kGlNoError) return code;
  code = renderbuffers_.GetError();
  if (code != kGlNoError) return code;
  code = framebuffers_.GetError();
  if (code != kGlNoError) return code;
  code = tessellation_.GetError();
  if (code != kGlNoError) return code;
  code = compute_.GetError();
  if (code != kGlNoError) return code;
  code = sync_.GetError();
  if (code != kGlNoError) return code;
  code = queries_.GetError();
  if (code != kGlNoError) return code;
  code = debug_.GetError();
  if (code != kGlNoError) return code;
  code = draw_.GetError();
  if (code != kGlNoError) return code;
  code = raster_.GetError();
  if (code != kGlNoError) return code;
  code = images_.GetError();
  if (code != kGlNoError) return code;
  return command_plan_.GetError();
}

void GlesContext::BindBuffer(GLenum target, GLuint buffer) {
  buffers_.BindBuffer(target, buffer);
  if (buffers_.HasPending()) return;  // Invalid target: nothing else to do.
  if (target == kGlElementArrayBuffer) {
    vertex_arrays_.SetElementArrayBuffer(buffer);
  }
}

void GlesContext::SyncIndirectState() {
  draw_.SetIndirectBufferBound(
      buffers_.BoundBuffer(kGlDrawIndirectBuffer) != 0);
}

void GlesContext::SyncComputeProgram() {
  const GLuint current = programs_.CurrentProgram();
  compute_.SetComputeProgramBound(
      current != 0 && programs_.ProgramHasStage(current, kGlComputeShader));
}

void GlesContext::DrawElementsIndirect(GLenum mode, GLenum type,
                                       std::uintptr_t indirect) {
  SyncIndirectState();
  draw_.DrawElementsIndirect(mode, type, indirect);
}

void GlesContext::DispatchCompute(GLuint x, GLuint y, GLuint z) {
  SyncComputeProgram();
  compute_.DispatchCompute(x, y, z);
}

namespace {

// Decodes one float vec3/vec4 attribute element into a float4 (pads w=1).
// Only kGlFloat attribs are bridge-decodable (matches the MSL VertexIn).
bool DecodeAttribFloat4(const GenericAttrib& attrib, const std::uint8_t* store,
                        GLsizeiptr store_size, GLint vertex, GLfloat out[4]) {
  if (attrib.type != kGlFloat || attrib.pure_integer) return false;
  if (attrib.size != 3 && attrib.size != 4) return false;
  const GLsizeiptr elem =
      (attrib.stride != 0) ? attrib.stride
                           : static_cast<GLsizeiptr>(attrib.size) * 4;
  const GLsizeiptr at =
      static_cast<GLsizeiptr>(attrib.offset) +
      static_cast<GLsizeiptr>(vertex) * elem;
  if (at < 0 || at + static_cast<GLsizeiptr>(attrib.size) * 4 > store_size) {
    return false;
  }
  const float* f =
      reinterpret_cast<const float*>(store + at);
  for (GLint i = 0; i < attrib.size; ++i) out[i] = f[i];
  for (GLint i = attrib.size; i < 4; ++i) out[i] = 1.0f;
  return true;
}

}  // namespace

bool GlesContext::RenderFrame(metal_bridge::MetalBridge& bridge, GLenum mode,
                              GLint first, GLsizei count) {
  draw_.DrawArrays(mode, first, count);
  if (draw_.HasPending()) return false;
  if (count == 0) return true;  // Valid no-op; touch no bridge state.
  if (mode != kGlTriangles) {
    draw_.FlagBridgeError();
    return false;
  }
  const GenericAttrib pos = vertex_arrays_.AttribState(0);
  const GenericAttrib col = vertex_arrays_.AttribState(1);
  if (!pos.enabled || !col.enabled) {
    draw_.FlagBridgeError();
    return false;
  }
  const std::uint8_t* pos_bytes = nullptr;
  const std::uint8_t* col_bytes = nullptr;
  GLsizeiptr pos_size = 0, col_size = 0;
  if (!buffers_.GetBufferBytes(pos.buffer, &pos_bytes, &pos_size) ||
      !buffers_.GetBufferBytes(col.buffer, &col_bytes, &col_size)) {
    draw_.FlagBridgeError();
    return false;
  }
  std::vector<std::uint8_t> interleaved;
  interleaved.reserve(static_cast<std::size_t>(count) * 32);
  for (GLint v = first; v < first + count; ++v) {
    GLfloat p[4], c[4];
    if (!DecodeAttribFloat4(pos, pos_bytes, pos_size, v, p) ||
        !DecodeAttribFloat4(col, col_bytes, col_size, v, c)) {
      draw_.FlagBridgeError();
      return false;
    }
    const std::uint8_t* pp = reinterpret_cast<const std::uint8_t*>(p);
    const std::uint8_t* cp = reinterpret_cast<const std::uint8_t*>(c);
    interleaved.insert(interleaved.end(), pp, pp + 16);
    interleaved.insert(interleaved.end(), cp, cp + 16);
  }
  GLfloat mvp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const GLuint current = programs_.CurrentProgram();
  if (current != 0) {
    const GLint loc = programs_.GetUniformLocation(current, "u_modelViewProj");
    if (loc >= 0) programs_.GetUniformMatrix(loc, mvp);  // Else identity.
  }
  const GLuint draw_fbo =
      framebuffers_.BoundFramebuffer(kGlDrawFramebuffer);
  const Attachment fb_att =
      framebuffers_.AttachmentState(draw_fbo, kGlColorAttachment0);
  if (fb_att.width <= 0 || fb_att.height <= 0) {
    draw_.FlagBridgeError();
    return false;
  }
  // Depth wiring: the GPU depth test runs only when the app enabled
  // DEPTH_TEST and bound a depth (or depth-stencil) attachment sized like
  // the color target. A depth test without a depth buffer renders
  // color-only (as if the test always passes); a size mismatch fails
  // closed. DEPTH_ATTACHMENT wins over DEPTH_STENCIL_ATTACHMENT.
  const bool depth_test =
      foundation_.IsEnabled(kGlDepthTest) != kGlFalse;
  const Attachment depth_tex =
      framebuffers_.AttachmentState(draw_fbo, kGlDepthAttachment);
  const Attachment depth_packed = framebuffers_.AttachmentState(
      draw_fbo, kGlDepthStencilAttachment);
  const Attachment* depth_src = nullptr;
  if (depth_tex.present) {
    depth_src = &depth_tex;
  } else if (depth_packed.present) {
    depth_src = &depth_packed;
  }
  metal_bridge::DepthConfig depth_cfg;  // Disabled by default.
  if (depth_test && depth_src != nullptr) {
    if (depth_src->width != fb_att.width ||
        depth_src->height != fb_att.height) {
      draw_.FlagBridgeError();
      return false;
    }
    depth_cfg.enabled = true;
    depth_cfg.func = raster_.GetDepthFunc();
    depth_cfg.write_mask = raster_.GetDepthMask();
    depth_cfg.clear_depth = raster_.GetClearDepth();
  }
  bridge.ConfigureDepth(depth_cfg);
  if (bridge.GetError() != kGlNoError) {
    draw_.FlagBridgeError();
    return false;
  }
  if (!bridge.BeginFrame(fb_att.width, fb_att.height)) {
    draw_.FlagBridgeError();
    return false;
  }
  bridge.SetVertexBytes(interleaved.data(), interleaved.size(), 32);
  bridge.SetMVP(mvp);
  bridge.BeginRenderPass();
  bridge.Draw();
  bridge.EndRenderPass();
  if (!bridge.CommitFrame() || bridge.GetError() != kGlNoError) {
    draw_.FlagBridgeError();
    return false;
  }
  return true;
}

std::vector<std::string> GlesContext::ConformanceChecklist() const {
  std::vector<std::string> failures;
  const auto check = [&](bool ok, const char* name) {
    if (!ok) failures.emplace_back(name);
  };
  check(kCoreEntryPointCount == 358, "core-entry-points-358");
  check(kCoreEnableCapCount == 13, "enable-caps-13");
  check(kCoreQueryTargetCount == 4, "query-targets-4");
  check(kGlsl320VersionNumber == 320, "glsl-320");
  check(kSupportedGlslVersionCount == 4, "glsl-versions-4");
  check(kShaderStageCount == 6, "shader-stages-6");
  check(kMaxVertexAttribs >= 16, "max-vertex-attribs-16");
  check(kMaxCombinedTextureImageUnits >= 32, "combined-units-32");
  check(kMaxTextureImageUnits >= 16, "texture-units-16");
  check(kMaxUniformBufferBindings >= 72, "ubo-bindings-72");
  check(kMaxTransformFeedbackSeparateAttribs >= 4, "tf-attribs-4");
  check(kMaxDrawBuffers >= 4, "draw-buffers-4");
  check(kMaxColorAttachments >= 4, "color-attachments-4");
  check(kMaxSamplesValue >= 4, "max-samples-4");
  check(kMaxRenderbufferSizeValue >= 2048, "renderbuffer-size-2048");
  check(kMaxTextureSizeValue >= 2048, "texture-size-2048");
  check(kMaxCubeMapTextureSizeValue >= 2048, "cubemap-size-2048");
  check(kMax3dTextureSizeValue >= 256, "3d-size-256");
  check(kMaxArrayTextureLayersValue >= 256, "array-layers-256");
  check(kMaxTextureBufferSizeValue >= 65536, "texture-buffer-65536");
  check(kMaxPatchVertices >= 32, "patch-vertices-32");
  check(kMaxTessGenLevel >= 64, "tess-gen-level-64");
  check(kMaxGeometryOutputVertices >= 256, "geometry-output-256");
  check(kMaxComputeWorkGroupInvocations >= 128, "compute-invocations-128");
  check(kMaxComputeSharedMemorySize >= 16384, "compute-shared-16384");
  check(kEsMajorVersion == 3 && kEsMinorVersion == 2, "version-3-2");
  // ES 3.1+ host surface: image units, SSBO/UBO reflection, EXT storage.
  check(kMaxImageUnitsValue >= 8, "image-units-8");
  check(kEglProcNameCount == 46, "egl-proc-46");
  check(kCoreProcNameCount == 358, "gl-proc-358");
  // Backend rules that correct the old plan.
  check(metal::FindGpuFamily("Apple7") != nullptr, "family-apple7");
  check(metal::FindGpuFamily("Apple10") != nullptr, "family-apple10");
  check(metal::FindGpuFamily("Apple9")->supports_bc_compression,
        "bc-apple9-plus");
  check(!metal::FindGpuFamily("Apple8")->supports_bc_compression,
        "no-bc-apple8");
  check(msl::UsesVaryingsStruct(msl::CorrectedTriangleVertexShader()),
        "msl-varyings-struct");
  check(!msl::UsesInvalidThreadUserVarying(msl::CorrectedTriangleVertexShader()),
        "msl-no-thread-varying");
  return failures;
}

std::string GlesContext::VersionReport() const {
  std::ostringstream out;
  out << foundation_.GetStringForReport();
  return out.str();
}

std::vector<std::string> GlesContext::HostIntegrationGaps() {
  // See docs/vi/minecraft.md (and /en/). GLES state, EGL state machine,
  // Metal bridge (device-verified render + present) and app VAO/FBO wiring
  // (RenderFrame) are done. Remaining for a launcher running end-to-end on
  // an iPhone: broader draw-path coverage and an on-device validation run.
  return {"Extended RenderFrame draw coverage (indexed draws, non-float and "
          "non-interleaved attribs, MRT targets)",
          "On-device iOS (A-series) validation run + launcher stack (JVM, "
          "LWJGL natives)"};
}

}  // namespace tgles
