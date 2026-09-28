#ifndef TGLES_GLSL_TO_MSL_H
#define TGLES_GLSL_TO_MSL_H

// GLSL ES -> MSL translator (pure C++, no Metal headers).
//
// What it does: compiles an app's linked vertex+fragment GLSL ES sources into
// an MSL library source + entry names, so RenderFrame executes the APP's
// shader logic on the GPU instead of the fixed passthrough pair. Ground
// truth: GLSL ES 3.20 spec (docs/reference/GLSL_ES_Specification_3.20.pdf) +
// Metal behavior probed on-device (varyings via struct, texture().sample(),
// constant buffer layout, dual-source index attribute).
//
// SUPPORTED (anything else -> ok=false, never a wrong shader):
// - Attributes: float vec4/vec3 at 0/1, vec2/float at 2, vec2/float at 3
//   (UV2/lightmap); ivec/uvec/int/uint spellings accepted and float-converted
//   (exact below 2^24; bitwise ops on bodies then fail closed). Explicit
//   layout() or sequential locations. Locations 0..3 (facade VertexIn carries
//   pos/col/slot2/slot3).
// - Uniforms: mat4/vec4/vec3/vec2/float/int/uint + uniform arrays
//   `uniform vec4 u[2]` (each element 16B-start; vec3 stride 16, scalars
//   tightly packed after the 16B-aligned base; facade reads per-element via
//   GetUniformElementFloats). Every field starts at a 16-byte boundary
//   (vec3 pads to 16); the facade packs with the offsets implied by
//   TranslatedProgram::uniforms order (vs first, deduped).
//   Sampler2D/Cube/3D/2DArray in vertex and/or fragment (no sampler arrays;
//   vertex texture fetch supported — lightmap/overlay).
// - User structs: `struct Light { vec4 col; ... }; uniform Light u;`
//   flattens to leaf uniforms `u.col` (MSL `u_col`); local struct vars copy
//   through with vec->float rewrites. Nested structs / varying structs fail.
// - Vertex IDs: gl_InstanceID/gl_VertexID -> [[instance_id]]/[[vertex_id]]
//   (int-cast, params emitted only when used).
// - Varyings: float vec4/vec3/vec2/float, vs-out matched by name in fs-in.
// - Fragment outs: single (location 0); MRT locations 0..N (N<=7, contiguous)
//   -> FragOut struct with [[color(i)]]; dual-source pair (location 0,
//   one index 0 + one index 1) -> DualOut with [[color(0), index(1)]].
// - Uniform blocks/UBO (both stages, std140 mat4/vec4/vec3/vec2/float/int/
//   uint/bool members; same block in both stages is deduped) -> constant
//   buffer 2+i on vs_main and fs_main. Same-name plain/member collisions
//   fail. Preprocessor lines (#version/#moj_import/#extension/...) are
//   stripped before parse (MC expands #moj_import before glShaderSource).
// - Bodies copied with: precision/centroid qualifiers stripped (centroid is
//   approximated as smooth — documented), gl_Position -> out.position (+ the
//   GL->Metal z remap epilogue, same as the fixed pair), texture() per-kind
//   sample rewrites, float literals gain f, vec/ivec/uvec/mat constructors
//   respelled, discard -> discard_fragment(), gl_FrontFacing supported,
//   relational lessThan/greaterThan/... -> isless/isgreater/...,
//   not(x) -> !(x), dFdx/dFdy -> dfdx/dfdy (MSL lowercase spelling).
//   Free helpers with texture()/textureLod() rewrite to MSL sample() and
//   gain texture/sampler params (sample_lightmap); call sites expand
//   `fn(Sampler, ...)` to `fn(Sampler_tex, Sampler_smp, ...)`.
//   `return` in fragment main fails closed (helper function returns are OK).
// - Vertex main must assign gl_Position. Empty mains FAIL here on purpose:
//   the facade routes those (trial convention) to the legacy passthrough.
//
// The facade owns the fallback policy (see GlesContext::SubmitVertices);
// this module only answers "translatable + how".

#include <map>
#include <string>
#include <vector>

namespace tgles {
namespace glsl {

struct UniformField {
  std::string name;      // Leaf name; struct leaf is "u.member", array is base.
  std::string msl_type;  // "float4x4", "float4", "float3", "float2",
                         // "float", "int", "uint", "bool".
  int float_count = 0;   // Per-element floats: 16 / 4 / 3 / 2 / 1.
  int offset = 0;        // Byte offset in the uniform block (16B-start rule).
  bool is_sampler = false;
  bool is_int = false;  // int/uint/bool: facade converts float store to int.
  int array_size = 1;    // 1 = scalar, >1 = uniform array (MSL `name[N]`).
  int array_stride_bytes = 0;  // Per-element stride (16 for vec3/arrays).
};

struct UboMember {
  std::string name;
  std::string msl_type;
  int float_count = 0;
  int offset = 0;  // Byte offset in the std140 block (diagnostics: find the
                   // MVP so the frame-extent log prints a real rectangle).
};

struct UboBlock {
  std::string name;        // MSL param name (instance or block name).
  std::string block_name;  // GLSL block name for GetUniformBlockIndex.
  std::vector<UboMember> members;
  int total_bytes = 0;
};

struct TranslatedProgram {
  bool ok = false;
  std::string error;  // Set when !ok (first reason, for logs/tests).
  std::string library_source;  // Complete MSL (metal_stdlib + structs + fns).
  std::string vertex_fn = "vs_main";
  std::string fragment_fn = "fs_main";
  bool uses_sampler = false;        // Any sampler declared (either stage).
  bool uses_vertex_sampler = false;  // Any sampler declared in the vertex stage.
  bool uses_fragment_sampler = false;  // Any sampler declared in the fragment stage.
  bool needs_slot2 = false;  // Attrib location 2 decoded (uv/normal/dir).
  int slot2_comps = 0;  // 0 = absent, else 2/3/4 (stride is 32 + 4*comps).
  bool needs_slot3 = false;  // Attrib location 3 decoded (UV2/lightmap).
  int slot3_comps = 0;  // 0 = absent, else 2/3/4 (adds 4*comps to stride).
  bool is_mrt = false;
  int mrt_count = 0;
  bool is_dual_source = false;
  bool uses_instance_id = false;  // gl_InstanceID -> [[instance_id]].
  bool uses_vertex_id = false;    // gl_VertexID -> [[vertex_id]].
  std::string mvp_name;  // First mat4 uniform in vs (may be empty).
  std::vector<UniformField> uniforms;  // Non-sampler, packing order.
  int uniform_block_bytes = 0;  // Padded total (16 when empty: the _pad).
  std::vector<std::string> sampler_names;  // Declaration order (vs first).
  std::vector<int> sampler_kinds;  // Parallel: 0=2D, 1=cube, 2=3D, 3=2D-array.
  // Parallel to sampler_names: true when the sampler is declared in that
  // stage (drives vs_main/fs_main texture params and bridge bindings).
  std::vector<bool> sampler_in_vs;
  std::vector<bool> sampler_in_fs;
  std::vector<std::string> frag_out_names;  // Single/MRT location order;
                                           // dual: index-0 then index-1.
  std::vector<UboBlock> ubo_blocks;
};

// `attrib_locs` (optional): pre-link glBindAttribLocation / link-parse name→
// location. When present it wins over declaration order (spec 7.3: bound
// locations are authoritative). Locations outside 0..3 still fail closed —
// the facade VertexIn only carries pos/col/slot2/slot3.
TranslatedProgram TranslateProgram(const std::string& vs_src,
                                   const std::string& fs_src,
                                   const std::map<std::string, int>* attrib_locs = nullptr);

// True when the (comment-stripped) sources look legacy-trivial: no sampler,
// texture(, dot(, normalize( tokens. The facade uses this to route empty-main
// trial shaders to the fixed passthrough instead of failing them closed.
bool LooksLegacyTrivial(const std::string& vs_src, const std::string& fs_src);

}  // namespace glsl
}  // namespace tgles

#endif  // TGLES_GLSL_TO_MSL_H
