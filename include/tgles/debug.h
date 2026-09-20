#ifndef TGLES_DEBUG_H
#define TGLES_DEBUG_H

// Debug output (spec chapter 18, KHR_debug core in ES 3.2): message control,
// application markers, object labels and the message log.

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "tgles/error.h"
#include "tgles/gl_types.h"

namespace tgles {

// Debug sources.
inline constexpr GLenum kGlDebugSourceApi = 0x8246;
inline constexpr GLenum kGlDebugSourceWindowSystem = 0x8247;
inline constexpr GLenum kGlDebugSourceShaderCompiler = 0x8248;
inline constexpr GLenum kGlDebugSourceThirdParty = 0x8249;
inline constexpr GLenum kGlDebugSourceApplication = 0x824A;
inline constexpr GLenum kGlDebugSourceOther = 0x824B;
inline constexpr GLenum kGlDontCare = 0x1100;

// Debug types.
inline constexpr GLenum kGlDebugTypeError = 0x824C;
inline constexpr GLenum kGlDebugTypeDeprecatedBehavior = 0x824D;
inline constexpr GLenum kGlDebugTypeUndefinedBehavior = 0x824E;
inline constexpr GLenum kGlDebugTypePortability = 0x824F;
inline constexpr GLenum kGlDebugTypePerformance = 0x8250;
inline constexpr GLenum kGlDebugTypeOther = 0x8251;
inline constexpr GLenum kGlDebugTypeMarker = 0x8268;
inline constexpr GLenum kGlDebugTypePushGroup = 0x8269;
inline constexpr GLenum kGlDebugTypePopGroup = 0x826A;

// Debug severities.
inline constexpr GLenum kGlDebugSeverityHigh = 0x9146;
inline constexpr GLenum kGlDebugSeverityMedium = 0x9147;
inline constexpr GLenum kGlDebugSeverityLow = 0x9148;
inline constexpr GLenum kGlDebugSeverityNotification = 0x826B;

// Object identifiers for labels.
inline constexpr GLenum kGlBufferObject = 0x9151;
inline constexpr GLenum kGlShaderObject = 0x8B48;
inline constexpr GLenum kGlProgramObject = 0x8B40;
inline constexpr GLenum kGlVertexArrayObject = 0x9154;
inline constexpr GLenum kGlQueryObject = 0x9153;
inline constexpr GLenum kGlSamplerObject = 0x9155;
inline constexpr GLenum kGlTextureObject = 0x9152;
inline constexpr GLenum kGlRenderbufferObject = 0x9157;
inline constexpr GLenum kGlFramebufferObject = 0x8D40;
inline constexpr GLenum kGlProgramPipelineObject = 0x8A4F;
inline constexpr GLenum kGlTransformFeedbackObject = 0x8E22;

inline constexpr GLint kMaxLabelLength = 256;
inline constexpr GLint kMaxDebugGroupStackDepth = 64;
inline constexpr GLint kMaxDebugMessageLength = 1024;

struct DebugMessage {
  GLenum source = 0;
  GLenum type = 0;
  GLuint id = 0;
  GLenum severity = 0;
  std::string text;
};

class DebugManager {
 public:
  DebugManager();

  GLenum GetError();
  bool HasPending() const;

  void DebugMessageControl(GLenum source, GLenum type, GLenum severity,
                           GLsizei count, const GLuint* ids, GLboolean enabled);
  void DebugMessageInsert(GLenum source, GLenum type, GLuint id,
                          GLenum severity, GLsizei length, const char* message);
  void ObjectLabel(GLenum identifier, GLuint name, GLsizei length,
                   const char* label);
  std::string GetObjectLabel(GLenum identifier, GLuint name);
  void PushDebugGroup(GLenum source, GLuint id, GLsizei length,
                      const char* message);
  void PopDebugGroup();
  GLuint GetDebugMessageLog(GLuint count, GLsizei buf_size, GLenum* sources,
                            GLenum* types, GLuint* ids, GLenum* severities,
                            GLsizei* lengths, char* message_log);

  // Test helpers.
  std::size_t PendingMessages() const;
  std::size_t GroupDepth() const;

 private:
  static bool IsSource(GLenum source);
  static bool IsType(GLenum type);
  static bool IsSeverity(GLenum severity);
  static bool IsIdentifier(GLenum identifier);

  ErrorQueue errors_;
  std::deque<DebugMessage> log_;
  std::vector<std::string> group_stack_;
  std::map<std::pair<GLenum, GLuint>, std::string> labels_;
};

}  // namespace tgles

#endif  // TGLES_DEBUG_H