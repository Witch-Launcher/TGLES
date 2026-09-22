#ifndef TGLES_TRANSFORM_FEEDBACK_H
#define TGLES_TRANSFORM_FEEDBACK_H

// Transform-feedback objects and capture spans (spec chapter 12):
// Gen/Bind/Begin/End/Pause/Resume with the deferred-capture model
// MobileGL's XfbImpl relies on (begin deferred to first draw of the span).

#include <map>

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"

namespace tgles {

inline constexpr GLenum kGlTransformFeedback = 0x8E22;

class TransformFeedbackManager {
 public:
  TransformFeedbackManager();

  GLenum GetError();
  bool HasPending() const;

  void GenTransformFeedbacks(GLsizei n, GLuint* ids);
  void DeleteTransformFeedbacks(GLsizei n, const GLuint* ids);
  GLboolean IsTransformFeedback(GLuint id);
  void BindTransformFeedback(GLenum target, GLuint id);
  void BeginTransformFeedback(GLenum primitive_mode);
  void EndTransformFeedback();
  void PauseTransformFeedback();
  void ResumeTransformFeedback();

  // Test helpers.
  bool IsActive() const;
  bool IsPaused() const;
  GLuint BoundId() const;

 private:
  static bool IsCapturePrimitive(GLenum mode);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, bool> objects_;  // id -> alive (0 = default, always alive).
  GLuint bound_ = 0;
  bool active_ = false;
  bool paused_ = false;
};

}  // namespace tgles

#endif  // TGLES_TRANSFORM_FEEDBACK_H
