#include "tgles/debug.h"

#include <cstring>

namespace tgles {

DebugManager::DebugManager() = default;

GLenum DebugManager::GetError() { return errors_.Get(); }
bool DebugManager::HasPending() const { return errors_.HasPending(); }

bool DebugManager::IsSource(GLenum source) {
  switch (source) {
    case kGlDebugSourceApi:
    case kGlDebugSourceWindowSystem:
    case kGlDebugSourceShaderCompiler:
    case kGlDebugSourceThirdParty:
    case kGlDebugSourceApplication:
    case kGlDebugSourceOther:
    case kGlDontCare:
      return true;
    default:
      return false;
  }
}

bool DebugManager::IsType(GLenum type) {
  switch (type) {
    case kGlDebugTypeError:
    case kGlDebugTypeDeprecatedBehavior:
    case kGlDebugTypeUndefinedBehavior:
    case kGlDebugTypePortability:
    case kGlDebugTypePerformance:
    case kGlDebugTypeOther:
    case kGlDebugTypeMarker:
    case kGlDebugTypePushGroup:
    case kGlDebugTypePopGroup:
    case kGlDontCare:
      return true;
    default:
      return false;
  }
}

bool DebugManager::IsSeverity(GLenum severity) {
  switch (severity) {
    case kGlDebugSeverityHigh:
    case kGlDebugSeverityMedium:
    case kGlDebugSeverityLow:
    case kGlDebugSeverityNotification:
    case kGlDontCare:
      return true;
    default:
      return false;
  }
}

bool DebugManager::IsIdentifier(GLenum identifier) {
  switch (identifier) {
    case kGlBufferObject:
    case kGlShaderObject:
    case kGlProgramObject:
    case kGlVertexArrayObject:
    case kGlQueryObject:
    case kGlSamplerObject:
    case kGlTextureObject:
    case kGlRenderbufferObject:
    case kGlFramebufferObject:
    case kGlProgramPipelineObject:
    case kGlTransformFeedbackObject:
      return true;
    default:
      return false;
  }
}

void DebugManager::DebugMessageControl(GLenum source, GLenum type,
                                       GLenum severity, GLsizei count,
                                       const GLuint* ids, GLboolean enabled) {
  if (!IsSource(source) || !IsType(type) || !IsSeverity(severity)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (count < 0 || (count > 0 && ids == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  (void)enabled;  // Filtering state is honored by the log reader (step 10).
}

void DebugManager::DebugMessageInsert(GLenum source, GLenum type, GLuint id,
                                      GLenum severity, GLsizei length,
                                      const char* message) {
  if (source != kGlDebugSourceApplication &&
      source != kGlDebugSourceThirdParty) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (!IsType(type) || !IsSeverity(severity)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (length < 0 && message == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  DebugMessage m;
  m.source = source;
  m.type = type;
  m.id = id;
  m.severity = severity;
  if (message != nullptr) {
    m.text = (length < 0) ? message
                          : std::string(message, static_cast<std::size_t>(length));
  }
  log_.push_back(m);
}

void DebugManager::ObjectLabel(GLenum identifier, GLuint name, GLsizei length,
                               const char* label) {
  if (!IsIdentifier(identifier)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (length >= kMaxLabelLength || (length < 0 && label == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  std::string text;
  if (label != nullptr) {
    text = (length < 0) ? label
                        : std::string(label, static_cast<std::size_t>(length));
  }
  labels_[{identifier, name}] = text;
}

std::string DebugManager::GetObjectLabel(GLenum identifier, GLuint name) {
  auto it = labels_.find({identifier, name});
  return it == labels_.end() ? std::string() : it->second;
}

void DebugManager::PushDebugGroup(GLenum source, GLuint id, GLsizei length,
                                  const char* message) {
  if (source != kGlDebugSourceApplication &&
      source != kGlDebugSourceThirdParty) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (group_stack_.size() >= static_cast<std::size_t>(kMaxDebugGroupStackDepth)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  std::string text;
  if (message != nullptr) {
    text = (length < 0) ? message
                        : std::string(message, static_cast<std::size_t>(length));
  }
  group_stack_.push_back(text);
  DebugMessage m;
  m.source = source;
  m.type = kGlDebugTypePushGroup;
  m.id = id;
  m.severity = kGlDebugSeverityNotification;
  m.text = text;
  log_.push_back(m);
}

void DebugManager::PopDebugGroup() {
  if (group_stack_.empty()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  group_stack_.pop_back();
  DebugMessage m;
  m.source = kGlDebugSourceApplication;
  m.type = kGlDebugTypePopGroup;
  m.severity = kGlDebugSeverityNotification;
  log_.push_back(m);
}

GLuint DebugManager::GetDebugMessageLog(GLuint count, GLsizei buf_size,
                                        GLenum* sources, GLenum* types,
                                        GLuint* ids, GLenum* severities,
                                        GLsizei* lengths, char* message_log) {
  GLuint fetched = 0;
  std::size_t written = 0;
  while (fetched < count && !log_.empty()) {
    const DebugMessage& m = log_.front();
    if (sources != nullptr) sources[fetched] = m.source;
    if (types != nullptr) types[fetched] = m.type;
    if (ids != nullptr) ids[fetched] = m.id;
    if (severities != nullptr) severities[fetched] = m.severity;
    if (lengths != nullptr) lengths[fetched] = static_cast<GLsizei>(m.text.size());
    if (message_log != nullptr && buf_size > 0 &&
        written < static_cast<std::size_t>(buf_size)) {
      const std::size_t room = static_cast<std::size_t>(buf_size) - written;
      const std::size_t n = std::min(room - 1, m.text.size());
      std::memcpy(message_log + written, m.text.data(), n);
      written += n;
      message_log[written] = '\0';
      ++written;
    }
    log_.pop_front();
    ++fetched;
  }
  return fetched;
}

std::size_t DebugManager::PendingMessages() const { return log_.size(); }
std::size_t DebugManager::GroupDepth() const { return group_stack_.size(); }

}  // namespace tgles
