#include "tgles/state/debug.h"

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

bool DebugManager::MessageEnabled(GLenum source, GLenum type,
                                       GLenum severity, GLuint id) const {
  for (auto it = rules_.rbegin(); it != rules_.rend(); ++it) {
    const bool source_hit =
        it->source == kGlDontCare || it->source == source;
    const bool type_hit = it->type == kGlDontCare || it->type == type;
    const bool severity_hit =
        it->severity == kGlDontCare || it->severity == severity;
    if (!source_hit || !type_hit || !severity_hit) continue;
    if (!it->ids.empty()) {
      bool id_hit = false;
      for (GLuint listed : it->ids) {
        if (listed == id) {
          id_hit = true;
          break;
        }
      }
      if (!id_hit) continue;
    }
    return it->enabled;
  }
  return true;
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
  ControlRule rule;
  rule.source = source;
  rule.type = type;
  rule.severity = severity;
  rule.enabled = (enabled != kGlFalse);
  for (GLsizei i = 0; i < count; ++i) rule.ids.push_back(ids[i]);
  rules_.push_back(rule);
}

void DebugManager::SetCallback(void* callback, const void* user_param) {
  callback_ = callback;
  callback_user_param_ = user_param;
}

void* DebugManager::Callback() const { return callback_; }

const void* DebugManager::CallbackUserParam() const {
  return callback_user_param_;
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
  if (!MessageEnabled(source, type, severity, id)) return;  // Filtered out.
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

void DebugManager::ObjectPtrLabel(const void* ptr, GLsizei length,
                                  const char* label) {
  if (ptr == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (length >= kMaxLabelLength || (length < 0 && label == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  std::string text;
  if (label != nullptr) {
    text = (length < 0)
               ? label
               : std::string(label, static_cast<std::size_t>(length));
  }
  ptr_labels_[ptr] = text;
}

std::string DebugManager::GetObjectPtrLabel(const void* ptr) {
  auto it = ptr_labels_.find(ptr);
  return it == ptr_labels_.end() ? std::string() : it->second;
}

void DebugManager::GetPointerv(GLenum pname, void** params) {
  static constexpr GLenum kCallbackFn = 0x8244u;
  static constexpr GLenum kCallbackParam = 0x8245u;
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname == kCallbackFn) {
    *params = callback_;
    return;
  }
  if (pname == kCallbackParam) {
    *params = const_cast<void*>(callback_user_param_);
    return;
  }
  errors_.Record(kGlInvalidEnum);
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
  if (!MessageEnabled(source, kGlDebugTypePushGroup,
                      kGlDebugSeverityNotification, id)) {
    return;  // The group exists; only its log message is filtered.
  }
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
  if (!MessageEnabled(kGlDebugSourceApplication, kGlDebugTypePopGroup,
                      kGlDebugSeverityNotification, 0)) {
    return;
  }
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
