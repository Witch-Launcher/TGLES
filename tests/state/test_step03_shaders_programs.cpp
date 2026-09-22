// Step 3 tests: shader objects (spec 7.1-7.2) and program objects (7.3-7.6)
// with source-parsed introspection, link rules and uniform type checking.

#include "test_framework.h"

#include <string>

#include "tgles/state/program.h"
#include "tgles/state/shader.h"

namespace {

const char* kVert320 = R"(#version 320 es
layout(location=0) in vec4 inPosition;
layout(location=1) in vec4 inColor;
uniform mat4 u_modelViewProj;
out vec4 vColor;
void main() {
  gl_Position = u_modelViewProj * inPosition;
  vColor = inColor;
})";

const char* kFrag320 = R"(#version 320 es
precision mediump float;
in vec4 vColor;
uniform vec4 u_tint;
uniform sampler2D u_tex;
out vec4 outColor;
void main() {
  outColor = vColor * u_tint;
})";

tgles::GLuint Compile(tgles::ShaderManager& sm, tgles::GLenum type,
                      const char* src) {
  tgles::GLuint s = sm.CreateShader(type);
  sm.ShaderSource(s, 1, &src, nullptr);
  sm.CompileShader(s);
  return s;
}

}  // namespace

// --- Shader objects ---

TEST(Step03, CreateShaderRejectsBadType) {
  tgles::ShaderManager sm;
  EXPECT_EQ(sm.CreateShader(0x1234u), 0u);
  EXPECT_EQ(sm.GetError(), tgles::kGlInvalidEnum);
  tgles::GLuint v = sm.CreateShader(tgles::kGlVertexShader);
  EXPECT_NE(v, 0u);
  EXPECT_EQ(sm.IsShader(v), tgles::kGlTrue);
  EXPECT_EQ(sm.IsShader(0), tgles::kGlFalse);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
}

TEST(Step03, CompileChecksVersionAndMain) {
  tgles::ShaderManager sm;
  tgles::GLuint good = Compile(sm, tgles::kGlVertexShader, kVert320);
  EXPECT_TRUE(sm.CompileSucceeded(good));
  EXPECT_EQ(sm.ShaderVersion(good), 320);
  const char* bad_version = "#version 999 es\nvoid main() {}";
  tgles::GLuint bad = Compile(sm, tgles::kGlVertexShader, bad_version);
  EXPECT_FALSE(sm.CompileSucceeded(bad));
  EXPECT_TRUE(!sm.GetShaderInfoLog(bad).empty());
  const char* no_main = "#version 320 es\nuniform float x;";
  tgles::GLuint no_entry = Compile(sm, tgles::kGlFragmentShader, no_main);
  EXPECT_FALSE(sm.CompileSucceeded(no_entry));
  tgles::GLint status = -1;
  sm.GetShaderiv(good, tgles::kGlCompileStatus, &status);
  EXPECT_EQ(status, 1);
  sm.GetShaderiv(bad, tgles::kGlCompileStatus, &status);
  EXPECT_EQ(status, 0);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
}

TEST(Step03, AllSixStagesCreatable) {
  tgles::ShaderManager sm;
  EXPECT_NE(sm.CreateShader(tgles::kGlVertexShader), 0u);
  EXPECT_NE(sm.CreateShader(tgles::kGlTessControlShader), 0u);
  EXPECT_NE(sm.CreateShader(tgles::kGlTessEvaluationShader), 0u);
  EXPECT_NE(sm.CreateShader(tgles::kGlGeometryShader), 0u);
  EXPECT_NE(sm.CreateShader(tgles::kGlFragmentShader), 0u);
  EXPECT_NE(sm.CreateShader(tgles::kGlComputeShader), 0u);
  EXPECT_EQ(sm.GetError(), tgles::kGlNoError);
}

// --- Program link rules ---

TEST(Step03, LinkVertexFragmentPair) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint prog = pm.CreateProgram();
  EXPECT_EQ(pm.IsProgram(prog), tgles::kGlTrue);
  tgles::GLuint v = Compile(sm, tgles::kGlVertexShader, kVert320);
  tgles::GLuint f = Compile(sm, tgles::kGlFragmentShader, kFrag320);
  pm.AttachShader(prog, v);
  pm.AttachShader(prog, f);
  pm.AttachShader(prog, f);  // Duplicate attach is an error.
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidOperation);
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));
  tgles::GLint status = 0;
  pm.GetProgramiv(prog, tgles::kGlLinkStatus, &status);
  EXPECT_EQ(status, 1);
  pm.UseProgram(prog);
  EXPECT_EQ(pm.CurrentProgram(), prog);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
}

TEST(Step03, LinkFailsWithoutVertex) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint prog = pm.CreateProgram();
  tgles::GLuint f = Compile(sm, tgles::kGlFragmentShader, kFrag320);
  pm.AttachShader(prog, f);
  pm.LinkProgram(prog);
  EXPECT_FALSE(pm.LinkSucceeded(prog));
  EXPECT_TRUE(!pm.GetProgramInfoLog(prog).empty());
}

TEST(Step03, LinkFailsOnMixedVersions) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  const char* old_frag = "#version 100 es\nvoid main() {}";
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kVert320));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, old_frag));
  pm.LinkProgram(prog);
  EXPECT_FALSE(pm.LinkSucceeded(prog));
}

TEST(Step03, ComputeMustLinkAlone) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  const char* comp = "#version 320 es\nvoid main() {}";
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlComputeShader, comp));
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kVert320));
  pm.LinkProgram(prog);
  EXPECT_FALSE(pm.LinkSucceeded(prog));
  tgles::GLuint solo = pm.CreateProgram();
  pm.AttachShader(solo, Compile(sm, tgles::kGlComputeShader, comp));
  pm.LinkProgram(solo);
  EXPECT_TRUE(pm.LinkSucceeded(solo));
}

// --- Introspection ---

TEST(Step03, UniformLocationsAndActiveQuery) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kVert320));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, kFrag320));
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));
  tgles::GLint mvp = pm.GetUniformLocation(prog, "u_modelViewProj");
  EXPECT_TRUE(mvp >= 0);
  EXPECT_EQ(pm.GetUniformLocation(prog, "u_modelViewProj"), mvp);
  EXPECT_EQ(pm.GetUniformLocation(prog, "no_such_uniform"), -1);
  EXPECT_EQ(pm.GetUniformLocation(prog, "gl_Position"), -1);  // Built-in.
  tgles::GLint count = 0;
  pm.GetProgramiv(prog, tgles::kGlActiveUniforms, &count);
  // u_modelViewProj (vertex) + u_tint, u_tex (fragment); varyings excluded.
  EXPECT_EQ(count, 3);
  char name[64] = {};
  tgles::GLint size = 0;
  tgles::GLenum type = 0;
  pm.GetActiveUniform(prog, 0, sizeof(name), nullptr, &size, &type, name);
  EXPECT_EQ(size, 1);
  EXPECT_EQ(type, tgles::kGlFloatMat4);
  EXPECT_TRUE(std::string(name) == "u_modelViewProj");
  pm.GetActiveUniform(prog, 99, sizeof(name), nullptr, nullptr, nullptr, name);
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidValue);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
}

TEST(Step03, AttribLocations) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint prog = pm.CreateProgram();
  pm.BindAttribLocation(prog, 5, "inColor");
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kVert320));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, kFrag320));
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));
  // layout(location=1) in the source wins for inColor.
  EXPECT_EQ(pm.GetAttribLocation(prog, "inColor"), 1);
  EXPECT_EQ(pm.GetAttribLocation(prog, "inPosition"), 0);
  EXPECT_EQ(pm.GetAttribLocation(prog, "missing"), -1);
  pm.BindAttribLocation(prog, 99, "inColor");
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidValue);
  // Binding after link is an error.
  pm.BindAttribLocation(prog, 2, "inColor");
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidOperation);
}

// --- Uniform setters ---

TEST(Step03, UniformSettersTypeChecked) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kVert320));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, kFrag320));
  pm.LinkProgram(prog);
  pm.UseProgram(prog);
  tgles::GLint mvp = pm.GetUniformLocation(prog, "u_modelViewProj");
  tgles::GLint tint = pm.GetUniformLocation(prog, "u_tint");
  const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                              0, 0, 1, 0, 0, 0, 0, 1};
  pm.UniformMatrix4fv(mvp, 1, tgles::kGlFalse, identity);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  pm.UniformMatrix4fv(mvp, 1, tgles::kGlTrue, identity);  // Transpose!
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidValue);
  pm.Uniform4f(tint, 1, 1, 1, 1);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  pm.Uniform1i(tint, 1);  // vec4 <- int: type mismatch.
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidOperation);
  pm.Uniform1f(-1, 0.5f);  // -1 is silently ignored.
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  pm.UseProgram(0);
  pm.Uniform1f(tint, 0.5f);  // No current program.
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidOperation);
}

// --- Binaries (never SPIR-V) ---

TEST(Step03, ProgramBinaryRoundTrip) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLint formats = 0;
  tgles::GLuint probe = pm.CreateProgram();
  pm.GetProgramiv(probe, tgles::kGlNumProgramBinaryFormats, &formats);
  EXPECT_EQ(formats, 1);
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kVert320));
  pm.AttachShader(prog, Compile(sm, tgles::kGlFragmentShader, kFrag320));
  pm.LinkProgram(prog);
  const std::uint8_t blob[8] = {7, 1, 3, 2, 0, 0, 0, 9};
  pm.ProgramBinary(prog, tgles::kGlTglProgramBinary, blob, sizeof(blob));
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  std::uint8_t out[8] = {};
  tgles::GLsizei length = 0;
  tgles::GLenum format = 0;
  pm.GetProgramBinary(prog, sizeof(out), &length, &format, out);
  EXPECT_EQ(length, 8);
  EXPECT_EQ(format, tgles::kGlTglProgramBinary);
  EXPECT_EQ(std::memcmp(out, blob, 8), 0);
  pm.ProgramBinary(prog, 0xABCDu, blob, sizeof(blob));  // Unknown format.
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidEnum);
}

// --- Pipelines ---

TEST(Step03, ProgramPipelines) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  tgles::GLuint pipe = 0;
  pm.GenProgramPipelines(1, &pipe);
  EXPECT_NE(pipe, 0u);
  EXPECT_EQ(pm.IsProgramPipeline(pipe), tgles::kGlTrue);
  pm.BindProgramPipeline(pipe);
  tgles::GLuint prog = pm.CreateProgram();
  pm.ProgramParameteri(prog, tgles::kGlProgramSeparable, 1);
  tgles::GLint sep = 0;
  pm.GetProgramiv(prog, tgles::kGlProgramSeparable, &sep);
  EXPECT_EQ(sep, 1);
  pm.AttachShader(prog, Compile(sm, tgles::kGlVertexShader, kVert320));
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));  // Separable: single stage links.
  pm.UseProgramStages(pipe, tgles::kGlVertexShaderBit, prog);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  pm.ActiveShaderProgram(pipe, prog);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  pm.UseProgramStages(pipe, tgles::kGlVertexShaderBit, 9999u);  // Bad program.
  EXPECT_EQ(pm.GetError(), tgles::kGlInvalidOperation);
}
