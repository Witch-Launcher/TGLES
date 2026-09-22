#include "tgles/state/query.h"

namespace tgles {

QueryManager::QueryManager() {
  active_[QueryTargetEnum(QueryTarget::kPrimitivesGenerated)] = 0;
  active_[QueryTargetEnum(QueryTarget::kTransformFeedbackPrimitivesWritten)] =
      0;
  active_[QueryTargetEnum(QueryTarget::kAnySamplesPassed)] = 0;
  active_[QueryTargetEnum(QueryTarget::kAnySamplesPassedConservative)] = 0;
}

GLenum QueryManager::GetError() { return errors_.Get(); }
bool QueryManager::HasPending() const { return errors_.HasPending(); }

bool QueryManager::IsCoreTarget(GLenum target) const {
  return active_.find(target) != active_.end();
}

QueryManager::Query* QueryManager::Find(GLuint id) {
  auto it = queries_.find(id);
  if (it == queries_.end() || !it->second.alive) return nullptr;
  return &it->second;
}

void QueryManager::GenQueries(GLsizei n, GLuint* ids) {
  if (n < 0 || (n > 0 && ids == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    ids[i] = next_name_++;
    queries_[ids[i]] = Query();
  }
}

void QueryManager::DeleteQueries(GLsizei n, const GLuint* ids) {
  if (n < 0 || (n > 0 && ids == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (ids[i] == 0) continue;
    auto it = queries_.find(ids[i]);
    if (it == queries_.end() || !it->second.alive) continue;
    // Ending an active query by deletion is allowed; just drop it.
    for (auto& kv : active_) {
      if (kv.second == ids[i]) kv.second = 0;
    }
    queries_.erase(it);
  }
}

GLboolean QueryManager::IsQuery(GLuint id) {
  if (id == 0) return kGlFalse;
  return Find(id) != nullptr ? kGlTrue : kGlFalse;
}

void QueryManager::BeginQuery(GLenum target, GLuint id) {
  if (!IsCoreTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (id == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  auto it = queries_.find(id);
  if (it == queries_.end()) {
    errors_.Record(kGlInvalidOperation);  // Never generated.
    return;
  }
  Query* q = &it->second;
  q->alive = true;  // First Begin creates the object.
  if (active_[target] != 0) {
    errors_.Record(kGlInvalidOperation);  // One active query per type.
    return;
  }
  // Occlusion targets share a slot family (either may be reused).
  q->target = target;
  q->result_available = false;
  q->result = 0;
  active_[target] = id;
}

void QueryManager::EndQuery(GLenum target) {
  if (!IsCoreTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (active_[target] == 0) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  active_[target] = 0;
}

void QueryManager::GetQueryiv(GLenum target, GLenum pname, GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (!IsCoreTarget(target)) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (pname != kGlCurrentQuery) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  *params = static_cast<GLint>(active_[target]);
}

void QueryManager::GetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Query* q = Find(id);
  if (q == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  switch (pname) {
    case kGlQueryResult:
      *params = q->result;
      return;
    case kGlQueryResultAvailable:
      *params = q->result_available ? 1u : 0u;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void QueryManager::QueryCounterEXT(GLuint id, GLenum target) {
  static constexpr GLenum kTimestamp = 0x8E28u;
  static constexpr GLenum kElapsed = 0x88BFu;
  if (target != kTimestamp && target != kElapsed) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  auto it = queries_.find(id);
  if (it == queries_.end()) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  Query* q = &it->second;
  q->alive = true;
  q->target = target;
  // CPU model: timestamp completes immediately with 0.
  q->result = 0;
  q->result_available = true;
}

void QueryManager::GetQueryObjectivEXT(GLuint id, GLenum pname, GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Query* q = Find(id);
  if (q == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  switch (pname) {
    case kGlQueryResult:
      *params = static_cast<GLint>(q->result);
      return;
    case kGlQueryResultAvailable:
      *params = q->result_available ? 1 : 0;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void QueryManager::GetQueryObjecti64vEXT(GLuint id, GLenum pname,
                                         GLint64* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Query* q = Find(id);
  if (q == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  switch (pname) {
    case kGlQueryResult:
      *params = static_cast<GLint64>(q->result);
      return;
    case kGlQueryResultAvailable:
      *params = q->result_available ? 1 : 0;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void QueryManager::GetQueryObjectui64vEXT(GLuint id, GLenum pname,
                                          GLuint64* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Query* q = Find(id);
  if (q == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  switch (pname) {
    case kGlQueryResult:
      *params = static_cast<GLuint64>(q->result);
      return;
    case kGlQueryResultAvailable:
      *params = q->result_available ? 1u : 0u;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void QueryManager::CompleteQuery(GLuint id, GLuint result) {
  Query* q = Find(id);
  if (q == nullptr) return;
  q->result = result;
  q->result_available = true;
}

}  // namespace tgles
