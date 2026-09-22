#ifndef TGLES_SAMPLER_H
#define TGLES_SAMPLER_H

// Sampler objects (spec 8.2): independent sampling state bound to texture
// units, overriding texture parameters while bound.

#include <array>
#include <map>

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"
#include "tgles/state/texture.h"  // Parameter enums + unit limit.

namespace tgles {

struct SamplerParams {
  GLenum min_filter = kGlNearestMipmapLinear;
  GLenum mag_filter = kGlLinear;
  GLenum wrap_s = kGlClampToEdge;
  GLenum wrap_t = kGlClampToEdge;
  GLenum wrap_r = kGlClampToEdge;
  GLfloat min_lod = -1000.0f;
  GLfloat max_lod = 1000.0f;
  GLenum compare_mode = kGlNone;
  GLenum compare_func = 0x0202;  // LEQUAL.
  GLfloat border_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

class SamplerManager {
 public:
  SamplerManager();

  GLenum GetError();
  bool HasPending() const;

  void GenSamplers(GLsizei n, GLuint* samplers);
  void DeleteSamplers(GLsizei n, const GLuint* samplers);
  GLboolean IsSampler(GLuint sampler);
  void BindSampler(GLuint unit, GLuint sampler);
  void SamplerParameteri(GLuint sampler, GLenum pname, GLint param);
  void SamplerParameterf(GLuint sampler, GLenum pname, GLfloat param);
  void SamplerParameterfv(GLuint sampler, GLenum pname,
                          const GLfloat* params);
  void SamplerParameteriv(GLuint sampler, GLenum pname, const GLint* params);
  void GetSamplerParameteriv(GLuint sampler, GLenum pname, GLint* params);
  void GetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat* params);
  void SamplerParameterIiv(GLuint sampler, GLenum pname, const GLint* params);
  void SamplerParameterIuiv(GLuint sampler, GLenum pname, const GLuint* params);
  // Integer reads mirror GetTexParameterIiv (same float-family rule).
  void GetSamplerParameterIiv(GLuint sampler, GLenum pname, GLint* params);
  void GetSamplerParameterIuiv(GLuint sampler, GLenum pname, GLuint* params);

  GLuint BoundSampler(GLuint unit) const;

 private:
  struct Sampler {
    bool alive = false;
    SamplerParams params;
  };

  Sampler* Find(GLuint sampler);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Sampler> samplers_;
  std::array<GLuint, kMaxCombinedTextureImageUnits> unit_bindings_{};
};

}  // namespace tgles

#endif  // TGLES_SAMPLER_H
