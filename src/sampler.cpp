#include "tgles/sampler.h"

namespace tgles {

SamplerManager::SamplerManager() { unit_bindings_.fill(0); }

GLenum SamplerManager::GetError() { return errors_.Get(); }
bool SamplerManager::HasPending() const { return errors_.HasPending(); }

SamplerManager::Sampler* SamplerManager::Find(GLuint sampler) {
  auto it = samplers_.find(sampler);
  if (it == samplers_.end() || !it->second.alive) return nullptr;
  return &it->second;
}

void SamplerManager::GenSamplers(GLsizei n, GLuint* samplers) {
  if (n < 0 || (n > 0 && samplers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    samplers[i] = next_name_++;
    samplers_[samplers[i]] = Sampler();
  }
}

void SamplerManager::DeleteSamplers(GLsizei n, const GLuint* samplers) {
  if (n < 0 || (n > 0 && samplers == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (samplers[i] == 0) continue;
    samplers_.erase(samplers[i]);
    for (auto& bound : unit_bindings_) {
      if (bound == samplers[i]) bound = 0;
    }
  }
}

GLboolean SamplerManager::IsSampler(GLuint sampler) {
  if (sampler == 0) return kGlFalse;
  return Find(sampler) != nullptr ? kGlTrue : kGlFalse;
}

void SamplerManager::BindSampler(GLuint unit, GLuint sampler) {
  if (unit >= static_cast<GLuint>(kMaxCombinedTextureImageUnits)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (sampler != 0) {
    auto it = samplers_.find(sampler);
    if (it == samplers_.end()) {
      errors_.Record(kGlInvalidOperation);  // Never generated.
      return;
    }
    it->second.alive = true;  // First bind creates the object.
  }
  unit_bindings_[unit] = sampler;
}

void SamplerManager::SamplerParameteri(GLuint sampler, GLenum pname,
                                       GLint param) {
  Sampler* s = Find(sampler);
  if (s == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  SamplerParams& p = s->params;
  switch (pname) {
    case kGlTextureMinFilter:
      switch (param) {
        case kGlNearest:
        case kGlLinear:
        case kGlNearestMipmapNearest:
        case kGlLinearMipmapNearest:
        case kGlNearestMipmapLinear:
        case kGlLinearMipmapLinear:
          p.min_filter = static_cast<GLenum>(param);
          return;
        default:
          break;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    case kGlTextureMagFilter:
      if (param == static_cast<GLint>(kGlNearest) ||
          param == static_cast<GLint>(kGlLinear)) {
        p.mag_filter = static_cast<GLenum>(param);
        return;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    case kGlTextureWrapS:
    case kGlTextureWrapT:
    case kGlTextureWrapR:
      if (param == static_cast<GLint>(kGlClampToEdge) ||
          param == static_cast<GLint>(kGlRepeat) ||
          param == static_cast<GLint>(kGlMirroredRepeat) ||
          param == static_cast<GLint>(kGlClampToBorder)) {
        if (pname == kGlTextureWrapS) p.wrap_s = static_cast<GLenum>(param);
        if (pname == kGlTextureWrapT) p.wrap_t = static_cast<GLenum>(param);
        if (pname == kGlTextureWrapR) p.wrap_r = static_cast<GLenum>(param);
        return;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    case kGlTextureCompareMode:
      if (param == static_cast<GLint>(kGlNone) ||
          param == static_cast<GLint>(kGlCompareRefToTexture)) {
        p.compare_mode = static_cast<GLenum>(param);
        return;
      }
      errors_.Record(kGlInvalidEnum);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

void SamplerManager::SamplerParameterf(GLuint sampler, GLenum pname,
                                       GLfloat param) {
  Sampler* s = Find(sampler);
  if (s == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (pname == kGlTextureMinLod) {
    s->params.min_lod = param;
    return;
  }
  if (pname == kGlTextureMaxLod) {
    s->params.max_lod = param;
    return;
  }
  SamplerParameteri(sampler, pname, static_cast<GLint>(param));
}

void SamplerManager::SamplerParameterfv(GLuint sampler, GLenum pname,
                                         const GLfloat* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Sampler* s = Find(sampler);
  if (s == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (pname == kGlTextureBorderColor) {
    for (int i = 0; i < 4; ++i) s->params.border_color[i] = params[i];
    return;
  }
  SamplerParameterf(sampler, pname, params[0]);
}

void SamplerManager::GetSamplerParameterfv(GLuint sampler, GLenum pname,
                                           GLfloat* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Sampler* s = Find(sampler);
  if (s == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (pname == kGlTextureBorderColor) {
    for (int i = 0; i < 4; ++i) params[i] = s->params.border_color[i];
    return;
  }
  if (pname == kGlTextureMinLod) {
    params[0] = s->params.min_lod;
    return;
  }
  if (pname == kGlTextureMaxLod) {
    params[0] = s->params.max_lod;
    return;
  }
  GLint v = 0;
  GetSamplerParameteriv(sampler, pname, &v);
  if (HasPending()) return;
  params[0] = static_cast<GLfloat>(v);
}

void SamplerManager::GetSamplerParameteriv(GLuint sampler, GLenum pname,
                                           GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Sampler* s = Find(sampler);
  if (s == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  switch (pname) {
    case kGlTextureMinFilter:
      *params = static_cast<GLint>(s->params.min_filter);
      return;
    case kGlTextureMagFilter:
      *params = static_cast<GLint>(s->params.mag_filter);
      return;
    case kGlTextureWrapS:
      *params = static_cast<GLint>(s->params.wrap_s);
      return;
    case kGlTextureWrapT:
      *params = static_cast<GLint>(s->params.wrap_t);
      return;
    case kGlTextureWrapR:
      *params = static_cast<GLint>(s->params.wrap_r);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

GLuint SamplerManager::BoundSampler(GLuint unit) const {
  return unit < unit_bindings_.size() ? unit_bindings_[unit] : 0;
}

}  // namespace tgles
