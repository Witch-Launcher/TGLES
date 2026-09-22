#include "tgles/state/program.h"

#include <cstdint>
#include <cstring>
#include <regex>

#include "tgles/state/buffer.h"  // kMaxUniformBufferBindings for block bindings.
#include "tgles/state/vertex_array.h"  // kGlFloat etc. + kMaxVertexAttribs.

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
  static const std::regex kStruct(R"(struct\s+(\w+)\s*\{([^}]*)\}\s*;)");
  static const std::regex kStructMem(R"((\w+)\s+(\w+)\s*;)");
  // Collect struct definitions across all attached sources (11b).
  std::map<std::string, std::vector<std::pair<std::string, std::string>>>
      struct_members;
  for (GLuint s : prog.attached) {
    const std::string src = shaders_->GetShaderSource(s);
    for (std::sregex_iterator it(src.begin(), src.end(), kStruct), end;
         it != end; ++it) {
      const std::string sname = (*it)[1].str();
      if (struct_members.find(sname) != struct_members.end()) continue;
      const std::string body = (*it)[2].str();
      std::vector<std::pair<std::string, std::string>> mems;
      for (std::sregex_iterator mi(body.begin(), body.end(), kStructMem), mend;
           mi != mend; ++mi) {
        mems.emplace_back((*mi)[1].str(), (*mi)[2].str());
      }
      if (!mems.empty()) struct_members[sname] = mems;
    }
  }
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
      const std::string tname = (*it)[1].str();
      const std::string name = (*it)[2].str();
      if (name.rfind("gl_", 0) == 0) continue;
      // Struct uniform (11b): flatten leaves `u.member`.
      auto sit = struct_members.find(tname);
      if (sit != struct_members.end()) {
        const std::string count = (*it)[3].str();
        if (!count.empty()) continue;  // Struct arrays: skip (fail via trans).
        for (const auto& mem : sit->second) {
          const GLenum mtype = UniformTypeFromName(mem.first);
          if (mtype == 0) continue;
          const std::string leaf = name + "." + mem.second;
          bool known = false;
          for (const auto& u : prog.uniforms) {
            if (u.name == leaf) {
              known = true;
              break;
            }
          }
          if (known) continue;
          ActiveUniform u;
          u.name = leaf;
          u.type = mtype;
          u.array_size = 1;
          u.base_location = next_uniform_loc++;
          prog.uniforms.push_back(u);
        }
        continue;
      }
      const GLenum type = UniformTypeFromName(tname);
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
  SetUniformFloatFor(current_program_, location, values, comps, count);
}

void ProgramManager::SetUniformFloatFor(GLuint program, GLint location,
                                        const GLfloat* values, int comps,
                                        GLsizei count) {
  if (location < 0) return;  // -1 locations are silently ignored.
  const Program* prog = FindProgram(program);
  if (prog == nullptr || !prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (count < 1 || (count > 0 && values == nullptr)) {
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
  Program& mut = programs_[program];
  std::vector<float>& slot = mut.uniform_f[location];
  slot.assign(values, values + count * comps);
}

void ProgramManager::SetUniformIntFor(GLuint program, GLint location,
                                      const GLint* values, int comps,
                                      GLsizei count, bool is_unsigned) {
  if (location < 0) return;  // -1 locations are silently ignored.
  const Program* prog = FindProgram(program);
  if (prog == nullptr || !prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (count < 1 || (count > 0 && values == nullptr)) {
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
  // Integer setters accept int/uint/bool vectors of matching width; sampler
  // uniforms additionally accept the SIGNED scalar form (spec 7.3.1 allows
  // setting samplers with Uniform1i). This preserves the Uniform1i
  // acceptance exactly (it also took uint types); unsigned setters take uint
  // types only.
  bool type_ok = IsUniformScalarCommandFor(target->type, comps, false,
                                           is_unsigned);
  if (!type_ok && !is_unsigned) {
    if (IsUniformScalarCommandFor(target->type, comps, false, true)) {
      type_ok = true;
    } else if (comps == 1 &&
               (target->type == kGlSampler2d ||
                target->type == kGlSampler3d ||
                target->type == kGlSamplerCube ||
                target->type == kGlSampler2dArray)) {
      type_ok = true;
    }
  }
  if (!type_ok) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  Program& mut = programs_[program];
  std::vector<float>& slot = mut.uniform_f[location];
  slot.resize(static_cast<std::size_t>(count) * comps);
  for (GLsizei i = 0; i < count * comps; ++i) {
    slot[static_cast<std::size_t>(i)] =
        is_unsigned ? static_cast<GLfloat>(
                          static_cast<GLuint>(values[i]))
                    : static_cast<GLfloat>(values[i]);
  }
}

void ProgramManager::SetUniformMatrixFor(GLuint program, GLint location,
                                         const GLfloat* value, GLsizei count,
                                         GLenum mat_type, int float_count) {
  if (location < 0) return;  // -1 locations are silently ignored.
  if (count < 1 || (count > 0 && value == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const Program* prog = FindProgram(program);
  if (prog == nullptr || !prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  for (const auto& u : prog->uniforms) {
    if (location >= u.base_location &&
        location < u.base_location + u.array_size) {
      if (u.type != mat_type) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      if (count > 1 && u.array_size == 1) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      Program& mut = programs_[program];
      std::vector<float>& slot = mut.uniform_f[location];
      slot.assign(value, value + count * float_count);
      return;
    }
  }
  errors_.Record(kGlInvalidOperation);
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
  SetUniformIntFor(current_program_, location, &v0, 1, 1, false);
}

void ProgramManager::UniformMatrix4fv(GLint location, GLsizei count,
                                      GLboolean transpose, const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);  // Must be FALSE in ES.
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count, kGlFloatMat4,
                      16);
}

// --- Full scalar/vector/matrix families (spec 7.3.1) --------------------------
// Each shape funnels into the three *For helpers above, so validation lives
// in exactly one place per value class (float / int / matrix).

void ProgramManager::Uniform2f(GLint location, GLfloat v0, GLfloat v1) {
  const GLfloat v[2] = {v0, v1};
  SetUniformFloatFor(current_program_, location, v, 2, 1);
}

void ProgramManager::Uniform3f(GLint location, GLfloat v0, GLfloat v1,
                               GLfloat v2) {
  const GLfloat v[3] = {v0, v1, v2};
  SetUniformFloatFor(current_program_, location, v, 3, 1);
}

void ProgramManager::Uniform2fv(GLint location, GLsizei count,
                                const GLfloat* value) {
  if (value == nullptr && count > 0) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformFloatFor(current_program_, location, value, 2, count);
}

void ProgramManager::Uniform3fv(GLint location, GLsizei count,
                                const GLfloat* value) {
  if (value == nullptr && count > 0) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformFloatFor(current_program_, location, value, 3, count);
}

void ProgramManager::Uniform4fv(GLint location, GLsizei count,
                                const GLfloat* value) {
  if (value == nullptr && count > 0) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformFloatFor(current_program_, location, value, 4, count);
}

void ProgramManager::Uniform2i(GLint location, GLint v0, GLint v1) {
  const GLint v[2] = {v0, v1};
  SetUniformIntFor(current_program_, location, v, 2, 1, false);
}

void ProgramManager::Uniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
  const GLint v[3] = {v0, v1, v2};
  SetUniformIntFor(current_program_, location, v, 3, 1, false);
}

void ProgramManager::Uniform4i(GLint location, GLint v0, GLint v1, GLint v2,
                               GLint v3) {
  const GLint v[4] = {v0, v1, v2, v3};
  SetUniformIntFor(current_program_, location, v, 4, 1, false);
}

void ProgramManager::Uniform1ui(GLint location, GLuint v0) {
  const GLint v[1] = {static_cast<GLint>(v0)};
  SetUniformIntFor(current_program_, location, v, 1, 1, true);
}

void ProgramManager::Uniform2ui(GLint location, GLuint v0, GLuint v1) {
  const GLint v[2] = {static_cast<GLint>(v0), static_cast<GLint>(v1)};
  SetUniformIntFor(current_program_, location, v, 2, 1, true);
}

void ProgramManager::Uniform3ui(GLint location, GLuint v0, GLuint v1,
                                GLuint v2) {
  const GLint v[3] = {static_cast<GLint>(v0), static_cast<GLint>(v1),
                      static_cast<GLint>(v2)};
  SetUniformIntFor(current_program_, location, v, 3, 1, true);
}

void ProgramManager::Uniform4ui(GLint location, GLuint v0, GLuint v1,
                                GLuint v2, GLuint v3) {
  const GLint v[4] = {static_cast<GLint>(v0), static_cast<GLint>(v1),
                      static_cast<GLint>(v2), static_cast<GLint>(v3)};
  SetUniformIntFor(current_program_, location, v, 4, 1, true);
}

void ProgramManager::Uniform1iv(GLint location, GLsizei count,
                                const GLint* value) {
  SetUniformIntFor(current_program_, location, value, 1, count, false);
}

void ProgramManager::Uniform2iv(GLint location, GLsizei count,
                                const GLint* value) {
  SetUniformIntFor(current_program_, location, value, 2, count, false);
}

void ProgramManager::Uniform3iv(GLint location, GLsizei count,
                                const GLint* value) {
  SetUniformIntFor(current_program_, location, value, 3, count, false);
}

void ProgramManager::Uniform4iv(GLint location, GLsizei count,
                                const GLint* value) {
  SetUniformIntFor(current_program_, location, value, 4, count, false);
}

void ProgramManager::Uniform1uiv(GLint location, GLsizei count,
                                 const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count));
    for (GLsizei i = 0; i < count; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(current_program_, location,
                   count > 0 ? tmp.data() : nullptr,
                   1, count, true);
}

void ProgramManager::Uniform2uiv(GLint location, GLsizei count,
                                 const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count) * 2);
    for (GLsizei i = 0; i < count * 2; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(current_program_, location,
                   count > 0 ? tmp.data() : nullptr,
                   2, count, true);
}

void ProgramManager::Uniform3uiv(GLint location, GLsizei count,
                                 const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count) * 3);
    for (GLsizei i = 0; i < count * 3; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(current_program_, location,
                   count > 0 ? tmp.data() : nullptr,
                   3, count, true);
}

void ProgramManager::Uniform4uiv(GLint location, GLsizei count,
                                 const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count) * 4);
    for (GLsizei i = 0; i < count * 4; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(current_program_, location,
                   count > 0 ? tmp.data() : nullptr,
                   4, count, true);
}

void ProgramManager::UniformMatrix2fv(GLint location, GLsizei count,
                                      GLboolean transpose,
                                      const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count, kGlFloatMat2,
                      4);
}

void ProgramManager::UniformMatrix3fv(GLint location, GLsizei count,
                                      GLboolean transpose,
                                      const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count, kGlFloatMat3,
                      9);
}

void ProgramManager::UniformMatrix2x3fv(GLint location, GLsizei count,
                                        GLboolean transpose,
                                        const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count,
                      kGlFloatMat2x3, 6);
}

void ProgramManager::UniformMatrix3x2fv(GLint location, GLsizei count,
                                        GLboolean transpose,
                                        const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count,
                      kGlFloatMat3x2, 6);
}

void ProgramManager::UniformMatrix2x4fv(GLint location, GLsizei count,
                                        GLboolean transpose,
                                        const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count,
                      kGlFloatMat2x4, 8);
}

void ProgramManager::UniformMatrix4x2fv(GLint location, GLsizei count,
                                        GLboolean transpose,
                                        const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count,
                      kGlFloatMat4x2, 8);
}

void ProgramManager::UniformMatrix3x4fv(GLint location, GLsizei count,
                                        GLboolean transpose,
                                        const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count,
                      kGlFloatMat3x4, 12);
}

void ProgramManager::UniformMatrix4x3fv(GLint location, GLsizei count,
                                        GLboolean transpose,
                                        const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(current_program_, location, value, count,
                      kGlFloatMat4x3, 12);
}

// --- Explicit-program setters (spec 7.3.1 glProgramUniform*) -------------------
// Same validation as the current-program twins, resolved against the NAMED
// program: unknown/dead/unlinked program or bad location fails closed, and
// -1 stays silent. A launcher sets uniforms without touching its GL state.

void ProgramManager::ProgramUniform1f(GLuint program, GLint location,
                                      GLfloat v0) {
  SetUniformFloatFor(program, location, &v0, 1, 1);
}

void ProgramManager::ProgramUniform2f(GLuint program, GLint location,
                                      GLfloat v0, GLfloat v1) {
  const GLfloat v[2] = {v0, v1};
  SetUniformFloatFor(program, location, v, 2, 1);
}

void ProgramManager::ProgramUniform3f(GLuint program, GLint location,
                                      GLfloat v0, GLfloat v1, GLfloat v2) {
  const GLfloat v[3] = {v0, v1, v2};
  SetUniformFloatFor(program, location, v, 3, 1);
}

void ProgramManager::ProgramUniform4f(GLuint program, GLint location,
                                      GLfloat v0, GLfloat v1, GLfloat v2,
                                      GLfloat v3) {
  const GLfloat v[4] = {v0, v1, v2, v3};
  SetUniformFloatFor(program, location, v, 4, 1);
}

void ProgramManager::ProgramUniform1i(GLuint program, GLint location,
                                      GLint v0) {
  SetUniformIntFor(program, location, &v0, 1, 1, false);
}

void ProgramManager::ProgramUniform2i(GLuint program, GLint location, GLint v0,
                                      GLint v1) {
  const GLint v[2] = {v0, v1};
  SetUniformIntFor(program, location, v, 2, 1, false);
}

void ProgramManager::ProgramUniform3i(GLuint program, GLint location, GLint v0,
                                      GLint v1, GLint v2) {
  const GLint v[3] = {v0, v1, v2};
  SetUniformIntFor(program, location, v, 3, 1, false);
}

void ProgramManager::ProgramUniform4i(GLuint program, GLint location, GLint v0,
                                      GLint v1, GLint v2, GLint v3) {
  const GLint v[4] = {v0, v1, v2, v3};
  SetUniformIntFor(program, location, v, 4, 1, false);
}

void ProgramManager::ProgramUniform1ui(GLuint program, GLint location,
                                       GLuint v0) {
  const GLint v[1] = {static_cast<GLint>(v0)};
  SetUniformIntFor(program, location, v, 1, 1, true);
}

void ProgramManager::ProgramUniform2ui(GLuint program, GLint location,
                                       GLuint v0, GLuint v1) {
  const GLint v[2] = {static_cast<GLint>(v0), static_cast<GLint>(v1)};
  SetUniformIntFor(program, location, v, 2, 1, true);
}

void ProgramManager::ProgramUniform3ui(GLuint program, GLint location,
                                       GLuint v0, GLuint v1, GLuint v2) {
  const GLint v[3] = {static_cast<GLint>(v0), static_cast<GLint>(v1),
                      static_cast<GLint>(v2)};
  SetUniformIntFor(program, location, v, 3, 1, true);
}

void ProgramManager::ProgramUniform4ui(GLuint program, GLint location,
                                       GLuint v0, GLuint v1, GLuint v2,
                                       GLuint v3) {
  const GLint v[4] = {static_cast<GLint>(v0), static_cast<GLint>(v1),
                      static_cast<GLint>(v2), static_cast<GLint>(v3)};
  SetUniformIntFor(program, location, v, 4, 1, true);
}

void ProgramManager::ProgramUniform1fv(GLuint program, GLint location,
                                       GLsizei count, const GLfloat* value) {
  SetUniformFloatFor(program, location, value, 1, count);
}

void ProgramManager::ProgramUniform2fv(GLuint program, GLint location,
                                       GLsizei count, const GLfloat* value) {
  SetUniformFloatFor(program, location, value, 2, count);
}

void ProgramManager::ProgramUniform3fv(GLuint program, GLint location,
                                       GLsizei count, const GLfloat* value) {
  SetUniformFloatFor(program, location, value, 3, count);
}

void ProgramManager::ProgramUniform4fv(GLuint program, GLint location,
                                       GLsizei count, const GLfloat* value) {
  SetUniformFloatFor(program, location, value, 4, count);
}

void ProgramManager::ProgramUniform1iv(GLuint program, GLint location,
                                       GLsizei count, const GLint* value) {
  SetUniformIntFor(program, location, value, 1, count, false);
}

void ProgramManager::ProgramUniform2iv(GLuint program, GLint location,
                                       GLsizei count, const GLint* value) {
  SetUniformIntFor(program, location, value, 2, count, false);
}

void ProgramManager::ProgramUniform3iv(GLuint program, GLint location,
                                       GLsizei count, const GLint* value) {
  SetUniformIntFor(program, location, value, 3, count, false);
}

void ProgramManager::ProgramUniform4iv(GLuint program, GLint location,
                                       GLsizei count, const GLint* value) {
  SetUniformIntFor(program, location, value, 4, count, false);
}

void ProgramManager::ProgramUniform1uiv(GLuint program, GLint location,
                                        GLsizei count, const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count));
    for (GLsizei i = 0; i < count; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(program, location, count > 0 ? tmp.data() : nullptr, 1,
                   count, true);
}

void ProgramManager::ProgramUniform2uiv(GLuint program, GLint location,
                                        GLsizei count, const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count) * 2);
    for (GLsizei i = 0; i < count * 2; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(program, location, count > 0 ? tmp.data() : nullptr, 2,
                   count, true);
}

void ProgramManager::ProgramUniform3uiv(GLuint program, GLint location,
                                        GLsizei count, const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count) * 3);
    for (GLsizei i = 0; i < count * 3; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(program, location, count > 0 ? tmp.data() : nullptr, 3,
                   count, true);
}

void ProgramManager::ProgramUniform4uiv(GLuint program, GLint location,
                                        GLsizei count, const GLuint* value) {
  std::vector<GLint> tmp;
  if (value != nullptr && count > 0) {
    tmp.reserve(static_cast<std::size_t>(count) * 4);
    for (GLsizei i = 0; i < count * 4; ++i) {
      tmp.push_back(static_cast<GLint>(value[i]));
    }
  }
  SetUniformIntFor(program, location, count > 0 ? tmp.data() : nullptr, 4,
                   count, true);
}

void ProgramManager::ProgramUniformMatrix2fv(GLuint program, GLint location,
                                             GLsizei count, GLboolean transpose,
                                             const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat2, 4);
}

void ProgramManager::ProgramUniformMatrix3fv(GLuint program, GLint location,
                                             GLsizei count, GLboolean transpose,
                                             const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat3, 9);
}

void ProgramManager::ProgramUniformMatrix4fv(GLuint program, GLint location,
                                             GLsizei count, GLboolean transpose,
                                             const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat4, 16);
}

void ProgramManager::ProgramUniformMatrix2x3fv(GLuint program, GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat2x3, 6);
}

void ProgramManager::ProgramUniformMatrix3x2fv(GLuint program, GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat3x2, 6);
}

void ProgramManager::ProgramUniformMatrix2x4fv(GLuint program, GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat2x4, 8);
}

void ProgramManager::ProgramUniformMatrix4x2fv(GLuint program, GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat4x2, 8);
}

void ProgramManager::ProgramUniformMatrix3x4fv(GLuint program, GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat3x4, 12);
}

void ProgramManager::ProgramUniformMatrix4x3fv(GLuint program, GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat* value) {
  if (transpose != kGlFalse) {
    if (location < 0) return;
    errors_.Record(kGlInvalidValue);
    return;
  }
  SetUniformMatrixFor(program, location, value, count, kGlFloatMat4x3, 12);
}

// --- Uniform reads (spec 7.12): values convert freely, zeros when unset ------

bool ProgramManager::GetUniformFloats(GLuint program, GLint location,
                                      std::vector<float>* out) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    // Spec 7.12 (verified in es_spec_3.2.pdf): a name that is neither
    // program nor shader is INVALID_VALUE (older query methods keep their
    // own INVALID_OPERATION convention — untouched).
    errors_.Record(kGlInvalidValue);
    return false;
  }
  if (!prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return false;
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
    return false;
  }
  const std::size_t n = UniformFloatCount(target->type);
  out->assign(n, 0.0f);  // Unset locations read back zeros (spec 7.13).
  auto it = prog->uniform_f.find(location);
  if (it != prog->uniform_f.end()) {
    const std::size_t have = std::min(n, it->second.size());
    for (std::size_t i = 0; i < have; ++i) (*out)[i] = it->second[i];
  }
  return true;
}

bool ProgramManager::GetUniformElementFloats(GLuint program, GLint location,
                                             std::vector<float>* out) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  if (!prog->linked || !prog->link_status) {
    errors_.Record(kGlInvalidOperation);
    return false;
  }
  const ActiveUniform* target = nullptr;
  for (const auto& u : prog->uniforms) {
    if (location >= u.base_location &&
        location < u.base_location + u.array_size) {
      target = &u;
      break;
    }
  }
  if (target == nullptr || out == nullptr) {
    if (target == nullptr) errors_.Record(kGlInvalidOperation);
    if (out == nullptr && target != nullptr) errors_.Record(kGlInvalidValue);
    return false;
  }
  const std::size_t n = UniformFloatCount(target->type);
  out->assign(n, 0.0f);
  // Exact per-location store first (Uniform*(base+elem,1) path).
  auto it = prog->uniform_f.find(location);
  if (it != prog->uniform_f.end()) {
    const std::size_t have = std::min(n, it->second.size());
    for (std::size_t i = 0; i < have; ++i) (*out)[i] = it->second[i];
    // If the exact store holds a full element, it wins (spec: last upload).
    if (it->second.size() >= n) return true;
  }
  // Contiguous array store at the base (Uniform*(base,count>1) path): slice.
  const GLint elem = location - target->base_location;
  auto bit = prog->uniform_f.find(target->base_location);
  if (bit != prog->uniform_f.end()) {
    const std::size_t off = static_cast<std::size_t>(elem) * n;
    if (off + n <= bit->second.size()) {
      for (std::size_t i = 0; i < n; ++i) (*out)[i] = bit->second[off + i];
      return true;
    }
    // Base holds a single element but we ask for elem>0: zeros (unset).
    if (elem == 0 && bit->second.size() > 0) {
      const std::size_t have = std::min(n, bit->second.size());
      for (std::size_t i = 0; i < have; ++i) (*out)[i] = bit->second[i];
    }
  }
  return true;
}

bool ProgramManager::HasUniformValue(GLuint program, GLint location) const {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) return false;
  return prog->uniform_f.find(location) != prog->uniform_f.end();
}

int ProgramManager::UniformFloatCount(GLenum type) {
  switch (type) {
    case kGlFloat:
    case kGlInt:
    case kGlUnsignedInt:
    case kGlBool:
      return 1;
    case kGlFloatVec2:
    case kGlIntVec2:
    case kGlBoolVec2:
      return 2;
    case kGlFloatVec3:
    case kGlIntVec3:
    case kGlBoolVec3:
      return 3;
    case kGlFloatVec4:
    case kGlIntVec4:
    case kGlBoolVec4:
    case kGlFloatMat2:
      return 4;
    case kGlFloatMat3:
      return 9;
    case kGlFloatMat4:
      return 16;
    case kGlFloatMat2x3:
    case kGlFloatMat3x2:
      return 6;
    case kGlFloatMat2x4:
    case kGlFloatMat4x2:
      return 8;
    case kGlFloatMat3x4:
    case kGlFloatMat4x3:
      return 12;
    default:
      break;
  }
  if (type == 0x8DC6 || type == 0x8DC7 || type == 0x8DC8) {
    return (type == 0x8DC6) ? 2 : (type == 0x8DC7 ? 3 : 4);
  }
  if (type == kGlSampler2d || type == kGlSampler3d ||
      type == kGlSamplerCube || type == kGlSampler2dArray) {
    return 1;
  }
  return 1;  // Unknown type: one float keeps the read total.
}

// --- Reflection over the link parse (spec 7.3.2) --------------------------------
// Error convention for these NEW queries (verified against es_spec_3.2.pdf
// p.152 for GetUniform*): a name that is neither program nor shader is
// INVALID_VALUE; a dead index or unknown property is INVALID_VALUE for
// index-style queries and INVALID_ENUM for unknown properties. Older methods
// (GetProgramiv etc.) keep their own convention — untouched.

void ProgramManager::GetUniformIndices(GLuint program, GLsizei uniform_count,
                                       const char* const* names,
                                       GLuint* indices) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (uniform_count < 0 ||
      (uniform_count > 0 && (names == nullptr || indices == nullptr))) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  for (GLsizei i = 0; i < uniform_count; ++i) {
    indices[i] = kGlInvalidIndex;
    if (names[i] == nullptr) continue;
    // Accept "name" and "name[N]" (element of an active array names the
    // uniform itself, per spec 7.3.1).
    std::string base(names[i]);
    const std::size_t bracket = base.find('[');
    if (bracket != std::string::npos) base.resize(bracket);
    for (GLuint u = 0; u < prog->uniforms.size(); ++u) {
      if (prog->uniforms[u].name == base) {
        indices[i] = u;
        break;
      }
    }
  }
}

void ProgramManager::GetActiveUniformsiv(GLuint program, GLsizei uniform_count,
                                         const GLuint* indices, GLenum pname,
                                         GLint* params) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (uniform_count < 0 ||
      (uniform_count > 0 && (indices == nullptr || params == nullptr))) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname != kGlUniformTypeQ && pname != kGlUniformSizeQ &&
      pname != kGlArraySizeQ) {
    // BLOCK_INDEX/OFFSET/STRIDEs need block membership the parse does not
    // model: fail closed instead of reporting a wrong -1 (tracked work).
    errors_.Record(kGlInvalidEnum);
    return;
  }
  for (GLsizei i = 0; i < uniform_count; ++i) {
    if (indices[i] >= prog->uniforms.size()) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    const ActiveUniform& u = prog->uniforms[indices[i]];
    if (pname == kGlUniformTypeQ) {
      params[i] = static_cast<GLint>(u.type);
    } else {  // SIZE and ARRAY_SIZE are both the array size here.
      params[i] = u.array_size;
    }
  }
}

void ProgramManager::GetActiveUniformBlockName(GLuint program, GLuint index,
                                               GLsizei buf_size,
                                               GLsizei* length, char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (buf_size < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (index >= prog->uniform_blocks.size()) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const std::string& block = prog->uniform_blocks[index];
  if (length != nullptr) *length = static_cast<GLsizei>(block.size());
  if (name != nullptr && buf_size > 0) {
    std::size_t n = block.size();
    if (n > static_cast<std::size_t>(buf_size - 1)) {
      n = static_cast<std::size_t>(buf_size - 1);
    }
    std::memcpy(name, block.data(), n);
    name[n] = '\0';
  }
}

void ProgramManager::GetActiveUniformBlockiv(GLuint program, GLuint index,
                                             GLenum pname, GLint* params) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (index >= prog->uniform_blocks.size()) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (pname == kGlUniformBlockBindingQ) {
    auto it = prog->uniform_block_bindings.find(index);
    *params = static_cast<GLint>(it == prog->uniform_block_bindings.end()
                                     ? 0
                                     : it->second);
    return;
  }
  if (pname == kGlUniformBlockNameLengthQ) {
    *params =
        static_cast<GLint>(prog->uniform_blocks[index].size() + 1);
    return;
  }
  // DATA_SIZE / ACTIVE_UNIFORMS / member lists need block layout the parse
  // does not model: fail closed (tracked work).
  errors_.Record(kGlInvalidEnum);
}

void ProgramManager::GetProgramInterfaceiv(GLuint program,
                                           GLenum program_interface,
                                           GLenum pname, GLint* params) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  std::size_t resources = 0;
  bool known_interface = true;
  switch (program_interface) {
    case kGlUniformInterface:
      resources = prog->uniforms.size();
      break;
    case kGlUniformBlockInterface:
      resources = prog->uniform_blocks.size();
      break;
    case kGlProgramInputQ:
      resources = prog->attribs.size();
      break;
    default:
      known_interface = false;
      break;
  }
  if (!known_interface) {
    // PROGRAM_OUTPUT (fragment outs unparsed), BUFFER_VARIABLE/SSBO and
    // everything else the parse does not model: fail closed (tracked work).
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (pname == kGlActiveResourcesQ) {
    *params = static_cast<GLint>(resources);
    return;
  }
  if (pname == kGlMaxNameLengthQ) {
    std::size_t longest = 0;
    if (program_interface == kGlUniformInterface) {
      for (const auto& u : prog->uniforms) {
        longest = std::max(longest, u.name.size() + 1);
      }
    } else if (program_interface == kGlUniformBlockInterface) {
      for (const auto& b : prog->uniform_blocks) {
        longest = std::max(longest, b.size() + 1);
      }
    } else {
      for (const auto& a : prog->attribs) {
        longest = std::max(longest, a.name.size() + 1);
      }
    }
    *params = static_cast<GLint>(longest);
    return;
  }
  errors_.Record(kGlInvalidEnum);
}

void ProgramManager::GetProgramResourceName(GLuint program,
                                            GLenum program_interface,
                                            GLuint index, GLsizei buf_size,
                                            GLsizei* length, char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (buf_size < 0) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  std::string text;
  if (program_interface == kGlUniformInterface) {
    if (index >= prog->uniforms.size()) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    text = prog->uniforms[index].name;
    if (prog->uniforms[index].array_size > 1) text += "[0]";
  } else if (program_interface == kGlUniformBlockInterface) {
    if (index >= prog->uniform_blocks.size()) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    text = prog->uniform_blocks[index];
  } else if (program_interface == kGlProgramInputQ) {
    if (index >= prog->attribs.size()) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    text = prog->attribs[index].name;
  } else {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (length != nullptr) *length = static_cast<GLsizei>(text.size());
  if (name != nullptr && buf_size > 0) {
    std::size_t n = text.size();
    if (n > static_cast<std::size_t>(buf_size - 1)) {
      n = static_cast<std::size_t>(buf_size - 1);
    }
    std::memcpy(name, text.data(), n);
    name[n] = '\0';
  }
}

GLint ProgramManager::GetProgramResourceLocation(GLuint program,
                                                 GLenum program_interface,
                                                 const char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidValue);
    return -1;
  }
  if (program_interface == kGlUniformInterface) {
    return GetUniformLocation(program, name);
  }
  if (program_interface == kGlProgramInputQ) {
    return GetAttribLocation(program, name);
  }
  if (program_interface == kGlUniformBlockInterface) {
    return -1;  // Blocks have locations only via bindings, not numbers.
  }
  errors_.Record(kGlInvalidEnum);
  return -1;
}

void ProgramManager::GetProgramResourceiv(GLuint program,
                                          GLenum program_interface,
                                          GLuint index, GLsizei prop_count,
                                          const GLenum* props, GLsizei buf_size,
                                          GLsizei* length, GLint* params) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (prop_count < 0 || buf_size < 0 ||
      (prop_count > 0 && (props == nullptr || params == nullptr))) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (length != nullptr) *length = 0;
  const ActiveUniform* uniform = nullptr;
  const ActiveAttrib* attrib = nullptr;
  if (program_interface == kGlUniformInterface) {
    if (index >= prog->uniforms.size()) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    uniform = &prog->uniforms[index];
  } else if (program_interface == kGlProgramInputQ) {
    if (index >= prog->attribs.size()) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    attrib = &prog->attribs[index];
  } else {
    // BLOCK (no member layout) and everything unparsed: fail closed.
    errors_.Record(kGlInvalidEnum);
    return;
  }
  for (GLsizei i = 0; i < prop_count; ++i) {
    if (i >= buf_size) break;  // Truncate like a robust query.
    const GLenum prop = props[i];
    if (prop == kGlUniformTypeQ) {
      params[i] = static_cast<GLint>(uniform != nullptr ? uniform->type
                                                       : attrib->type);
    } else if (prop == kGlUniformSizeQ || prop == kGlArraySizeQ) {
      params[i] = uniform != nullptr ? uniform->array_size
                                     : attrib->array_size;
    } else if (prop == kGlLocationQ) {
      params[i] = uniform != nullptr ? uniform->base_location
                                     : attrib->location;
    } else {
      // BLOCK_INDEX (no membership data), REFERENCED_BY_*, OFFSET/STRIDE
      // and everything else unmodeled: fail closed (tracked work).
      errors_.Record(kGlInvalidEnum);
      return;
    }
  }
  if (length != nullptr) *length = prop_count;
}

void ProgramManager::GetAttachedShaders(GLuint program, GLsizei max_count,
                                        GLsizei* count, GLuint* shaders) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (max_count < 0 || (max_count > 0 && shaders == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  const std::size_t n =
      std::min(static_cast<std::size_t>(max_count), prog->attached.size());
  for (std::size_t i = 0; i < n; ++i) shaders[i] = prog->attached[i];
  if (count != nullptr) *count = static_cast<GLsizei>(prog->attached.size());
}

GLint ProgramManager::GetFragDataLocation(GLuint program, const char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidValue);
    return -1;
  }
  auto it = prog->frag_bindings.find(name);
  if (it != prog->frag_bindings.end()) {
    return static_cast<GLint>(it->second);
  }
  // Unbound names (no EXT binding, no layout(location) parse) read -1.
  return -1;
}

GLuint ProgramManager::CreateShaderProgramv(GLenum type, GLsizei count,
                                            const char* const* strings) {
  GLenum shader_type = 0;
  switch (type) {
    case kGlVertexShader:
    case kGlTessControlShader:
    case kGlTessEvaluationShader:
    case kGlGeometryShader:
    case kGlFragmentShader:
    case kGlComputeShader:
      shader_type = type;
      break;
    default:
      errors_.Record(kGlInvalidEnum);
      return 0;
  }
  if (count < 0 || (count > 0 && strings == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return 0;
  }
  const GLuint shader = shaders_->CreateShader(shader_type);
  if (shader == 0) return 0;  // Error already recorded.
  // ShaderSource takes const char** (legacy signature); it only reads.
  // Same const_cast the host_c_api.cpp wrapper already uses.
  shaders_->ShaderSource(shader, count, const_cast<const char**>(strings),
                         nullptr);
  shaders_->CompileShader(shader);
  const GLuint program = CreateProgram();
  AttachShader(program, shader);
  LinkProgram(program);
  // Flagged after linking: queries on a flagged shader fail, but the linked
  // program keeps working (spec 7.1 delete-while-attached rule). Deleting
  // before LinkProgram would poison CompileSucceeded (it requires alive).
  shaders_->DeleteShader(shader);
  return program;  // Returned even when compile/link failed (spec 7.1).
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

bool ProgramManager::GetGraphicsSources(GLuint program, std::string* vs,
                                         std::string* fs) {
  Program* prog = FindProgram(program);
  if (prog == nullptr || !prog->linked || !prog->link_status) return false;
  bool got_vs = false, got_fs = false;
  for (GLuint s : prog->attached) {
    if (shaders_->IsShader(s) == kGlFalse) continue;
    const GLenum type = shaders_->GetType(s);
    if (type == kGlVertexShader && !got_vs) {
      if (vs != nullptr) *vs = shaders_->GetShaderSource(s);
      got_vs = true;
    } else if (type == kGlFragmentShader && !got_fs) {
      if (fs != nullptr) *fs = shaders_->GetShaderSource(s);
      got_fs = true;
    }
  }
  return got_vs && got_fs;
}

bool ProgramManager::IsComputeAddOneProgram(GLuint program) {
  Program* prog = FindProgram(program);
  if (prog == nullptr || !prog->linked || !prog->link_status) return false;
  for (GLuint s : prog->attached) {
    if (shaders_->GetType(s) != kGlComputeShader) continue;
    if (shaders_->IsShader(s) == kGlFalse) continue;
    const std::string src = shaders_->GetShaderSource(s);
    // GetShaderSource records only when the name is dead (checked above),
    // so a valid source query leaves the error queues untouched.
    if (src.find("TGL_COMPUTE_ADD_ONE_SSBO0") != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<ActiveUniform> ProgramManager::Uniforms(GLuint program) const {
  const Program* prog = FindProgram(program);
  return prog == nullptr ? std::vector<ActiveUniform>() : prog->uniforms;
}

bool ProgramManager::GetUniformBlockBinding(GLuint program,
                                             GLuint uniform_block_index,
                                             GLuint* binding) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr || binding == nullptr) {
    errors_.Record(kGlInvalidValue);
    return false;
  }
  auto it = prog->uniform_block_bindings.find(uniform_block_index);
  if (it == prog->uniform_block_bindings.end()) return false;
  *binding = it->second;
  return true;
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

void ProgramManager::GetProgramPipelineiv(GLuint pipeline, GLenum pname,
                                          GLint* params) {
  static constexpr GLenum kActiveProgram = 0x8259u;
  static constexpr GLenum kVertexShader = 0x8B31u;
  static constexpr GLenum kFragmentShader = 0x8B30u;
  static constexpr GLenum kValidateStatus = 0x8B83u;
  static constexpr GLenum kInfoLogLength = 0x8B84u;
  if (params == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  auto it = pipelines_.find(pipeline);
  if (it == pipelines_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  const Pipeline& p = it->second;
  switch (pname) {
    case kActiveProgram:
      *params = static_cast<GLint>(p.active_program);
      return;
    case kVertexShader:
      *params = static_cast<GLint>(p.vertex);
      return;
    case kFragmentShader:
      *params = static_cast<GLint>(p.fragment);
      return;
    case kValidateStatus:
      *params = p.validate_status ? 1 : 0;
      return;
    case kInfoLogLength:
      *params = static_cast<GLint>(p.info_log.size() + 1);
      return;
    default:
      break;
  }
  errors_.Record(kGlInvalidEnum);
}

std::string ProgramManager::GetProgramPipelineInfoLog(GLuint pipeline) {
  auto it = pipelines_.find(pipeline);
  if (it == pipelines_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return {};
  }
  return it->second.info_log;
}

void ProgramManager::ValidateProgramPipeline(GLuint pipeline) {
  auto it = pipelines_.find(pipeline);
  if (it == pipelines_.end() || !it->second.alive) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  // All staged programs must be separable + linked; else validate fails but
  // records no GL error (spec 7.4: validate status, not error).
  Pipeline& p = it->second;
  bool ok = true;
  const GLuint stages[] = {p.vertex, p.fragment, p.geometry, p.tess_control,
                           p.tess_evaluation, p.compute, p.active_program};
  for (GLuint prog : stages) {
    if (prog == 0) continue;
    const Program* pr = FindProgram(prog);
    if (pr == nullptr || !pr->linked || !pr->link_status || !pr->separable) {
      ok = false;
      break;
    }
  }
  p.validate_status = ok;
  p.info_log = ok ? "" : "error: pipeline stages not separable/linked";
}

void ProgramManager::TransformFeedbackVaryings(GLuint program, GLsizei count,
                                               const char* const* varyings,
                                               GLenum bufferMode) {
  static constexpr GLenum kInterleaved = 0x8C8Cu;
  static constexpr GLenum kSeparate = 0x8C8Du;
  Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (prog->linked) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (bufferMode != kInterleaved && bufferMode != kSeparate) {
    errors_.Record(kGlInvalidEnum);
    return;
  }
  if (count < 0 || (count > 0 && varyings == nullptr)) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  prog->tf_varyings.clear();
  for (GLsizei i = 0; i < count; ++i) {
    if (varyings[i] == nullptr) continue;
    prog->tf_varyings.emplace_back(varyings[i]);
  }
  prog->tf_buffer_mode = bufferMode;
}

void ProgramManager::GetTransformFeedbackVarying(GLuint program, GLuint index,
                                                 GLsizei bufSize,
                                                 GLsizei* length, GLsizei* size,
                                                 GLenum* type, char* name) {
  const Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (index >= prog->tf_varyings.size()) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  // TGL link parse does not capture varying types; report vec4 size 1.
  if (size != nullptr) *size = 1;
  if (type != nullptr) *type = kGlFloatVec4;
  const std::string& v = prog->tf_varyings[index];
  if (length != nullptr) *length = static_cast<GLsizei>(v.size());
  if (name != nullptr && bufSize > 0) {
    std::size_t n = v.size();
    if (n > static_cast<std::size_t>(bufSize - 1)) n = bufSize - 1;
    std::memcpy(name, v.data(), n);
    name[n] = '\0';
  }
}

void ProgramManager::ShaderStorageBlockBinding(GLuint program,
                                               GLuint storageBlockIndex,
                                               GLuint storageBlockBinding) {
  Program* prog = FindProgram(program);
  if (prog == nullptr) {
    errors_.Record(kGlInvalidValue);
    return;
  }
  if (storageBlockBinding >= 8u) {  // kMaxShaderStorageBufferBindings.
    errors_.Record(kGlInvalidValue);
    return;
  }
  prog->ssbo_bindings[storageBlockIndex] = storageBlockBinding;
}

void ProgramManager::BindFragDataLocationEXT(GLuint program, GLuint color,
                                             const char* name) {
  Program* prog = FindProgram(program);
  if (prog == nullptr || name == nullptr) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  if (prog->linked) {
    errors_.Record(kGlInvalidOperation);
    return;
  }
  prog->frag_bindings[name] = color;
}

}  // namespace tgles
