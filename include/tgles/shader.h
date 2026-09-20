#ifndef TGLES_SHADER_H
#define TGLES_SHADER_H

// Shader objects (spec 7.1-7.2): creation, source, CPU-side compile check
// (#version + entry point), queries and info logs.

#include <map>
#include <string>

#include "tgles/error.h"
#include "tgles/gl_types.h"
#include "tgles/spec.h"  // ShaderStage names.

namespace tgles {

// Shader types (from gl32.h).
inline constexpr GLenum kGlFragmentShader = 0x8B30;
inline constexpr GLenum kGlVertexShader = 0x8B31;
inline constexpr GLenum kGlComputeShader = 0x91B9;
inline constexpr GLenum kGlGeometryShader = 0x8DD9;
inline constexpr GLenum kGlTessControlShader = 0x8E88;
inline constexpr GLenum kGlTessEvaluationShader = 0x8E87;

// Shader queries.
inline constexpr GLenum kGlShaderType = 0x8B4F;
inline constexpr GLenum kGlDeleteStatus = 0x8B80;
inline constexpr GLenum kGlCompileStatus = 0x8B81;
inline constexpr GLenum kGlInfoLogLength = 0x8B84;
inline constexpr GLenum kGlShaderSourceLength = 0x8B88;

class ShaderManager {
 public:
  ShaderManager();

  GLenum GetError();
  bool HasPending() const;

  GLuint CreateShader(GLenum type);
  void DeleteShader(GLuint shader);
  GLboolean IsShader(GLuint shader);
  void ShaderSource(GLuint shader, GLsizei count, const char** strings,
                    const GLint* lengths);
  void CompileShader(GLuint shader);
  void GetShaderiv(GLuint shader, GLenum pname, GLint* params);
  std::string GetShaderInfoLog(GLuint shader);
  std::string GetShaderSource(GLuint shader);

  // Test/introspection helpers.
  bool CompileSucceeded(GLuint shader) const;
  int ShaderVersion(GLuint shader) const;  // 100/300/310/320, -1 unknown.
  GLenum GetType(GLuint shader) const;

 private:
  struct Shader {
    bool alive = false;
    GLenum type = 0;
    std::string source;
    bool compiled = false;
    bool compile_status = false;
    std::string info_log;
    bool marked_delete = false;
  };

  static bool IsShaderType(GLenum type);
  static int ParseVersion(const std::string& source);

  ErrorQueue errors_;
  GLuint next_name_ = 1;
  std::map<GLuint, Shader> shaders_;
};

}  // namespace tgles

#endif  // TGLES_SHADER_H
