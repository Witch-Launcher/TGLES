#ifndef TGLES_CONTEXT_H
#define TGLES_CONTEXT_H

// Minimal OpenGL ES 3.2 context: server state needed by Step 1.
// Spec references: 2.3.1 (errors), 15.1.7 + 18 (enable defaults),
// 20.2-20.3 (string and version queries).

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"

#include <string>

namespace tgles {

// Number of core enable/disable capabilities.
inline constexpr int kCoreEnableCapCount = 13;

// Creates a context. Pass debug=true for a KHR_debug debug context
// (DEBUG_OUTPUT initially enabled); otherwise it starts disabled.
class Context {
 public:
  static Context Create(bool debug = false);

  // State toggles. Unknown caps record INVALID_ENUM and change nothing.
  void Enable(GLenum cap);
  void Disable(GLenum cap);
  GLboolean IsEnabled(GLenum cap);

  // Error handling (spec 2.3.1).
  GLenum GetError();
  bool HasPending() const;

  // Server strings (spec 20.2). Unknown names record INVALID_ENUM and
  // return nullptr.
  const GLubyte* GetString(GLenum name) const;

  // Indexed extension strings (spec 20.3). Only EXTENSIONS is valid;
  // other names record INVALID_ENUM, out-of-range index records
  // INVALID_VALUE, both return nullptr.
  const GLubyte* GetStringi(GLenum name, GLuint index);

  // Numeric queries for the foundation subset. Unknown names record
  // INVALID_ENUM and leave *params untouched (spec: no pointer writes
  // on error). Null params records INVALID_VALUE.
  void GetIntegerv(GLenum pname, GLint* params);
  // Boolean/float mirrors of GetIntegerv (spec 20.2): the same pname table,
  // converted (nonzero -> GL_TRUE; exact float cast). Same error rules.
  void GetBooleanv(GLenum pname, GLboolean* params);
  void GetFloatv(GLenum pname, GLfloat* params);

  bool IsDebugContext() const;

  // One-line "VENDOR | RENDERER | VERSION | SL" report for logs/tests.
  std::string GetStringForReport() const;

 private:
  Context() = default;

  bool IsCoreEnableCap(GLenum cap) const;
  bool* MutableFlag(GLenum cap);

  ErrorQueue errors_;
  bool is_debug_ = false;

  // One bit per core capability.
  bool blend_ = false;
  bool cull_face_ = false;
  bool debug_output_ = false;
  bool debug_output_synchronous_ = false;
  bool depth_test_ = false;
  bool dither_ = true;  // Spec 15.1.7: dithering starts enabled.
  bool polygon_offset_fill_ = false;
  bool primitive_restart_fixed_index_ = false;
  bool rasterizer_discard_ = false;
  bool sample_alpha_to_coverage_ = false;
  bool sample_coverage_ = false;
  bool scissor_test_ = false;
  bool stencil_test_ = false;
};

}  // namespace tgles

#endif  // TGLES_CONTEXT_H