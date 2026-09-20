#ifndef TGLES_SYNC_H
#define TGLES_SYNC_H

// Sync objects and fences (spec 4.1): creation, CPU-side wait simulation
// and status queries. GPU signaling is modeled by SignalSync (the Metal
// backend calls it when the command buffer completes, see step 9).

#include <map>

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

inline constexpr GLenum kGlSyncGpuCommandsComplete = 0x9117;
inline constexpr GLbitfield kGlSyncFlushCommandsBit = 0x00000001;
inline constexpr GLuint64 kGlTimeoutIgnored = 0xFFFFFFFFFFFFFFFFull;

inline constexpr GLenum kGlAlreadySignaled = 0x911A;
inline constexpr GLenum kGlTimeoutExpired = 0x911B;
inline constexpr GLenum kGlConditionSatisfied = 0x911C;
inline constexpr GLenum kGlWaitFailed = 0x911D;

inline constexpr GLenum kGlSyncStatus = 0x9114;
inline constexpr GLenum kGlSyncCondition = 0x9113;
inline constexpr GLenum kGlSyncFlags = 0x9115;
inline constexpr GLenum kGlSignaled = 0x9119;
inline constexpr GLenum kGlUnsignaled = 0x9118;

inline constexpr GLenum kGlMaxServerWaitTimeout = 0x9111;

class SyncManager {
 public:
  SyncManager();

  GLenum GetError();
  bool HasPending() const;

  GLuint FenceSync(GLenum condition, GLbitfield flags);
  GLboolean IsSync(GLuint sync);
  void DeleteSync(GLuint sync);
  GLenum ClientWaitSync(GLuint sync, GLbitfield flags, GLuint64 timeout);
  void WaitSync(GLuint sync, GLbitfield flags, GLuint64 timeout);
  void GetSynciv(GLuint sync, GLenum pname, GLsizei count, GLsizei* length,
                 GLint* values);
  void GetInteger64v(GLenum pname, GLint64* params);

  // Test/backend helper: pretend the GPU finished the sync's commands.
  void SignalSync(GLuint sync);

 private:
  struct Sync {
    bool alive = false;
    bool signaled = false;
  };

  Sync* Find(GLuint sync);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Sync> syncs_;
};

}  // namespace tgles

#endif  // TGLES_SYNC_H
