#include "tgles/transform_feedback.h"

namespace tgles {

TransformFeedbackManager::TransformFeedbackManager() { objects_[0] = true; }

GLenum TransformFeedbackManager::GetError() { return errors_.Get(); }
bool TransformFeedbackManager::HasPending() const {
  return errors_.HasPending();
}

bool TransformFeedbackManager::IsCapturePrimitive(GLenum mode) {
  return mode == 0x0000 ||  // POINTS
         mode == 0x0001 ||  // LINES
         mode == 0x0004;    // TRIANGLES
}

void TransformFeedbackManager::GenTransformFeedbacks(GLsizei n, GLuint* ids) {
  if (n < 0 || (n > 0 && ids == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    ids[i] = next_name_++;
    objects_[ids[i]] = false;  // Reserved; created on first bind.
  }
}

void TransformFeedbackManager::DeleteTransformFeedbacks(GLsizei n,
                                                        const GLuint* ids) {
  if (n < 0 || (n > 0 && ids == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (ids[i] == 0) continue;
    if (bound_ == ids[i] && active_) {
      errors_.Record(kGlInvalidOperation);  // Cannot delete while capturing.
      continue;
    }
    objects_.erase(ids[i]);
    if (bound_ == ids[i]) bound_ = 0;
  }
}

GLboolean TransformFeedbackManager::IsTransformFeedback(GLuint id) {
  if (id == 0) return kGlFalse;
  auto it = objects_.find(id);
  return (it != objects_.end() && it->second) ? kGlTrue : kGlFalse;
}

void TransformFeedbackManager::BindTransformFeedback(GLenum target, GLuint id) {
  if (target != kGlTransformFeedback) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (active_) {
    errors_.Record(kGlInvalidOperation);  // No rebinding mid-capture.
    return;
  }
  auto it = objects_.find(id);
  if (id != 0 && it == objects_.end()) {
    objects_[id] = true;  // Binding a fresh name creates the object.
  } else if (it != objects_.end()) {
    it->second = true;
  }
  bound_ = id;
}

void TransformFeedbackManager::BeginTransformFeedback(GLenum primitive_mode) {
  if (!IsCapturePrimitive(primitive_mode)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (active_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  active_ = true;
  paused_ = false;
}

void TransformFeedbackManager::EndTransformFeedback() {
  if (!active_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  active_ = false;
  paused_ = false;
}

void TransformFeedbackManager::PauseTransformFeedback() {
  if (!active_ || paused_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  paused_ = true;
}

void TransformFeedbackManager::ResumeTransformFeedback() {
  if (!active_ || !paused_) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  paused_ = false;
}

bool TransformFeedbackManager::IsActive() const { return active_; }
bool TransformFeedbackManager::IsPaused() const { return paused_; }
GLuint TransformFeedbackManager::BoundId() const { return bound_; }

}  // namespace tgles
