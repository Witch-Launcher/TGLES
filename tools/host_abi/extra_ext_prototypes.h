// Extra Khronos-style prototypes for contract names that docs/reference/*.h
// do not declare. Each line mirrors the vendor's published signature so
// tools/host_abi/gen_gl_abi.py can emit a verbatim ABI prototype:
//
// - glPolygonModeANGLE: ANGLE_translated_shader_source-era entry point kept
//   by MobileGL's loader as OPTIONAL (same (face, mode) signature as
//   glPolygonModeNV / desktop glPolygonMode).
GL_APICALL void GL_APIENTRY glPolygonModeANGLE (GLenum face, GLenum mode);
