#ifndef TGLES_COMPUTE_H
#define TGLES_COMPUTE_H

// Compute dispatch (spec compute chapter): work-group limits, dispatch
// validation and memory-barrier bit checking.

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

// Memory barrier bits (from gl32.h).
inline constexpr GLbitfield kGlVertexAttribArrayBarrierBit = 0x00000001;
inline constexpr GLbitfield kGlElementArrayBarrierBit = 0x00000002;
inline constexpr GLbitfield kGlUniformBarrierBit = 0x00000004;
inline constexpr GLbitfield kGlTextureFetchBarrierBit = 0x00000008;
inline constexpr GLbitfield kGlShaderImageAccessBarrierBit = 0x00000020;
inline constexpr GLbitfield kGlCommandBarrierBit = 0x00000040;
inline constexpr GLbitfield kGlPixelBufferBarrierBit = 0x00000080;
inline constexpr GLbitfield kGlTextureUpdateBarrierBit = 0x00000100;
inline constexpr GLbitfield kGlBufferUpdateBarrierBit = 0x00000200;
inline constexpr GLbitfield kGlFramebufferBarrierBit = 0x00000400;
inline constexpr GLbitfield kGlTransformFeedbackBarrierBit = 0x00000800;
inline constexpr GLbitfield kGlAtomicCounterBarrierBit = 0x00001000;
inline constexpr GLbitfield kGlShaderStorageBarrierBit = 0x00002000;
inline constexpr GLbitfield kGlClientMappedBufferBarrierBit = 0x00004000;
inline constexpr GLbitfield kGlQueryBufferBarrierBit = 0x00008000;
inline constexpr GLbitfield kGlAllBarrierBits = 0xFFFFFFFF;

// Indexed limit queries.
inline constexpr GLenum kGlMaxComputeWorkGroupCount = 0x91BE;
inline constexpr GLenum kGlMaxComputeWorkGroupSize = 0x91BF;

// Work-group limits (all equal to the spec minimums).
inline constexpr GLint kMaxComputeWorkGroupCount[3] = {65535, 65535, 65535};
inline constexpr GLint kMaxComputeWorkGroupSize[3] = {1024, 1024, 64};
inline constexpr GLint kMaxComputeWorkGroupInvocations = 128;
inline constexpr GLint kMaxComputeSharedMemorySize = 16384;  // Spec minimum.

class ComputeState {
 public:
  ComputeState();

  GLenum GetError();
  bool HasPending() const;

  // Tells the dispatcher whether a compute-stage program is active.
  // (Wired to ProgramManager by the facade in step 10.)
  void SetComputeProgramBound(bool bound);

  void DispatchCompute(GLuint groups_x, GLuint groups_y, GLuint groups_z);
  void DispatchComputeIndirect(GLintptr indirect);
  void MemoryBarrier(GLbitfield barriers);
  void MemoryBarrierByRegion(GLbitfield barriers);
  void GetIntegeri_v(GLenum pname, GLuint index, GLint* data);

  struct Dispatch {
    GLuint x = 0;
    GLuint y = 0;
    GLuint z = 0;
  };
  Dispatch LastDispatch() const;

 private:
  static bool ValidBarrierBits(GLbitfield barriers);

  ErrorQueue errors_;
  bool compute_program_bound_ = false;
  Dispatch last_;
};

}  // namespace tgles

#endif  // TGLES_COMPUTE_H