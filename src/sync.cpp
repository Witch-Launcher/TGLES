#include "tgles/sync.h"

namespace tgles {

SyncManager::SyncManager() = default;

GLenum SyncManager::GetError() { return errors_.Get(); }
bool SyncManager::HasPending() const { return errors_.HasPending(); }

SyncManager::Sync* SyncManager::Find(GLuint sync) {
  auto it = syncs_.find(sync);
  if (it == syncs_.end() || !it->second.alive) return nullptr;
  return &it->second;
}

GLuint SyncManager::FenceSync(GLenum condition, GLbitfield flags) {
  if (condition != kGlSyncGpuCommandsComplete) {
    errors_.Record(kGlInvalidEnum);
    return 0;
  }
  if (flags != 0) {
    errors_.Record(kGlInvalidValue);
    return 0;
  }
  const GLuint name = next_name_++;
  syncs_[name] = Sync();
  syncs_[name].alive = true;
  return name;
}

GLboolean SyncManager::IsSync(GLuint sync) {
  if (sync == 0) return kGlFalse;
  return Find(sync) != nullptr ? kGlTrue : kGlFalse;
}

void SyncManager::DeleteSync(GLuint sync) {
  if (sync == 0) return;  // Zero is silently ignored (spec 4.1).
  auto it = syncs_.find(sync);
  if (it == syncs_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  it->second.alive = false;
}

GLenum SyncManager::ClientWaitSync(GLuint sync, GLbitfield flags,
                                   GLuint64 timeout) {
  Sync* s = Find(sync);
  if (s == nullptr) {
    errors_.Record(kGlInvalidValue);  // Verified against spec 4.1.1.
    return kGlWaitFailed;
  }
  if ((flags & ~kGlSyncFlushCommandsBit) != 0) {
    errors_.Record(kGlInvalidValue);
    return kGlWaitFailed;
  }
  if (s->signaled) return kGlAlreadySignaled;
  if (timeout == 0) return kGlTimeoutExpired;
  // No GPU runs on the host; a finite wait always expires. The Metal
  // backend (step 9) signals before waiting whenever work completed.
  return kGlTimeoutExpired;
}

void SyncManager::WaitSync(GLuint sync, GLbitfield flags, GLuint64 timeout) {
  if (Find(sync) == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (flags != 0 || timeout != kGlTimeoutIgnored) {
    errors_.Record(kGlInvalidValue);
  }
}

void SyncManager::GetSynciv(GLuint sync, GLenum pname, GLsizei count,
                            GLsizei* length, GLint* values) {
  Sync* s = Find(sync);
  if (s == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (count < 1 || values == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  switch (pname) {
    case kGlSyncStatus:
      values[0] = s->signaled ? static_cast<GLint>(kGlSignaled)
                              : static_cast<GLint>(kGlUnsignaled);
      break;
    case kGlSyncCondition:
      values[0] = static_cast<GLint>(kGlSyncGpuCommandsComplete);
      break;
    case kGlSyncFlags:
      values[0] = 0;
      break;
    default:
      errors_.Record(kGlInvalidEnum);
      return;
  }
  if (length != nullptr) *length = 1;
}

void SyncManager::GetInteger64v(GLenum pname, GLint64* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname == kGlMaxServerWaitTimeout) {
    *params = 1000000000LL;  // 1 second in nanoseconds.
    return;
  }
  errors_.Record(kGlInvalidEnum);
}

void SyncManager::SignalSync(GLuint sync) {
  Sync* s = Find(sync);
  if (s != nullptr) s->signaled = true;
}

}  // namespace tgles
