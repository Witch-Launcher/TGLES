#ifndef TGLES_ERROR_H
#define TGLES_ERROR_H

// Error-queue semantics from spec section 2.3.1 (Command Execution):
// - The first detected error is latched; later errors do not replace it.
// - GetError returns the latched code and clears it.
// - A failing command (except OUT_OF_MEMORY) has no side effects, returns
//   zero when it returns a value, and never writes through pointer args.

#include "tgles/base/gl_types.h"

namespace tgles {

class ErrorQueue {
 public:
  ErrorQueue();

  // Latch an error unless one is already pending or code is NO_ERROR.
  void Record(GLenum code);

  // Return the pending code (or NO_ERROR) and clear the latch.
  GLenum Get();

  // True while an unread error is pending.
  bool HasPending() const;

 private:
  GLenum pending_;
};

}  // namespace tgles

#endif  // TGLES_ERROR_H