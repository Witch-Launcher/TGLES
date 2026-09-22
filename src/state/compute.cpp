// Compute stage state (spec ch.19): program binding, dispatches and barriers.
// Mirrors include/tgles/state/compute.h.

#include "tgles/state/compute.h"

namespace tgles {

ComputeState::ComputeState() = default;

GLenum ComputeState::GetError() { return errors_.Get(); }
bool ComputeState::HasPending() const { return errors_.HasPending(); }

void ComputeState::SetComputeProgramBound(bool bound) {
  compute_program_bound_ = bound;
}

void ComputeState::DispatchCompute(GLuint groups_x, GLuint groups_y,
                                   GLuint groups_z) {
  if (!compute_program_bound_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const GLuint groups[3] = {groups_x, groups_y, groups_z};
  for (int i = 0; i < 3; ++i) {
    if (groups[i] > static_cast<GLuint>(kMaxComputeWorkGroupCount[i])) {
      errors_.Record(kGlInvalidValue);
      return;
    }
  }
  // Zero in any dimension means "dispatch nothing" (spec); still recorded.
  last_.x = groups_x;
  last_.y = groups_y;
  last_.z = groups_z;
}

void ComputeState::DispatchComputeIndirect(GLintptr indirect) {
  if (!compute_program_bound_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (indirect < 0 || (indirect % 4) != 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
}

bool ComputeState::ValidBarrierBits(GLbitfield barriers) {
  constexpr GLbitfield kKnown =
      kGlVertexAttribArrayBarrierBit | kGlElementArrayBarrierBit |
      kGlUniformBarrierBit | kGlTextureFetchBarrierBit |
      kGlShaderImageAccessBarrierBit | kGlCommandBarrierBit |
      kGlPixelBufferBarrierBit | kGlTextureUpdateBarrierBit |
      kGlBufferUpdateBarrierBit | kGlFramebufferBarrierBit |
      kGlTransformFeedbackBarrierBit | kGlAtomicCounterBarrierBit |
      kGlShaderStorageBarrierBit | kGlClientMappedBufferBarrierBit |
      kGlQueryBufferBarrierBit;
  return (barriers & ~kKnown) == 0 || barriers == kGlAllBarrierBits;
}

void ComputeState::MemoryBarrier(GLbitfield barriers) {
  if (!ValidBarrierBits(barriers)) {
    errors_.Record(kGlInvalidValue);
  }
}

void ComputeState::MemoryBarrierByRegion(GLbitfield barriers) {
  if (!ValidBarrierBits(barriers)) {
    errors_.Record(kGlInvalidValue);
  }
}

void ComputeState::GetIntegeri_v(GLenum pname, GLuint index, GLint* data) {
  if (data == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname == kGlMaxComputeWorkGroupCount) {
    if (index > 2) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    *data = kMaxComputeWorkGroupCount[index];
    return;
  }
  if (pname == kGlMaxComputeWorkGroupSize) {
    if (index > 2) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    *data = kMaxComputeWorkGroupSize[index];
    return;
  }
  errors_.Record(kGlInvalidEnum);
}

ComputeState::Dispatch ComputeState::LastDispatch() const { return last_; }

}  // namespace tgles
