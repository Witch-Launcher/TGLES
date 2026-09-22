#ifndef TGLES_GL_TYPES_H
#define TGLES_GL_TYPES_H

// Portable OpenGL ES 3.2 scalar types and core constants.
// Usable on macOS hosts and on iOS (no system GLES headers required).
// Values match docs/reference/gl32.h exactly.

#include <cstdint>

namespace tgles {

// Scalar types (Khronos GLES3/gl3platform.h compatible).
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLbyte = std::int8_t;
using GLshort = std::int16_t;
using GLint = int;
using GLsizei = int;
using GLubyte = std::uint8_t;
using GLushort = std::uint16_t;
using GLuint = unsigned int;
using GLfloat = float;
using GLclampf = float;
using GLfixed = std::int32_t;
using GLintptr = std::intptr_t;
using GLsizeiptr = std::intptr_t;
using GLint64 = std::int64_t;
using GLuint64 = std::uint64_t;

inline constexpr GLboolean kGlTrue = 1;
inline constexpr GLboolean kGlFalse = 0;

// Error codes (spec Table 2.3, values match docs/reference/gl32.h).
inline constexpr GLenum kGlNoError = 0;
inline constexpr GLenum kGlInvalidEnum = 0x0500;
inline constexpr GLenum kGlInvalidValue = 0x0501;
inline constexpr GLenum kGlInvalidOperation = 0x0502;
inline constexpr GLenum kGlOutOfMemory = 0x0505;
inline constexpr GLenum kGlInvalidFramebufferOperation = 0x0506;
inline constexpr GLenum kGlContextLost = 0x0507;

// Core server-string query names (spec section 20.2).
inline constexpr GLenum kGlVendor = 0x1F00;
inline constexpr GLenum kGlRenderer = 0x1F01;
inline constexpr GLenum kGlVersion = 0x1F02;
inline constexpr GLenum kGlExtensions = 0x1F03;
inline constexpr GLenum kGlShadingLanguageVersion = 0x8B8C;

// Core numeric queries used by the foundation (spec chapter 20).
inline constexpr GLenum kGlMajorVersion = 0x821B;
inline constexpr GLenum kGlMinorVersion = 0x821C;
inline constexpr GLenum kGlNumExtensions = 0x821D;
inline constexpr GLenum kGlContextFlags = 0x821E;
inline constexpr GLenum kGlContextFlagDebugBit = 0x00000002;
inline constexpr GLenum kGlMaxTextureSize = 0x0D33;
// Core limit queries (values match docs/reference/gl32.h exactly).
// Served from TGL's own limit constants (texture.h / image_units.h /
// buffer.h / framebuffer.h); see Context::GetIntegerv.
inline constexpr GLenum kGlMaxTextureImageUnitsQ = 0x8872;
inline constexpr GLenum kGlMaxVertexTextureImageUnitsQ = 0x8B4C;
inline constexpr GLenum kGlMaxCombinedTextureImageUnitsQ = 0x8B4D;
inline constexpr GLenum kGlMaxImageUnitsQ = 0x8F38;
inline constexpr GLenum kGlTextureBufferOffsetAlignmentQ = 0x919F;
inline constexpr GLenum kGlMaxTextureBufferSizeQ = 0x8C2B;
inline constexpr GLenum kGlMaxUniformBufferBindingsQ = 0x8A2F;
inline constexpr GLenum kGlMaxShaderStorageBufferBindingsQ = 0x90DD;
inline constexpr GLenum kGlMaxDrawBuffersQ = 0x8824;
inline constexpr GLenum kGlMaxColorAttachmentsQ = 0x8CDF;
inline constexpr GLenum kGlMaxSamplesQ = 0x8D57;
inline constexpr GLenum kGlMaxRenderbufferSizeQ = 0x84E8;
inline constexpr GLenum kGlMaxCubeMapTextureSizeQ = 0x851C;
inline constexpr GLenum kGlMax3dTextureSizeQ = 0x8073;
inline constexpr GLenum kGlMaxArrayTextureLayersQ = 0x88FF;
inline constexpr GLenum kGlMaxComputeTextureImageUnitsQ = 0x91BC;

// Core enable/disable capabilities (13 total, see context.h).
inline constexpr GLenum kGlBlend = 0x0BE2;
inline constexpr GLenum kGlCullFace = 0x0B44;
inline constexpr GLenum kGlDebugOutput = 0x92E0;
inline constexpr GLenum kGlDebugOutputSynchronous = 0x8242;
inline constexpr GLenum kGlDepthTest = 0x0B71;
inline constexpr GLenum kGlDither = 0x0BD0;
inline constexpr GLenum kGlPolygonOffsetFill = 0x8037;
inline constexpr GLenum kGlPrimitiveRestartFixedIndex = 0x8D69;
inline constexpr GLenum kGlRasterizerDiscard = 0x8C89;
inline constexpr GLenum kGlSampleAlphaToCoverage = 0x809E;
inline constexpr GLenum kGlSampleCoverage = 0x80A0;
inline constexpr GLenum kGlScissorTest = 0x0C11;
inline constexpr GLenum kGlStencilTest = 0x0B90;

// Implemented ES version.
inline constexpr GLint kEsMajorVersion = 3;
inline constexpr GLint kEsMinorVersion = 2;

}  // namespace tgles

#endif  // TGLES_GL_TYPES_H
