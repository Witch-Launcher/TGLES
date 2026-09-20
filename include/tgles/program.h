#ifndef TGLES_PROGRAM_H
#define TGLES_PROGRAM_H

// Program objects (spec 7.3-7.6): attach/link/use, active introspection by
// parsing uniform/attribute declarations, type-checked Uniform* setters,
// program binaries and separable programs + pipeline objects (spec 7.4).

#include <map>
#include <string>
#include <vector>

#include "tgles/error.h"
#include "tgles/gl_types.h"
#include "tgles/shader.h"

namespace tgles {

// Program queries.
inline constexpr GLenum kGlLinkStatus = 0x8B82;
inline constexpr GLenum kGlValidateStatus = 0x8B83;
inline constexpr GLenum kGlAttachedShaders = 0x8B85;
inline constexpr GLenum kGlActiveAttributes = 0x8B89;
inline constexpr GLenum kGlActiveUniforms = 0x8B86;
inline constexpr GLenum kGlActiveUniformBlocks = 0x8A36;
inline constexpr GLenum kGlProgramSeparable = 0x8258;
inline constexpr GLenum kGlNumProgramBinaryFormats = 0x8DF9;
inline constexpr GLenum kGlProgramBinaryFormats = 0x8DF8;

// Uniform types (subset used by the parser).
inline constexpr GLenum kGlFloatVec2 = 0x8B50;
inline constexpr GLenum kGlFloatVec3 = 0x8B51;
inline constexpr GLenum kGlFloatVec4 = 0x8B52;
inline constexpr GLenum kGlIntVec2 = 0x8B53;
inline constexpr GLenum kGlIntVec3 = 0x8B54;
inline constexpr GLenum kGlIntVec4 = 0x8B55;
inline constexpr GLenum kGlBool = 0x8B56;
inline constexpr GLenum kGlBoolVec2 = 0x8B57;
inline constexpr GLenum kGlBoolVec3 = 0x8B58;
inline constexpr GLenum kGlBoolVec4 = 0x8B59;
inline constexpr GLenum kGlFloatMat2 = 0x8B5A;
inline constexpr GLenum kGlFloatMat3 = 0x8B5B;
inline constexpr GLenum kGlFloatMat4 = 0x8B5C;
inline constexpr GLenum kGlSampler2d = 0x8B5E;
inline constexpr GLenum kGlSampler3d = 0x8B5F;
inline constexpr GLenum kGlSamplerCube = 0x8B60;
inline constexpr GLenum kGlSampler2dArray = 0x8DC1;

// Shader stage bits for UseProgramStages.
inline constexpr GLuint kGlVertexShaderBit = 0x00000001;
inline constexpr GLuint kGlFragmentShaderBit = 0x00000002;
inline constexpr GLuint kGlGeometryShaderBit = 0x00000004;
inline constexpr GLuint kGlTessControlShaderBit = 0x00000008;
inline constexpr GLuint kGlTessEvaluationShaderBit = 0x00000010;
inline constexpr GLuint kGlComputeShaderBit = 0x00000020;
inline constexpr GLuint kGlAllShaderBits = 0xFFFFFFFF;

// TGL program binary format (implementation-defined blob, spec 7.5).
inline constexpr GLenum kGlTglProgramBinary = 0x8F00;

// Program interfaces for GetProgramResource* (gl32.h core, spec 7.3.2).
inline constexpr GLenum kGlUniformInterface = 0x92E1;
inline constexpr GLenum kGlUniformBlockInterface = 0x92E2;
inline constexpr GLenum kGlBufferVariableInterface = 0x92E5;
inline constexpr GLenum kGlShaderStorageBlock = 0x92E6;
inline constexpr GLuint kGlInvalidIndex = 0xFFFFFFFFu;

struct ActiveUniform {
  std::string name;
  GLenum type = 0;
  GLint array_size = 1;
  GLint base_location = -1;
};

struct ActiveAttrib {
  std::string name;
  GLenum type = 0;
  GLint array_size = 1;
  GLint location = -1;
};

class ProgramManager {
 public:
  explicit ProgramManager(ShaderManager* shaders);

  GLenum GetError();
  bool HasPending() const;

  GLuint CreateProgram();
  void DeleteProgram(GLuint program);
  GLboolean IsProgram(GLuint program);
  void AttachShader(GLuint program, GLuint shader);
  void DetachShader(GLuint program, GLuint shader);
  void LinkProgram(GLuint program);
  void UseProgram(GLuint program);
  GLuint CurrentProgram() const;
  void ValidateProgram(GLuint program);
  void GetProgramiv(GLuint program, GLenum pname, GLint* params);
  std::string GetProgramInfoLog(GLuint program);

  GLint GetUniformLocation(GLuint program, const char* name);
  GLint GetAttribLocation(GLuint program, const char* name);
  void BindAttribLocation(GLuint program, GLuint index, const char* name);
  void GetActiveUniform(GLuint program, GLuint index, GLsizei buf_size,
                        GLsizei* length, GLint* size, GLenum* type,
                        char* name);
  void GetActiveAttrib(GLuint program, GLuint index, GLsizei buf_size,
                       GLsizei* length, GLint* size, GLenum* type, char* name);

  // Uniform setters act on the current program (location -1 is ignored).
  void Uniform1f(GLint location, GLfloat v0);
  void Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                 GLfloat v3);
  void Uniform1i(GLint location, GLint v0);
  void UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value);
  void Uniform1fv(GLint location, GLsizei count, const GLfloat* value);

  // Uniform/SSBO block bindings + program-resource reflection (spec 7.3.2,
  // core in gl32.h; host side for MobileGL SSBO path via GetProgramResource,
  // not desktop-only glShaderStorageBlockBinding).
  void UniformBlockBinding(GLuint program, GLuint uniform_block_index,
                           GLuint uniform_block_binding);
  GLuint GetProgramResourceIndex(GLuint program, GLenum program_interface,
                                 const char* name);
  GLuint GetUniformBlockIndex(GLuint program, const char* name);
  // Reads 16 floats of a mat4 uniform value on the CURRENT program
  // (RenderFrame MVP lookup). Returns false when there is no current
  // program, the location is unknown, or fewer than 16 floats are set.
  bool GetUniformMatrix(GLint location, GLfloat* out_16);

  // Program binaries (spec 7.5): opaque blobs, never SPIR-V.
  void GetProgramBinary(GLuint program, GLsizei buf_size, GLsizei* length,
                        GLenum* binary_format, void* binary);
  void ProgramBinary(GLuint program, GLenum binary_format, const void* binary,
                     GLsizei length);

  // Separable programs + pipelines (spec 7.4).
  void ProgramParameteri(GLuint program, GLenum pname, GLint value);
  void GenProgramPipelines(GLsizei n, GLuint* pipelines);
  void DeleteProgramPipelines(GLsizei n, const GLuint* pipelines);
  GLboolean IsProgramPipeline(GLuint pipeline);
  void BindProgramPipeline(GLuint pipeline);
  void UseProgramStages(GLuint pipeline, GLbitfield stages, GLuint program);
  void ActiveShaderProgram(GLuint pipeline, GLuint program);

  // Test helpers.
  bool LinkSucceeded(GLuint program) const;
  std::vector<ActiveUniform> Uniforms(GLuint program) const;
  // True when a linked program contains the given shader stage.
  bool ProgramHasStage(GLuint program, GLenum shader_type) const;

 private:
  struct Program {
    bool alive = false;
    std::vector<GLuint> attached;
    bool linked = false;
    bool link_status = false;
    bool validate_status = false;
    std::string info_log;
    bool separable = false;
    std::vector<ActiveUniform> uniforms;
    std::vector<ActiveAttrib> attribs;
    std::vector<std::string> uniform_blocks;
    std::map<GLuint, GLuint> bound_attribs;  // name-hash? no: index by order
    std::map<std::string, GLint> attrib_bindings;  // pre-link bindings
    std::map<GLint, std::vector<float>> uniform_f;
    std::map<GLuint, GLuint> uniform_block_bindings;
    std::vector<std::uint8_t> binary;
  };

  struct Pipeline {
    bool alive = false;
    GLuint vertex = 0;
    GLuint tess_control = 0;
    GLuint tess_evaluation = 0;
    GLuint geometry = 0;
    GLuint fragment = 0;
    GLuint compute = 0;
    GLuint active_program = 0;
  };

  static GLenum UniformTypeFromName(const std::string& type_name);
  static bool IsUniformScalarCommandFor(GLenum uniform_type, int comps,
                                        bool is_float, bool is_unsigned);
  void ParseLink(Program& prog);
  Program* FindProgram(GLuint program);
  const Program* FindProgram(GLuint program) const;
  void SetUniformFloat(GLint location, const GLfloat* values, int comps,
                       GLsizei count);

  ErrorQueue errors_;
  ShaderManager* shaders_;
  GLuint next_program_ = 1;
  GLuint next_pipeline_ = 1;
  std::map<GLuint, Program> programs_;
  std::map<GLuint, Pipeline> pipelines_;
  GLuint current_program_ = 0;
  GLuint bound_pipeline_ = 0;
};

}  // namespace tgles

#endif  // TGLES_PROGRAM_H
