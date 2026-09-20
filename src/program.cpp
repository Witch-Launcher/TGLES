#include "tgles/program.h"

#include <cstdint>
#include <cstring>
#include <regex>

#include "tgles/buffer.h"  // kMaxUniformBufferBindings for block bindings.
#include "tgles/vertex_array.h"  // kGlFloat etc. + kMaxVertexAttribs.

namespace tgles {

ProgramManager::ProgramManager(ShaderManager* shaders) : shaders_(shaders) {}

GLenum ProgramManager::GetError() { return errors_.Get(); }
bool ProgramManager::HasPending() const { return errors_.HasPending(); }

GLenum ProgramManager::UniformTypeFromName(const std::string& t) {
  if (t == "float") return kGlFloat;
  if (t == "vec2") return kGlFloatVec2;
  if (t == "vec3") return kGlFloatVec3;
  if (t == "vec4") return kGlFloatVec4;
  if (t == "int") return kGlInt;
  if (t == "ivec2") return kGlIntVec2;
  if (t == "ivec3") return kGlIntVec3;
  if (t == "ivec4") return kGlIntVec4;
  if (t == "uint") return kGlUnsignedInt;
  if (t == "uvec2") return 0x8DC6;
  if (t == "uvec3") return 0x8DC7;
  if (t == "uvec4") return 0x8DC8;
  if (t == "bool") return kGlBool;
  if (t == "bvec2") return kGlBoolVec2;
  if (t == "bvec3") return kGlBoolVec3;
  if (t == "bvec4") return kGlBoolVec4;
  if (t == "mat2") return kGlFloatMat2;
  if (t == "mat3") return kGlFloatMat3;
  if (t == "mat4") return kGlFloatMat4;
  if (t == "sampler2D") return kGlSampler2d;
  if (t == "sampler3D") return kGlSampler3d;
  if (t == "samplerCube") return kGlSamplerCube;
  if (t == "sampler2DArray") return kGlSampler2dArray;
  return 0;
}

ProgramManager::Program* ProgramManager::FindProgram(GLuint program) {
  auto it = programs_.find(program);
  if (it == programs_.end() || !it->second.alive) return nullptr;
  return &it->second;
}

const ProgramManager::Program* ProgramManager::FindProgram(
    GLuint program) const {
  auto it = programs_.find(program);
  if (it == programs_.end() || !it->second.alive) return nullptr;
  return &it->second;
}

GLuint ProgramManager::CreateProgram() {
  const GLuint name = next_program_++;
  programs_[name] = Program();
  programs_[name].alive = true;
  return name;
}

void ProgramManager::DeleteProgram(GLuint program) {
  if (program == 0) return;
  auto it = programs_.find(program);
  if (it == programs_.end() || !it->second.alive) return;
  it->second.alive = false;
  if (current_program_ == program) current_program_ = 0;
}

GLboolean ProgramManager::IsProgram(GLuint program) {
  if (program == 0) return kGlFalse;
  return FindProgram(program) != nullptr ? kGlTrue : kGlFalse;
}

void ProgramManager::AttachShader(GLuint program, GLuint shader) {
  Program* prog = FindProgram(program);
  if (prog == nullptr || shaders_->IsShader(shader) == kGlFalse) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  for (GLuint s : prog->attached) {
    if (s == shader) {
      errors_.Record(kGlInvalidOperation);  // Already attached.
      return;
    }
  }
  prog->attached.push_back(shader);
}

void ProgramManager::DetachShader(GLuint program, GLuint shader) {
  Program* prog = FindProgram(program);
  if (prog == nullptr || shaders_->IsShader(shader) == kGlFalse) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  for (auto it = prog->attached.begin(); it != prog->attached.end(); ++it) {
    if (*it == shader) {
      prog->attached.erase(it);
      return;
    }
  }
  errors_.Record(kGlInvalidOperation);  // Not attached.
}

void ProgramManager::ParseLink(Program& prog) {
  prog.uniforms.clear();
  prog.attribs.clear();
  prog.uniform_blocks.clear();
  GLint next_uniform_loc = 0;
  GLint next_attrib_loc = 0;
  static const std::regex kUniform(
      R"(uniform\s+(\w+)\s+(\w+)(?:\s*\[\s*(\d*)\s*\])?\s*;)");
  static const std::regex kUniformBlock(R"(uniform\s+(\w+)\s*\{)");
  static const std::regex kAttrib(
      R"((?:layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*)?in\s+\w+\s+(\w+)\s*;)");
  for (GLuint s : prog.attached) {
    const std::string src = shaders_->GetShaderSource(s);
    const GLenum stage = shaders_->GetType(s);
    for (std::sregex_iterator it(src.begin(), src.end(), kUniformBlock), end;
         it != end; ++it) {
      const std::string name = (*it)[1].str();
      if (name.rfind("gl_", 0) == 0) continue;
      bool known = false;
      for (const auto& b : prog.uniform_blocks) {
        if (b == name) {
          known = true;
          break;
        }
      }
      if (!known) prog.uniform_blocks.push_back(name);
    }
    for (std::sregex_iterator it(src.begin(), src.end(), kUniform), end;
         it != end; ++it) {
      const std::string name = (*it)[2].str();
      if (name.rfind("gl_", 0) == 0) continue;
      const GLenum type = UniformTypeFromName((*it)[1].str());
      if (type == 0) continue;
      bool known = false;
      for (const auto& u : prog.uniforms) {
        if (u.name == name) {
          known = true;
          break;
        }
      }
      if (known) continue;
      ActiveUniform u;
      u.name = name;
      u.type = type;
      const std::string count = (*it)[3].str();
      u.array_size = count.empty() ? 1 : std::stoi(count);
      if (u.array_size <= 0) u.array_size = 1;
      u.base_location = next_uniform_loc;
      next_uniform_loc += u.array_size;
      prog.uniforms.push_back(u);
    }
    if (stage == kGlVertexShader) {
      for (std::sregex_iterator it(src.begin(), src.end(), kAttrib), end;
           it != end; ++it) {
        const std::string name = (*it)[2].str();
        if (name.rfind("gl_", 0) == 0) continue;
        bool known = false;
        for (const auto& a : prog.attribs) {
          if (a.name == name) {
            known = true;
            break;
          }
        }
        if (known) continue;
        ActiveAttrib a;
        a.name = name;
        a.type = kGlFloatVec4;
        a.array_size = 1;
        auto bound = prog.attrib_bindings.find(name);
        if (!(*it)[1].str().empty()) {
          a.location = std::stoi((*it)[1].str());
        } else if (bound != prog.attrib_bindings.end()) {
          a.location = bound->second;
        } else {
          a.location = next_attrib_loc;
        }
        if (a.location >= next_attrib_loc) next_attrib_loc = a.location + 1;
        prog.attribs.push_back(a);
      }
    }
  }
}

void ProgramManager::LinkProgram(GLuint program) {
  Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  prog->linked = true;
  prog->link_status = false;
  prog->info_log.clear();
  // Every attached shader must have compiled.
  for (GLuint s : prog->attached) {
    if (!shaders_->CompileSucceeded(s)) {
      prog->info_log = "error: attached shader failed to compile";
      return;
    }
  }
  // All stages must share one language version.
  int version = -2;
  for (GLuint s : prog->attached) {
    const int v = shaders_->ShaderVersion(s);
    if (version == -2) {
      version = v;
    } else if (v != version) {
      prog->info_log = "error: mixed GLSL versions cannot be linked";
      return;
    }
  }
  bool has_vertex = false, has_tess_control = false, has_tess_eval = false,
       has_geometry = false, has_fragment = false, has_compute = false;
  for (GLuint s : prog->attached) {
    switch (shaders_->GetType(s)) {
      case kGlVertexShader: has_vertex = true; break;
      case kGlTessControlShader: has_tess_control = true; break;
      case kGlTessEvaluationShader: has_tess_eval = true; break;
      case kGlGeometryShader: has_geometry = true; break;
      case kGlFragmentShader: has_fragment = true; break;
      case kGlComputeShader: has_compute = true; break;
      default: break;
    }
  }
  if (has_compute &&
      (has_vertex || has_fragment || has_geometry || has_tess_control ||
       has_tess_eval)) {
    prog->info_log = "error: compute shader must be linked alone";
    return;
  }
  if (!has_compute && !prog->separable) {
    if (!has_vertex && !has_compute) {
      prog->info_log = "error: graphics program needs a vertex shader";
      return;
    }
    if (has_tess_control != has_tess_eval) {
      prog->info_log = "error: tessellation control/eval must pair up";
      return;
    }
    if ((has_tess_control || has_tess_eval) && !has_vertex) {
      prog->info_log = "error: tessellation needs a vertex shader";
      return;
    }
    if (has_geometry && !has_vertex && !has_tess_eval) {
      prog->info_log = "error: geometry shader needs vertex input";
      return;
    }
  }
  (void)has_fragment;
  ParseLink(*prog);
  prog->link_status = true;
}

void ProgramManager::UseProgram(GLuint program) {
  if (program != 0 && FindProgram(program) == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  current_program_ = program;
}

GLuint ProgramManager::CurrentProgram() const { return current_program_; }

void ProgramManager::ValidateProgram(GLuint program) {
  Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  prog->validate_status = prog->linked && prog->link_status;
}

void ProgramManager::GetProgramiv(GLuint program, GLenum pname,
                                  GLint* params) {
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  switch (pname) {
    case kGlLinkStatus:
      *params = (prog->linked && prog->link_status) ? 1 : 0;
      return;
    case kGlValidateStatus:
      *params = prog->validate_status ? 1 : 0;
      return;
    case kGlInfoLogLength:
      *params = static_cast<GLint>(prog->info_log.size() + 1);
      return;
    case kGlAttachedShaders:
      *params = static_cast<GLint>(prog->attached.size());
      return;
    case kGlActiveUniforms:
      *params = static_cast<GLint>(prog->uniforms.size());
      return;
    case kGlActiveUniformBlocks:
      *params = static_cast<GLint>(prog->uniform_blocks.size());
      return;
    case kGlActiveAttributes:
      *params = static_cast<GLint>(prog->attribs.size());
      return;
    case kGlDeleteStatus:
      *params = 0;
      return;
    case kGlProgramSeparable:
      *params = prog->separable ? 1 : 0;
      return;
    case kGlNumProgramBinaryFormats:
      *params = 1;
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

std::string ProgramManager::GetProgramInfoLog(GLuint program) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return {};
  }
  return prog->info_log;
}

GLint ProgramManager::GetUniformLocation(GLuint program, const char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return -1;
  }
  if (!prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return -1;
  }
  const std::string query(name);
  if (query.rfind("gl_", 0) == 0) return -1;
  // Accept "name" and "name[i]" forms.
  std::string base = query;
  GLint elem = 0;
  const std::size_t bracket = query.find('[');
  if (bracket != std::string::npos) {
    base = query.substr(0, bracket);
    const std::size_t end = query.find(']', bracket);
    if (end == std::string::npos) return -1;
    elem = std::stoi(query.substr(bracket + 1, end - bracket - 1));
  }
  for (const auto& u : prog->uniforms) {
    if (u.name == base) {
      if (elem < 0 || elem >= u.array_size) return -1;
      if (bracket == std::string::npos && u.array_size > 1) {
        // Whole-array query returns the base location (spec allows it).
      }
      return u.base_location + elem;
    }
  }
  return -1;
}

GLint ProgramManager::GetAttribLocation(GLuint program, const char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return -1;
  }
  if (!prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return -1;
  }
  for (const auto& a : prog->attribs) {
    if (a.name == name) return a.location;
  }
  return -1;
}

void ProgramManager::BindAttribLocation(GLuint program, GLuint index,
                                        const char* name) {
  Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (index >= static_cast<GLuint>(kMaxVertexAttribs)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (prog->linked) {
    errors_.Record(kGlInvalidOperation);  // Must precede linking.
    return;
  }
  prog->attrib_bindings[name] = static_cast<GLint>(index);
}

void ProgramManager::GetActiveUniform(GLuint program, GLuint index,
                                      GLsizei buf_size, GLsizei* length,
                                      GLint* size, GLenum* type, char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (index >= prog->uniforms.size()) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const ActiveUniform& u = prog->uniforms[index];
  if (size != nullptr) *size = u.array_size;
  if (type != nullptr) *type = u.type;
  if (name != nullptr && buf_size > 0) {
    std::string out = u.name;
    if (u.array_size > 1) out += "[0]";
    std::strncpy(name, out.c_str(), static_cast<std::size_t>(buf_size));
    name[buf_size - 1] = '\0';
    if (length != nullptr) {
      *length = static_cast<GLsizei>(out.size());
    }
  } else if (length != nullptr) {
    *length = 0;
  }
}

void ProgramManager::GetActiveAttrib(GLuint program, GLuint index,
                                     GLsizei buf_size, GLsizei* length,
                                     GLint* size, GLenum* type, char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (index >= prog->attribs.size()) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const ActiveAttrib& a = prog->attribs[index];
  if (size != nullptr) *size = a.array_size;
  if (type != nullptr) *type = a.type;
  if (name != nullptr && buf_size > 0) {
    std::strncpy(name, a.name.c_str(), static_cast<std::size_t>(buf_size));
    name[buf_size - 1] = '\0';
    if (length != nullptr) *length = static_cast<GLsizei>(a.name.size());
  } else if (length != nullptr) {
    *length = 0;
  }
}

bool ProgramManager::IsUniformScalarCommandFor(GLenum uniform_type, int comps,
                                               bool is_float,
                                               bool is_unsigned) {
  switch (uniform_type) {
    case kGlFloat:
      return is_float && !is_unsigned && comps == 1;
    case kGlFloatVec2:
      return is_float && !is_unsigned && comps == 2;
    case kGlFloatVec3:
      return is_float && !is_unsigned && comps == 3;
    case kGlFloatVec4:
      return is_float && !is_unsigned && comps == 4;
    case kGlInt:
    case kGlBool:
      return !is_float && !is_unsigned && comps == 1;
    case kGlIntVec2:
    case kGlBoolVec2:
      return !is_float && !is_unsigned && comps == 2;
    case kGlIntVec3:
    case kGlBoolVec3:
      return !is_float && !is_unsigned && comps == 3;
    case kGlIntVec4:
    case kGlBoolVec4:
      return !is_float && !is_unsigned && comps == 4;
    case kGlUnsignedInt:
      return !is_float && is_unsigned && comps == 1;
    default:
      break;
  }
  if (uniform_type == 0x8DC6 || uniform_type == 0x8DC7 ||
      uniform_type == 0x8DC8) {
    const int want = (uniform_type == 0x8DC6)
                         ? 2
                         : (uniform_type == 0x8DC7 ? 3 : 4);
    return !is_float && is_unsigned && comps == want;
  }
  return false;
}

void ProgramManager::SetUniformFloat(GLint location, const GLfloat* values,
                                     int comps, GLsizei count) {
  if (location < 0) return;  // -1 locations are silently ignored.
  const Program* prog = FindProgram(current_program_);
  if (prog == nullptr || !prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (count < 1) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const ActiveUniform* target = nullptr;
  for (const auto& u : prog->uniforms) {
    if (location >= u.base_location &&
        location < u.base_location + u.array_size) {
      target = &u;
      break;
    }
  }
  if (target == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (count > 1 && target->array_size == 1) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const GLint elem = location - target->base_location;
  if (elem + count > target->array_size) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (!IsUniformScalarCommandFor(target->type, comps, true, false)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  Program& mut = programs_[current_program_];
  std::vector<float>& slot = mut.uniform_f[location];
  slot.assign(values, values + count * comps);
}

void ProgramManager::Uniform1f(GLint location, GLfloat v0) {
  SetUniformFloat(location, &v0, 1, 1);
}

void ProgramManager::Uniform4f(GLint location, GLfloat v0, GLfloat v1,
                               GLfloat v2, GLfloat v3) {
  const GLfloat v[4] = {v0, v1, v2, v3};
  SetUniformFloat(location, v, 4, 1);
}

void ProgramManager::Uniform1fv(GLint location, GLsizei count,
                                const GLfloat* value) {
  if (value == nullptr && count > 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // Resolve component count from the target uniform.
  const Program* prog = FindProgram(current_program_);
  if (location >= 0 &&
      (prog == nullptr || !prog->linked || !prog->link_status)) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  int comps = 1;
  if (prog != nullptr) {
    for (const auto& u : prog->uniforms) {
      if (location >= u.base_location &&
          location < u.base_location + u.array_size) {
        if (u.type == kGlFloatVec2) comps = 2;
        if (u.type == kGlFloatVec3) comps = 3;
        if (u.type == kGlFloatVec4) comps = 4;
        break;
      }
    }
  }
  if (location < 0) return;
  SetUniformFloat(location, value, comps, count);
}

void ProgramManager::Uniform1i(GLint location, GLint v0) {
  if (location < 0) return;
  const Program* prog = FindProgram(current_program_);
  if (prog == nullptr || !prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  for (const auto& u : prog->uniforms) {
    if (location >= u.base_location &&
        location < u.base_location + u.array_size) {
      if (!IsUniformScalarCommandFor(u.type, 1, false, false) &&
          !IsUniformScalarCommandFor(u.type, 1, false, true) &&
          u.type != kGlSampler2d && u.type != kGlSampler3d &&
          u.type != kGlSamplerCube && u.type != kGlSampler2dArray) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      programs_[current_program_].uniform_f[location] =
          std::vector<float>{static_cast<float>(v0)};
      return;
    }
  }
  errors_.Record(kGlInvalidOperation);
}

void ProgramManager::UniformMatrix4fv(GLint location, GLsizei count,
                                      GLboolean transpose, const GLfloat* value) {
  if (location < 0) return;
  if (transpose != kGlFalse) {
    errors_.Record(kGlInvalidValue);  // Must be FALSE in ES.
    return;
  }
  if (count < 1 || (count > 0 && value == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Program* prog = FindProgram(current_program_);
  if (prog == nullptr || !prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  for (const auto& u : prog->uniforms) {
    if (location >= u.base_location &&
        location < u.base_location + u.array_size) {
      if (u.type != kGlFloatMat4) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      if (count > 1 && u.array_size == 1) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      Program& mut = programs_[current_program_];
      std::vector<float>& slot = mut.uniform_f[location];
      slot.assign(value, value + count * 16);
      return;
    }
  }
  errors_.Record(kGlInvalidOperation);
}

void ProgramManager::GetProgramBinary(GLuint program, GLsizei buf_size,
                                      GLsizei* length, GLenum* binary_format,
                                      void* binary) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || !prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (static_cast<std::size_t>(buf_size) < prog->binary.size()) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (length != nullptr) {
    *length = static_cast<GLsizei>(prog->binary.size());
  }
  if (binary_format != nullptr) *binary_format = kGlTglProgramBinary;
  if (binary != nullptr && !prog->binary.empty()) {
    std::memcpy(binary, prog->binary.data(), prog->binary.size());
  }
}

void ProgramManager::ProgramBinary(GLuint program, GLenum binary_format,
                                   const void* binary, GLsizei length) {
  Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (binary_format != kGlTglProgramBinary) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (length < 0 || (length > 0 && binary == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  prog->binary.assign(static_cast<const std::uint8_t*>(binary),
                      static_cast<const std::uint8_t*>(binary) + length);
  prog->linked = true;
  prog->link_status = true;
  prog->info_log.clear();
}

void ProgramManager::ProgramParameteri(GLuint program, GLenum pname,
                                       GLint value) {
  Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (pname != kGlProgramSeparable) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  prog->separable = (value != 0);
}

void ProgramManager::GenProgramPipelines(GLsizei n, GLuint* pipelines) {
  if (n < 0 || (n > 0 && pipelines == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    pipelines[i] = next_pipeline_++;
    pipelines_[pipelines[i]] = Pipeline();
    pipelines_[pipelines[i]].alive = true;
  }
}

void ProgramManager::DeleteProgramPipelines(GLsizei n,
                                            const GLuint* pipelines) {
  if (n < 0 || (n > 0 && pipelines == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < n; ++i) {
    if (pipelines[i] == 0) continue;
    pipelines_.erase(pipelines[i]);
    if (bound_pipeline_ == pipelines[i]) bound_pipeline_ = 0;
  }
}

GLboolean ProgramManager::IsProgramPipeline(GLuint pipeline) {
  if (pipeline == 0) return kGlFalse;
  auto it = pipelines_.find(pipeline);
  return (it != pipelines_.end() && it->second.alive) ? kGlTrue : kGlFalse;
}

void ProgramManager::BindProgramPipeline(GLuint pipeline) {
  if (pipeline != 0 && IsProgramPipeline(pipeline) == kGlFalse) {
    // Binding an unknown name creates it (spec 2.6.1 object model).
    pipelines_[pipeline] = Pipeline();
    pipelines_[pipeline].alive = true;
  }
  bound_pipeline_ = pipeline;
}

void ProgramManager::UseProgramStages(GLuint pipeline, GLbitfield stages,
                                      GLuint program) {
  auto it = pipelines_.find(pipeline);
  if (it == pipelines_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (program != 0) {
    const Program* prog = FindProgram(program);
    if (prog == nullptr || !prog->separable) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
  }
  constexpr GLbitfield kAllKnown =
      kGlVertexShaderBit | kGlFragmentShaderBit | kGlGeometryShaderBit |
      kGlTessControlShaderBit | kGlTessEvaluationShaderBit |
      kGlComputeShaderBit;
  if ((stages & ~kAllKnown) != 0 && stages != kGlAllShaderBits) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  Pipeline& pipe = it->second;
  if ((stages & kGlVertexShaderBit) != 0) pipe.vertex = program;
  if ((stages & kGlFragmentShaderBit) != 0) pipe.fragment = program;
  if ((stages & kGlGeometryShaderBit) != 0) pipe.geometry = program;
  if ((stages & kGlTessControlShaderBit) != 0) pipe.tess_control = program;
  if ((stages & kGlTessEvaluationShaderBit) != 0) {
    pipe.tess_evaluation = program;
  }
  if ((stages & kGlComputeShaderBit) != 0) pipe.compute = program;
}

void ProgramManager::ActiveShaderProgram(GLuint pipeline, GLuint program) {
  auto it = pipelines_.find(pipeline);
  const Program* prog = FindProgram(program);
  if (it == pipelines_.end() || !it->second.alive || prog == nullptr ||
      !prog->separable) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  it->second.active_program = program;
}
bool ProgramManager::LinkSucceeded(GLuint program) const {
  const Program* prog = FindProgram(program);
  return prog != nullptr && prog->linked && prog->link_status;
}

bool ProgramManager::ProgramHasStage(GLuint program,
                                     GLenum shader_type) const {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || !prog->linked || !prog->link_status) return false;
  for (GLuint s : prog->attached) {
    if (shaders_->GetType(s) == shader_type) return true;
  }
  return false;
}

std::vector<ActiveUniform> ProgramManager::Uniforms(GLuint program) const {
  const Program* prog = FindProgram(program);
  return prog == nullptr ? std::vector<ActiveUniform>() : prog->uniforms;
}

void ProgramManager::UniformBlockBinding(GLuint program,
                                         GLuint uniform_block_index,
                                         GLuint uniform_block_binding) {
  Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (uniform_block_binding >= static_cast<GLuint>(kMaxUniformBufferBindings)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // TGL tracks bindings even when the block index is not yet reflected;
  // out-of-range indices for the current link are still recorded (spec 7.3.1
  // allows binding before reflection; draw-time validation resolves).
  prog->uniform_block_bindings[uniform_block_index] = uniform_block_binding;
}

GLuint ProgramManager::GetProgramResourceIndex(GLuint program,
                                               GLenum program_interface,
                                               const char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidValue);
    return kGlInvalidIndex;
  }
  switch (program_interface) {
    case kGlUniformInterface:
    case kGlUniformBlockInterface:
    case kGlBufferVariableInterface:
    case kGlShaderStorageBlock:
      break;
    default:
      errors_.Record(kGlInvalidEnum);
      return kGlInvalidIndex;
  }
  if (!prog->linked || !prog->link_status) {
    return kGlInvalidIndex;
  }
  // Uniform-block names come from parsed `uniform Name {` declarations;
  // SSBO blocks share the same declaration shape at state level (storage
  // qualifiers are a compile-target concern), so both block interfaces
  // resolve against the reflected block list (spec 7.3.2: unknown names
  // return INVALID_INDEX without an error).
  if (program_interface == kGlUniformInterface) {
    for (GLuint i = 0; i < prog->uniforms.size(); ++i) {
      if (prog->uniforms[i].name == name) return i;
    }
    return kGlInvalidIndex;
  }
  if (program_interface == kGlUniformBlockInterface ||
      program_interface == kGlShaderStorageBlock) {
    for (GLuint i = 0; i < prog->uniform_blocks.size(); ++i) {
      if (prog->uniform_blocks[i] == name) return i;
    }
    return kGlInvalidIndex;
  }
  return kGlInvalidIndex;
}

GLuint ProgramManager::GetUniformBlockIndex(GLuint program, const char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidValue);
    return kGlInvalidIndex;
  }
  if (!prog->linked || !prog->link_status) {
    return kGlInvalidIndex;
  }
  for (GLuint i = 0; i < prog->uniform_blocks.size(); ++i) {
    if (prog->uniform_blocks[i] == name) return i;
  }
  return kGlInvalidIndex;
}

bool ProgramManager::GetUniformMatrix(GLint location, GLfloat* out_16) {
  if (out_16 == nullptr) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  const Program* prog = FindProgram(current_program_);
  if (prog == nullptr) return false;
  auto it = prog->uniform_f.find(location);
  if (it == prog->uniform_f.end() || it->second.size() < 16) return false;
  for (int i = 0; i < 16; ++i) out_16[i] = it->second[static_cast<std::size_t>(i)];
  return true;
}

}  // namespace tgles
