#ifndef TGLES_PROGRAM_H
#define TGLES_PROGRAM_H

// Program objects (spec 7.3-7.6): attach/link/use, active introspection by
// parsing uniform/attribute declarations, type-checked Uniform* setters,
// program binaries and separable programs + pipeline objects (spec 7.4).

#include <map>
#include <string>
#include <vector>

#include "tgles/base/error.h"
#include "tgles/base/gl_types.h"
#include "tgles/state/shader.h"

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
// GetActiveUniformsiv / GetProgramResourceiv property names (gl32.h,
// verified against docs/reference/gl32.h).
inline constexpr GLenum kGlUniformTypeQ = 0x8A37;
inline constexpr GLenum kGlUniformSizeQ = 0x8A38;
inline constexpr GLenum kGlUniformBlockBindingQ = 0x8A3F;
inline constexpr GLenum kGlUniformBlockNameLengthQ = 0x8A41;
inline constexpr GLenum kGlProgramInputQ = 0x92E3;
inline constexpr GLenum kGlProgramOutputQ = 0x92E4;
inline constexpr GLenum kGlActiveResourcesQ = 0x92F5;
inline constexpr GLenum kGlMaxNameLengthQ = 0x92F6;
inline constexpr GLenum kGlArraySizeQ = 0x92FB;
inline constexpr GLenum kGlBlockIndexQ = 0x92FD;
inline constexpr GLenum kGlLocationQ = 0x930E;
// Non-square matrix types (gl32.h; the link parser only yields mat2/3/4, so
// these setters validate without ever matching — fail closed, not silent).
inline constexpr GLenum kGlFloatMat2x3 = 0x8B65;
inline constexpr GLenum kGlFloatMat2x4 = 0x8B66;
inline constexpr GLenum kGlFloatMat3x2 = 0x8B67;
inline constexpr GLenum kGlFloatMat3x4 = 0x8B68;
inline constexpr GLenum kGlFloatMat4x2 = 0x8B69;
inline constexpr GLenum kGlFloatMat4x3 = 0x8B6A;

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
  // Every shape validates against the linked uniform's type exactly like the
  // five founders below; the *For variants below take an explicit program
  // for the ProgramUniform* entry points (same rules, no current program
  // needed).
  void Uniform1f(GLint location, GLfloat v0);
  void Uniform2f(GLint location, GLfloat v0, GLfloat v1);
  void Uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
  void Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                 GLfloat v3);
  void Uniform1i(GLint location, GLint v0);
  void Uniform2i(GLint location, GLint v0, GLint v1);
  void Uniform3i(GLint location, GLint v0, GLint v1, GLint v2);
  void Uniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
  void Uniform1ui(GLint location, GLuint v0);
  void Uniform2ui(GLint location, GLuint v0, GLuint v1);
  void Uniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2);
  void Uniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3);
  void Uniform1fv(GLint location, GLsizei count, const GLfloat* value);
  void Uniform2fv(GLint location, GLsizei count, const GLfloat* value);
  void Uniform3fv(GLint location, GLsizei count, const GLfloat* value);
  void Uniform4fv(GLint location, GLsizei count, const GLfloat* value);
  void Uniform1iv(GLint location, GLsizei count, const GLint* value);
  void Uniform2iv(GLint location, GLsizei count, const GLint* value);
  void Uniform3iv(GLint location, GLsizei count, const GLint* value);
  void Uniform4iv(GLint location, GLsizei count, const GLint* value);
  void Uniform1uiv(GLint location, GLsizei count, const GLuint* value);
  void Uniform2uiv(GLint location, GLsizei count, const GLuint* value);
  void Uniform3uiv(GLint location, GLsizei count, const GLuint* value);
  void Uniform4uiv(GLint location, GLsizei count, const GLuint* value);
  void UniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value);
  void UniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value);
  void UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose,
                        const GLfloat* value);
  void UniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value);
  void UniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value);
  void UniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value);
  void UniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value);
  void UniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value);
  void UniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose,
                          const GLfloat* value);
  // Program-id variants for glProgramUniform* (spec 7.3.1): same validation
  // against the NAMED program (which must exist and be linked), so a launcher
  // can set uniforms without disturbing the current program.
  void ProgramUniform1f(GLuint program, GLint location, GLfloat v0);
  void ProgramUniform2f(GLuint program, GLint location, GLfloat v0,
                        GLfloat v1);
  void ProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1,
                        GLfloat v2);
  void ProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1,
                        GLfloat v2, GLfloat v3);
  void ProgramUniform1i(GLuint program, GLint location, GLint v0);
  void ProgramUniform2i(GLuint program, GLint location, GLint v0, GLint v1);
  void ProgramUniform3i(GLuint program, GLint location, GLint v0, GLint v1,
                        GLint v2);
  void ProgramUniform4i(GLuint program, GLint location, GLint v0, GLint v1,
                        GLint v2, GLint v3);
  void ProgramUniform1ui(GLuint program, GLint location, GLuint v0);
  void ProgramUniform2ui(GLuint program, GLint location, GLuint v0,
                         GLuint v1);
  void ProgramUniform3ui(GLuint program, GLint location, GLuint v0, GLuint v1,
                         GLuint v2);
  void ProgramUniform4ui(GLuint program, GLint location, GLuint v0, GLuint v1,
                         GLuint v2, GLuint v3);
  void ProgramUniform1fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value);
  void ProgramUniform2fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value);
  void ProgramUniform3fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value);
  void ProgramUniform4fv(GLuint program, GLint location, GLsizei count,
                         const GLfloat* value);
  void ProgramUniform1iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value);
  void ProgramUniform2iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value);
  void ProgramUniform3iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value);
  void ProgramUniform4iv(GLuint program, GLint location, GLsizei count,
                         const GLint* value);
  void ProgramUniform1uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value);
  void ProgramUniform2uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value);
  void ProgramUniform3uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value);
  void ProgramUniform4uiv(GLuint program, GLint location, GLsizei count,
                          const GLuint* value);
  void ProgramUniformMatrix2fv(GLuint program, GLint location, GLsizei count,
                               GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count,
                               GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count,
                               GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix2x3fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix3x2fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix2x4fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix4x2fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix3x4fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value);
  void ProgramUniformMatrix4x3fv(GLuint program, GLint location, GLsizei count,
                                 GLboolean transpose, const GLfloat* value);
  // Uniform reads (spec 7.12 GetUniform*/GetnUniform*): values convert freely
  // between float/int/uint (spec lists no type-mismatch error — only program,
  // location and bufSize errors). Unset locations read back zeros (spec 7.13
  // initial values). Returns false + records when the program/location is
  // bad so the C ABI can tell "no data" from "zeros".
  bool GetUniformFloats(GLuint program, GLint location,
                        std::vector<float>* out);
  // Per-element array read (Phase 4 item 7): `location` may be base+elem;
  // slices element `elem` out of the contiguous array store at the base
  // (Uniform4fv(base,2) keeps 8 floats at base). Falls back to the exact
  // per-location store when the app uploaded via base+elem,count=1. Unset
  // elements read zeros like GetUniformFloats. False + records on bad
  // program/location.
  bool GetUniformElementFloats(GLuint program, GLint location,
                               std::vector<float>* out);
  // True when the location holds an explicitly uploaded value (spec 7.13:
  // unset locations read back zeros, but a mat4 zero matrix would collapse
  // geometry — the facade substitutes identity for unset mat4s).
  bool HasUniformValue(GLuint program, GLint location) const;
  // Reflection over the link parse (spec 7.3.2): indices, properties, block
  // names and program-interface counts. BLOCK_INDEX has no membership data
  // in the parse (see ParseLink), so queries naming it fail closed with
  // INVALID_ENUM instead of reporting a wrong -1 — tracked work, not a lie.
  void GetUniformIndices(GLuint program, GLsizei uniform_count,
                         const char* const* names, GLuint* indices);
  void GetActiveUniformsiv(GLuint program, GLsizei uniform_count,
                           const GLuint* indices, GLenum pname, GLint* params);
  void GetActiveUniformBlockName(GLuint program, GLuint index, GLsizei buf_size,
                                 GLsizei* length, char* name);
  void GetActiveUniformBlockiv(GLuint program, GLuint index, GLenum pname,
                               GLint* params);
  void GetProgramInterfaceiv(GLuint program, GLenum program_interface,
                             GLenum pname, GLint* params);
  void GetProgramResourceName(GLuint program, GLenum program_interface,
                              GLuint index, GLsizei buf_size, GLsizei* length,
                              char* name);
  GLint GetProgramResourceLocation(GLuint program, GLenum program_interface,
                                   const char* name);
  void GetProgramResourceiv(GLuint program, GLenum program_interface,
                            GLuint index, GLsizei prop_count,
                            const GLenum* props, GLsizei buf_size,
                            GLsizei* length, GLint* params);
  void GetAttachedShaders(GLuint program, GLsizei max_count, GLsizei* count,
                          GLuint* shaders);
  // Fragment-data locations have no binding storage (BindFragDataLocationEXT
  // is itself a gap), so every name is unbound: -1 with no error.
  GLint GetFragDataLocation(GLuint program, const char* name);
  // One-shot program build (spec 7.1/7.3): creates, sources, compiles,
  // attaches and links; returns 0 (with errors recorded) when any step fails.
  GLuint CreateShaderProgramv(GLenum type, GLsizei count,
                              const char* const* strings);

  // Uniform/SSBO block bindings + program-resource reflection (spec 7.3.2,
  // core in gl32.h; host side for MobileGL SSBO path via GetProgramResource,
  // not desktop-only glShaderStorageBlockBinding).
  void UniformBlockBinding(GLuint program, GLuint uniform_block_index,
                           GLuint uniform_block_binding);
  // Reads back a block binding for the facade UBO upload (buffer 2+i).
  // False + records when the program/block is unknown.
  bool GetUniformBlockBinding(GLuint program, GLuint uniform_block_index,
                              GLuint* binding);
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
  // Pipeline queries (spec 7.4, gl32.h): ACTIVE_PROGRAM, stage bindings,
  // VALIDATE_STATUS, INFO_LOG_LENGTH. Unknown pipeline -> INVALID_OPERATION,
  // unknown pname -> INVALID_ENUM, null params -> INVALID_VALUE.
  void GetProgramPipelineiv(GLuint pipeline, GLenum pname, GLint* params);
  std::string GetProgramPipelineInfoLog(GLuint pipeline);
  void ValidateProgramPipeline(GLuint pipeline);
  // Transform-feedback varyings (spec 12.1): recorded pre-link, queried via
  // GetTransformFeedbackVarying. Must precede LinkProgram; bad mode ->
  // INVALID_ENUM; linked program -> INVALID_OPERATION.
  void TransformFeedbackVaryings(GLuint program, GLsizei count,
                                 const char* const* varyings, GLenum bufferMode);
  void GetTransformFeedbackVarying(GLuint program, GLuint index, GLsizei bufSize,
                                   GLsizei* length, GLsizei* size, GLenum* type,
                                   char* name);
  // SSBO block binding (spec 7.3.2 core via GetProgramResource, desktop
  // glShaderStorageBlockBinding required by MobileGL loader by name).
  void ShaderStorageBlockBinding(GLuint program, GLuint storageBlockIndex,
                                 GLuint storageBlockBinding);
  // Frag-data location binding for EXT (loader-required name; ES core uses
  // layout(location=) instead). Stored pre-link, read by GetFragDataLocation.
  void BindFragDataLocationEXT(GLuint program, GLuint color, const char* name);

  // Test helpers.
  bool LinkSucceeded(GLuint program) const;
  std::vector<ActiveUniform> Uniforms(GLuint program) const;
  // Link-parse active vertex attributes (name + assigned location).
  std::vector<ActiveAttrib> Attribs(GLuint program) const;
  // True when a linked program contains the given shader stage.
  bool ProgramHasStage(GLuint program, GLenum shader_type) const;
  // Honest compute subset (ES 3.2 ch.19): true when the program's attached
  // compute shader source contains `TGL_COMPUTE_ADD_ONE_SSBO0`. The facade
  // executes exactly this pattern (SSBO0 uint32 += 1); other compute programs
  // validate without store mutation (documented, never miscompiled).
  bool IsComputeAddOneProgram(GLuint program);
  // Graphics sources for the GLSL->MSL translator: first attached vertex +
  // first attached fragment source of a linked program. False when either
  // stage is missing/unlinked (the facade keeps the legacy passthrough then).
  bool GetGraphicsSources(GLuint program, std::string* vs, std::string* fs);

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
    std::map<GLuint, GLuint> ssbo_bindings;
    std::map<std::string, GLuint> frag_bindings;
    std::vector<std::string> tf_varyings;
    GLenum tf_buffer_mode = 0x8C8Cu;  // INTERLEAVED_ATTRIBS default.
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
    bool validate_status = false;
    std::string info_log;
  };

  static GLenum UniformTypeFromName(const std::string& type_name);
  static bool IsUniformScalarCommandFor(GLenum uniform_type, int comps,
                                         bool is_float, bool is_unsigned);
  // Float count for one element of a uniform type (matrices column-major).
  static int UniformFloatCount(GLenum uniform_type);
  void ParseLink(Program& prog);
  Program* FindProgram(GLuint program);
  const Program* FindProgram(GLuint program) const;
  void SetUniformFloat(GLint location, const GLfloat* values, int comps,
                       GLsizei count);
  // Explicit-program variants backing both Uniform* (current program) and
  // ProgramUniform* (named program). int values store as float (exact for
  // small ints; reads convert back — see GetUniformFloats).
  void SetUniformFloatFor(GLuint program, GLint location,
                          const GLfloat* values, int comps, GLsizei count);
  void SetUniformIntFor(GLuint program, GLint location, const GLint* values,
                        int comps, GLsizei count, bool is_unsigned);
  void SetUniformMatrixFor(GLuint program, GLint location,
                           const GLfloat* value, GLsizei count,
                           GLenum mat_type, int float_count);

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
