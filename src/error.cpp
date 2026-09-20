#include "tgles/error.h"

namespace tgles {

ErrorQueue::ErrorQueue() : pending_(kGlNoError) {}

void ErrorQueue::Record(GLenum code) {
  if (code == kGlNoError) return;
  // First error wins (spec 2.3.1); later errors are dropped.
  if (pending_ == kGlNoError) pending_ = code;
}

GLenum ErrorQueue::Get() {
  const GLenum code = pending_;
  pending_ = kGlNoError;
  return code;
}

bool ErrorQueue::HasPending() const { return pending_ != kGlNoError; }

}  // namespace tgles
