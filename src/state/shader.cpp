#include "tgles/state/shader.h"

#include <regex>

namespace tgles {

ShaderManager::ShaderManager() = default;

GLenum ShaderManager::GetError() { return errors_.Get(); }
bool ShaderManager::HasPending() const { return errors_.HasPending(); }

bool ShaderManager::IsShaderType(GLenum type) {
  switch (type) {
    case kGlVertexShader:
    case kGlFragmentShader:
    case kGlComputeShader:
    case kGlGeometryShader:
    case kGlTessControlShader:
    case kGlTessEvaluationShader:
      return true;
    default:
      return false;
  }
}

int ShaderManager::ParseVersion(const std::string& source) {
  // Matches "#version 320 es" with any leading whitespace.
  static const std::regex kVersion(
      R"(^\s*#\s*version\s+(\d+)(?:\s+es)?)",
      std::regex_constants::multiline);
  std::smatch m;
  if (!std::regex_search(source, m, kVersion)) return 100;  // Default: 1.00.
  const int v = std::stoi(m[1].str());
  return (v == 100 || v == 300 || v == 310 || v == 320) ? v : -1;
}

GLuint ShaderManager::CreateShader(GLenum type) {
  if (!IsShaderType(type)) {
    errors_.Record(kGlInvalidEnum);
    return 0;
  }
  const GLuint name = next_name_++;
  Shader s;
  s.alive = true;
  s.type = type;
  shaders_[name] = s;
  return name;
}

void ShaderManager::DeleteShader(GLuint shader) {
  if (shader == 0) return;  // Unused names are silently ignored.
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) return;
  it->second.alive = false;
  it->second.marked_delete = true;
}

GLboolean ShaderManager::IsShader(GLuint shader) {
  if (shader == 0) return kGlFalse;
  auto it = shaders_.find(shader);
  return (it != shaders_.end() && it->second.alive) ? kGlTrue : kGlFalse;
}

void ShaderManager::ShaderSource(GLuint shader, GLsizei count,
                                 const char** strings, const GLint* lengths) {
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (count < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (count > 0 && strings == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  std::string src;
  for (GLsizei i = 0; i < count; ++i) {
    if (strings[i] == nullptr) continue;
    if (lengths == nullptr || lengths[i] < 0) {
      src += strings[i];
    } else {
      src.append(strings[i], static_cast<std::size_t>(lengths[i]));
    }
  }
  it->second.source = src;
  it->second.compiled = false;
  it->second.compile_status = false;
}

void ShaderManager::CompileShader(GLuint shader) {
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  Shader& s = it->second;
  s.compiled = true;
  const int version = ParseVersion(s.source);
  if (version < 0) {
    s.compile_status = false;
    s.info_log = "error: unsupported #version (want 100/300/310/320 es)";
    return;
  }
  if (s.source.find("void main") == std::string::npos) {
    s.compile_status = false;
    s.info_log = "error: missing 'void main' entry point";
    return;
  }
  s.compile_status = true;
  s.info_log.clear();
}

void ShaderManager::GetShaderiv(GLuint shader, GLenum pname, GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const Shader& s = it->second;
  switch (pname) {
    case kGlShaderType:
      *params = static_cast<GLint>(s.type);
      return;
    case kGlDeleteStatus:
      *params = s.marked_delete ? 1 : 0;
      return;
    case kGlCompileStatus:
      *params = (s.compiled && s.compile_status) ? 1 : 0;
      return;
    case kGlInfoLogLength:
      *params = static_cast<GLint>(s.info_log.size() + 1);
      return;
    case kGlShaderSourceLength:
      *params =
          s.source.empty() ? 0 : static_cast<GLint>(s.source.size() + 1);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

std::string ShaderManager::GetShaderInfoLog(GLuint shader) {
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return {};
  }
  return it->second.info_log;
}

std::string ShaderManager::GetShaderSource(GLuint shader) {
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return {};
  }
  return it->second.source;
}

bool ShaderManager::CompileSucceeded(GLuint shader) const {
  auto it = shaders_.find(shader);
  return it != shaders_.end() && it->second.alive && it->second.compiled &&
         it->second.compile_status;
}

int ShaderManager::ShaderVersion(GLuint shader) const {
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) return -1;
  return ParseVersion(it->second.source);
}

GLenum ShaderManager::GetType(GLuint shader) const {
  auto it = shaders_.find(shader);
  if (it == shaders_.end() || !it->second.alive) return 0;
  return it->second.type;
}

void ShaderManager::ShaderBinary(GLsizei count, const GLuint* shaders,
                                 GLenum binaryFormat, const void* binary,
                                 GLsizei length) {
  if (count < 0 || (count > 0 && shaders == nullptr) || length < 0 ||
      (length > 0 && binary == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (count == 0) return;  // No-op success.
  for (GLsizei i = 0; i < count; ++i) {
    auto it = shaders_.find(shaders[i]);
    if (it == shaders_.end() || !it->second.alive) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
  }
  (void)binaryFormat;
  errors_.Record(kGlInvalidEnum);  // No binary formats supported.
}

}  // namespace tgles
