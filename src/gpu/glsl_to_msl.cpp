// GLSL ES -> MSL translator v1+v2. See include/tgles/gpu/glsl_to_msl.h for
// the documented subset. Regex-based like ProgramManager::ParseLink (same
// style as the rest of TGL state parsing); every rejection names its reason.
//
// v2 additions over v1: uniform vec3/vec2/float/int/uint (16-byte-start
// packing shared with the facade), samplerCube/sampler3D/sampler2DArray,
// multi-out MRT (locations 0..N) + dual-source (location 0 index 1),
// ivec/uvec/int/uint attributes (float-converted, bitwise ops denied),
// uniform blocks/UBO (std140 mat4/vec4/float members -> buffer 2),
// relational rewrites (lessThan->isless etc.), layout(binding=) tolerance.

#include "tgles/gpu/glsl_to_msl.h"

#include <regex>
#include <sstream>

namespace tgles {
namespace glsl {

namespace {

std::string StripComments(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool line = false, block = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (line) {
      if (s[i] == '\n') {
        line = false;
        out += '\n';
      }
      continue;
    }
    if (block) {
      if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') {
        block = false;
        ++i;
      } else if (s[i] == '\n') {
        out += '\n';
      }
      continue;
    }
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      line = true;
      ++i;
      continue;
    }
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      block = true;
      ++i;
      continue;
    }
    out += s[i];
  }
  return out;
}

std::string StripLineDirectives(const std::string& s) {
  std::istringstream in(s);
  std::string line, out;
  while (std::getline(in, line)) {
    std::string t = line;
    t.erase(0, t.find_first_not_of(" \t\r"));
    if (t.rfind("#version", 0) == 0) continue;
    static const std::regex kPrecision(R"(^\s*precision\s+\w+\s+\w+\s*;)");
    if (std::regex_search(line, kPrecision)) continue;
    out += line + "\n";
  }
  return out;
}

// Removes standalone precision/invariant/precise qualifiers (word-boundary,
// so `lowpass` keeps its `lowp`). Centroid/sample interpolation qualifiers
// are rejected at declaration parse.
std::string StripQualifiers(const std::string& s) {
  static const std::regex kQ(
      R"(\b(highp|mediump|lowp|invariant|precise|centroid|sample|patch)\b)");
  return std::regex_replace(s, kQ, "");
}

struct Decl {
  std::string type;
  std::string name;
  int location = -1;  // -1 = no explicit layout location.
  int index = -1;     // Dual-source index; -1 = absent.
  bool array = false;
  int array_size = 0;
};

std::vector<Decl> FindDecls(const std::string& src, const std::string& kind) {
  // The layout(...) (when present) is part of the match itself, so location
  // and index always belong to THIS declaration (never a neighbor's).
  std::vector<Decl> out;
  const std::string pat =
      "(?:layout\\s*\\(([^)]*)\\)\\s*)?" + kind +
      "\\s+(\\w+)\\s+(\\w+)(\\s*\\[\\s*(\\d*)\\s*\\])?\\s*;";
  std::regex re(pat);
  std::regex loc_re(R"(location\s*=\s*(\d+))");
  std::regex idx_re(R"(index\s*=\s*(\d+))");
  for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end;
       ++it) {
    Decl d;
    const std::string inside = (*it)[1].matched ? (*it)[1].str() : "";
    d.type = (*it)[2].str();
    d.name = (*it)[3].str();
    if ((*it)[4].matched) {
      d.array = true;
      d.array_size = (*it)[5].matched && !(*it)[5].str().empty()
                         ? std::stoi((*it)[5].str())
                         : 0;
    }
    if (d.name.rfind("gl_", 0) == 0) continue;
    std::smatch vm;
    if (std::regex_search(inside, vm, loc_re))
      d.location = std::stoi(vm[1].str());
    if (std::regex_search(inside, vm, idx_re))
      d.index = std::stoi(vm[1].str());
    out.push_back(d);
  }
  return out;
}

// Extracts the body of `void main()` (without braces). Empty when absent.
std::string MainBody(const std::string& src) {
  static const std::regex kMain(R"(void\s+main\s*\(\s*\))");
  std::smatch m;
  if (!std::regex_search(src, m, kMain)) return {};
  std::size_t open = src.find('{', static_cast<std::size_t>(m.position()) +
                                       static_cast<std::size_t>(m.length()));
  if (open == std::string::npos) return {};
  int depth = 0;
  for (std::size_t i = open; i < src.size(); ++i) {
    if (src[i] == '{') ++depth;
    if (src[i] == '}') {
      if (--depth == 0) return src.substr(open + 1, i - open - 1);
    }
  }
  return {};
}

std::string ReplaceWord(std::string s, const std::string& from,
                        const std::string& to) {
  std::regex re("\\b" + from + "\\b");
  return std::regex_replace(s, re, to);
}

bool HasWord(const std::string& s, const std::string& word) {
  return std::regex_search(s, std::regex("\\b" + word + "\\b"));
}

// MSL spelling + (size, alignment) for the uniform packing shared with the
// facade (every field starts at a 16-byte boundary; mat4 fills 64 bytes).
struct UniformLayout {
  std::string msl;
  int floats = 0;
  int bytes = 0;
};
bool UniformLayoutOf(const std::string& glsl, UniformLayout* out) {
  if (glsl == "mat4") {
    *out = {"float4x4", 16, 64};
    return true;
  }
  if (glsl == "vec4") {
    *out = {"float4", 4, 16};
    return true;
  }
  if (glsl == "vec3") {
    *out = {"float3", 3, 12};
    return true;
  }
  if (glsl == "vec2") {
    *out = {"float2", 2, 8};
    return true;
  }
  if (glsl == "float" || glsl == "int" || glsl == "uint") {
    *out = {glsl, 1, 4};
    return true;
  }
  if (glsl == "bool") {
    *out = {"bool", 1, 4};
    return true;
  }
  return false;
}

std::string MslVaryingType(const std::string& glsl, bool* ok) {
  if (glsl == "float") return "float";
  if (glsl == "vec2") return "float2";
  if (glsl == "vec3") return "float3";
  if (glsl == "vec4") return "float4";
  *ok = false;
  return {};
}

// MSL constructors for GLSL type words in bodies (declarations are parsed
// out, so remaining words are constructors/casts). Probed failure without
// this: "use of undeclared identifier 'vec3'".
std::string RewriteConstructors(std::string s) {
  static const std::pair<const char*, const char*> kMap[] = {
      {"vec2", "float2"},   {"vec3", "float3"},     {"vec4", "float4"},
      {"ivec2", "int2"},    {"ivec3", "int3"},      {"ivec4", "int4"},
      {"uvec2", "uint2"},   {"uvec3", "uint3"},     {"uvec4", "uint4"},
      {"bvec2", "bool2"},   {"bvec3", "bool3"},     {"bvec4", "bool4"},
      {"mat2", "float2x2"}, {"mat3", "float3x3"},   {"mat4", "float4x4"},
  };
  for (const auto& kv : kMap) s = ReplaceWord(s, kv.first, kv.second);
  return s;
}

// GLSL decimals are float; MSL unsuffixed decimals are DOUBLE (breaks
// overloads like max(float, 0.0)). Suffix dotted literals with f.
std::string SuffixFloatLiterals(std::string s) {
  // NOTE: ECMAScript std::regex has no lookbehind: leading-dot literals use
  // a captured separator instead.
  static const std::regex kDec(R"(\b\d+\.\d+\b(?!f))");
  s = std::regex_replace(s, kDec, "$&f");
  static const std::regex kLeadDot(R"((^|[^\w.])(\.\d+\b)(?!f))");
  s = std::regex_replace(s, kLeadDot, "$1$2f");
  return s;
}

// texture(sampler, args...) with balanced parens -> sampler sample call.
// kind: 0=2D (uv), 1=cube (dir), 2=3D (coord), 3=2D-array (vec3: xy + layer).
bool RewriteTextureCalls(std::string& body, const std::string& sampler,
                         int kind, std::string* error) {
  std::string out;
  std::size_t pos = 0;
  bool any = false;
  const std::regex kCall("\\btexture\\s*\\(\\s*" + sampler + "\\s*,");
  while (true) {
    std::smatch m;
    std::string rest = body.substr(pos);
    if (!std::regex_search(rest, m, kCall)) {
      out += rest;
      break;
    }
    any = true;
    out += rest.substr(0, static_cast<std::size_t>(m.position()));
    std::size_t arg_start =
        pos + static_cast<std::size_t>(m.position()) +
        static_cast<std::size_t>(m.length());
    // Scan to the matching close paren of texture(.
    int depth = 1;
    std::size_t i = arg_start;
    for (; i < body.size() && depth > 0; ++i) {
      if (body[i] == '(') ++depth;
      if (body[i] == ')') --depth;
    }
    if (depth != 0) {
      *error = "unbalanced texture() call for sampler " + sampler;
      return false;
    }
    std::string args = body.substr(arg_start, i - arg_start - 1);
    // Strip an optional bias argument (v1: bias unsupported, must be absent).
    // A second top-level comma means bias/lod form -> fail closed.
    int d2 = 0;
    bool has_bias = false;
    for (char c : args) {
      if (c == '(') ++d2;
      if (c == ')') --d2;
      if (c == ',' && d2 == 0) {
        has_bias = true;
        break;
      }
    }
    if (has_bias) {
      *error = "texture() with bias/lod not supported in v1: " + sampler;
      return false;
    }
    if (kind == 3) {
      out += sampler + "_tex.sample(" + sampler + "_smp, ((" + args +
             ").xy), uint((" + args + ").z))";
    } else {
      out += sampler + "_tex.sample(" + sampler + "_smp, (" + args + "))";
    }
    pos = i;
  }
  body = out;
  if (!any) {
    *error = "declared sampler never sampled with texture(): " + sampler;
    return false;
  }
  return true;
}

// Denylisted constructs (v1+v2 scope). Checked on comment-stripped sources.
// gl_InstanceID/gl_VertexID are SUPPORTED (11a) via [[instance_id]]/
// [[vertex_id]] and are deliberately absent here.
const char* const kDeny[] = {
    "sampler2DMS",
    "samplerBuffer",
    "image2D",
    "image3D",
    "imageCube",
    "uimage",
    "iimage",
    "atomicUint",
    "atomicAdd",
    "atomicMin",
    "atomicMax",
    "atomicAnd",
    "atomicOr",
    "atomicXor",
    "atomicExchange",
    "atomicCompSwap",
    "barrier(",
    "memoryBarrier",
    "groupMemoryBarrier",
    "texelFetch",
    "textureProj",
    "textureLod",
    "textureGrad",
    "textureOffset",
    "textureGather",
    "gl_PointCoord",
    "gl_FragDepth",
    "gl_PrimitiveID",
    "subroutine",
    "dFdxCoarse",
    "dFdyCoarse",
    "fwidthCoarse",
    "interpolateAtCentroid",
    "interpolateAtSample",
    "interpolateAtOffset",
};

struct ParsedUbo {
  std::string name;  // MSL param name (instance or block name).
  struct Member {
    std::string name;
    std::string msl;
    int floats = 0;
    int bytes = 0;
  };
  std::vector<Member> members;
  int total_bytes = 0;
};

}  // namespace

bool LooksLegacyTrivial(const std::string& vs_src, const std::string& fs_src) {
  const std::string vs = StripComments(vs_src);
  const std::string fs = StripComments(fs_src);
  for (const char* tok : {"sampler", "texture(", "dot(", "normalize("}) {
    if (vs.find(tok) != std::string::npos) return false;
    if (fs.find(tok) != std::string::npos) return false;
  }
  return true;
}

TranslatedProgram TranslateProgram(const std::string& vs_src,
                                   const std::string& fs_src) {
  TranslatedProgram out;
  auto fail = [&](const std::string& why) {
    out.ok = false;
    out.error = why;
    return out;
  };

  std::string vs = StripQualifiers(StripLineDirectives(StripComments(vs_src)));
  std::string fs = StripQualifiers(StripLineDirectives(StripComments(fs_src)));

  for (const char* tok : kDeny) {
    if (vs.find(tok) != std::string::npos)
      return fail(std::string("vertex uses unsupported construct: ") + tok);
    if (fs.find(tok) != std::string::npos)
      return fail(std::string("fragment uses unsupported construct: ") + tok);
  }
  if (HasWord(fs, "return"))
    return fail("fragment main with early return is not supported");
  if (HasWord(vs, "discard"))
    return fail("discard in vertex shader is not supported");
  if (vs.find("uniform") != std::string::npos &&
      std::regex_search(vs, std::regex(R"(\buniform\s+\w+\s*\{)")))
    return fail("vertex uniform blocks not supported (fragment UBO only)");
  // UBO blocks are fragment-only in v1 (vertex UBO would need buffer(3)).

  // ---- Uniform blocks / UBO (fragment, std140 mat4/vec4/float/int/uint/ --
  // ---- bool members; 16B-start total shared with the facade). -------------
  std::vector<ParsedUbo> ubos;
  {
    static const std::regex kBlock(
        R"(uniform\s+(\w+)\s*\{([^}]*)\}\s*(\w+)?\s*;)");
    for (std::sregex_iterator it(fs.begin(), fs.end(), kBlock), end; it != end;
         ++it) {
      const std::string block_name = (*it)[1].str();
      const std::string members_src = (*it)[2].str();
      const std::string instance =
          (*it)[3].matched ? (*it)[3].str() : block_name;
      ParsedUbo b;
      b.name = instance;
      static const std::regex kMember(R"((\w+)\s+(\w+)\s*;)");
      int offset = 0;
      for (std::sregex_iterator mi(members_src.begin(), members_src.end(),
                                   kMember),
           mend;
           mi != mend; ++mi) {
        const std::string type = (*mi)[1].str();
        const std::string name = (*mi)[2].str();
        UniformLayout lay;
        if (!UniformLayoutOf(type, &lay) || (lay.msl != "float4x4" &&
                                             lay.msl != "float4" &&
                                             lay.msl != "float" &&
                                             lay.msl != "int" &&
                                             lay.msl != "uint" &&
                                             lay.msl != "bool"))
          return fail("UBO member type not supported (mat4/vec4/float/int/"
                      "uint/bool): " +
                      type + " " + name);
        // 16-byte-start rule shared with plain uniforms (over-allocates for
        // consecutive scalars but keeps std140 offsets for the tested
        // mat4/vec4/int/bool shapes; Metal pads bool(1B) so later offsets
        // still match on little-endian).
        offset = (offset + 15) / 16 * 16;
        b.members.push_back({name, lay.msl, lay.floats, lay.bytes});
        offset += lay.bytes;
      }
      if (b.members.empty())
        return fail("empty uniform block: " + block_name);
      b.total_bytes = (offset + 15) / 16 * 16;
      ubos.push_back(b);
    }
  }

  // ---- Attributes (vertex `in`, slot convention 0/1[/2]). ----
  struct Attr {
    std::string name;
    std::string msl;
    int loc = -1;
    bool is_int = false;  // ivec/uvec/int/uint: float-converted (see docs).
  };
  std::vector<Attr> attrs;
  bool int_attribs = false;
  {
    auto decls = FindDecls(vs, "in");
    int next_loc = 0;
    for (const Decl& d : decls) {
      if (d.array) return fail("attribute arrays not supported: " + d.name);
      std::string msl;
      bool is_int = false;
      if (d.type == "vec4" || d.type == "vec3") {
        msl = (d.type == "vec4") ? "float4" : "float3";
      } else if (d.type == "vec2" || d.type == "float") {
        msl = (d.type == "vec2") ? "float2" : "float";
      } else if (d.type == "ivec4" || d.type == "uvec4") {
        msl = "float4";
        is_int = true;
      } else if (d.type == "ivec3" || d.type == "uvec3") {
        msl = "float3";
        is_int = true;
      } else if (d.type == "ivec2" || d.type == "uvec2" || d.type == "int" ||
                 d.type == "uint") {
        msl = (d.type == "ivec2" || d.type == "uvec2") ? "float2" : "float";
        is_int = true;
      } else {
        return fail("attribute type not supported: " + d.type + " " + d.name);
      }
      const int loc = (d.location >= 0) ? d.location : next_loc;
      if (loc < 0 || loc > 2)
        return fail("attribute location must be 0..2: " + d.name);
      // Location 2 carries uv/normal/dir (any float width; the facade
      // interleaves slot2_comps*4 bytes and the descriptor matches).
      if (loc != 2 && msl != "float4" && msl != "float3")
        return fail("locations 0/1 must be vec4/vec3: " + d.name);
      for (const Attr& a : attrs) {
        if (a.loc == loc)
          return fail("duplicate attribute location: " + d.name);
      }
      attrs.push_back({d.name, msl, loc, is_int});
      int_attribs = int_attribs || is_int;
      if (d.location < 0) ++next_loc;
    }
  }
  bool has0 = false, has1 = false, has2 = false;
  for (const Attr& a : attrs) {
    if (a.loc == 0) has0 = true;
    if (a.loc == 1) has1 = true;
    if (a.loc == 2) has2 = true;
  }
  if (!has0 || !has1)
    return fail("vertex needs attribs at locations 0 and 1");

  // Integer attribs convert to float on the CPU (exact below 2^24); bitwise
  // ops on them would silently change meaning, so they fail closed here.
  auto deny_int_ops = [&](const std::string& body, const char* stage) -> bool {
    if (!int_attribs) return true;
    std::string t = body;
    // && and || are fine (boolean logic); strip them before hunting &, |.
    t = std::regex_replace(t, std::regex(R"(&&)"), "  ");
    t = std::regex_replace(t, std::regex(R"(\|\|)"), "  ");
    static const std::regex kBits(R"(<<|>>|[~%&\|])");
    if (std::regex_search(t, kBits)) {
      out.error = std::string(stage) +
                  " uses bitwise ops with integer attribs (float-converted)";
      return false;
    }
    return true;
  };

  // ---- User structs (11b): `struct Light { vec4 col; ... };`. Parsed from --
  // ---- both stages (deduped); members must be uniform-compatible types. ----
  struct StructDef {
    std::string name;
    struct Member {
      std::string type;
      std::string name;
    };
    std::vector<Member> members;
  };
  std::vector<StructDef> structs;
  {
    static const std::regex kStruct(R"(struct\s+(\w+)\s*\{([^}]*)\}\s*;)");
    const std::string both = vs + "\n" + fs;
    for (std::sregex_iterator it(both.begin(), both.end(), kStruct), end;
         it != end; ++it) {
      StructDef sd;
      sd.name = (*it)[1].str();
      const std::string body = (*it)[2].str();
      static const std::regex kSmem(R"((\w+)\s+(\w+)\s*;)");
      for (std::sregex_iterator mi(body.begin(), body.end(), kSmem), mend;
           mi != mend; ++mi) {
        sd.members.push_back({(*mi)[1].str(), (*mi)[2].str()});
      }
      if (sd.members.empty()) return fail("empty struct: " + sd.name);
      bool known = false;
      for (const auto& s : structs)
        if (s.name == sd.name) known = true;
      if (!known) structs.push_back(sd);
    }
    // Validate members are uniform-compatible (no nesting/sampler for now).
    for (const auto& s : structs) {
      for (const auto& m : s.members) {
        UniformLayout lay;
        if (!UniformLayoutOf(m.type, &lay)) {
          // Nested struct or sampler in struct: fail closed (medium scope).
          bool nested = false;
          for (const auto& o : structs)
            if (o.name == m.type) nested = true;
          if (nested)
            return fail("nested structs not supported: " + s.name + "." +
                        m.name);
          return fail("struct member type not supported: " + s.name + "." +
                      m.type + " " + m.name);
        }
      }
    }
  }
  auto find_struct = [&](const std::string& t) -> const StructDef* {
    for (const auto& s : structs)
      if (s.name == t) return &s;
    return nullptr;
  };

  // ---- Uniforms (both stages, vs first, deduped). 16-byte-start packing. --
  // ---- Arrays (7) + struct flattening (11b) live here. --------------------
  std::vector<UniformField> uniforms;
  struct SamplerInfo {
    std::string name;
    int kind = 0;  // 0=2D, 1=cube, 2=3D, 3=2D-array.
  };
  std::vector<SamplerInfo> samplers;
  int uniform_offset = 0;
  auto add_uniforms = [&](const std::string& src, const char* stage,
                          bool* failed) {
    for (const Decl& d : FindDecls(src, "uniform")) {
      const bool is_sampler =
          (d.type == "sampler2D" || d.type == "samplerCube" ||
           d.type == "sampler3D" || d.type == "sampler2DArray");
      if (is_sampler) {
        if (d.array) {
          out.error =
              std::string("sampler arrays not supported: ") + d.name;
          *failed = true;
          return;
        }
        if (std::string(stage) != "fragment") {
          out.error = "vertex texture fetch not supported: " + d.name;
          *failed = true;
          return;
        }
        bool known = false;
        for (const auto& s : samplers)
          if (s.name == d.name) known = true;
        if (!known) {
          const int kind = (d.type == "sampler2D")
                               ? 0
                               : (d.type == "samplerCube")
                                     ? 1
                                     : (d.type == "sampler3D") ? 2 : 3;
          samplers.push_back({d.name, kind});
        }
        continue;
      }
      // Struct uniform (11b): flatten leaves to `u.member` (MSL `u_member`).
      if (const StructDef* sd = find_struct(d.type)) {
        if (d.array) {
          out.error = std::string("struct arrays not supported: ") + d.name;
          *failed = true;
          return;
        }
        for (const auto& m : sd->members) {
          UniformLayout lay;
          if (!UniformLayoutOf(m.type, &lay)) {
            out.error = std::string("struct member type not supported: ") +
                        d.name + "." + m.name;
            *failed = true;
            return;
          }
          const std::string leaf = d.name + "." + m.name;
          bool known = false;
          for (const auto& u : uniforms) {
            if (u.name == leaf) {
              known = true;
              if (u.msl_type != lay.msl) {
                out.error = "uniform type mismatch across stages: " + leaf;
                *failed = true;
                return;
              }
            }
          }
          if (known) continue;
          uniform_offset = (uniform_offset + 15) / 16 * 16;
          UniformField f;
          f.name = leaf;
          f.msl_type = lay.msl;
          f.float_count = lay.floats;
          f.is_sampler = false;
          f.is_int = (m.type == "int" || m.type == "uint" ||
                      m.type == "bool");
          f.array_size = 1;
          f.array_stride_bytes = lay.bytes;
          f.offset = uniform_offset;
          uniform_offset += (m.type == "vec3") ? 16 : lay.bytes;
          uniforms.push_back(f);
        }
        continue;
      }
      // Plain uniform (possibly array).
      UniformLayout lay;
      if (!UniformLayoutOf(d.type, &lay)) {
        out.error =
            std::string("uniform type not supported: ") + d.type + " " +
            d.name;
        *failed = true;
        return;
      }
      const int arr = d.array ? d.array_size : 1;
      if (d.array && arr <= 0) {
        out.error = std::string("unsized uniform array: ") + d.name;
        *failed = true;
        return;
      }
      bool known = false;
      for (const auto& u : uniforms) {
        if (u.name == d.name) {
          known = true;
          if (u.msl_type != lay.msl || u.array_size != arr) {
            out.error = "uniform type mismatch across stages: " + d.name;
            *failed = true;
            return;
          }
        }
      }
      if (known) continue;
      uniform_offset = (uniform_offset + 15) / 16 * 16;
      UniformField f;
      f.name = d.name;
      f.msl_type = lay.msl;
      f.float_count = lay.floats;
      f.is_sampler = false;
      f.is_int =
          (d.type == "int" || d.type == "uint" || d.type == "bool");
      f.array_size = arr;
      // Stride: vec3 pads to 16, mat4 64, others tightly packed after base.
      f.array_stride_bytes =
          (d.type == "vec3") ? 16 : lay.bytes;
      if (d.type == "mat4") f.array_stride_bytes = 64;
      f.offset = uniform_offset;
      if (arr == 1) {
        uniform_offset += (d.type == "vec3") ? 16 : lay.bytes;
      } else {
        uniform_offset += arr * f.array_stride_bytes;
      }
      uniforms.push_back(f);
    }
  };
  {
    bool failed = false;
    add_uniforms(vs, "vertex", &failed);
    if (failed) return fail(out.error);
    add_uniforms(fs, "fragment", &failed);
    if (failed) return fail(out.error);
  }
  const int uniform_block_bytes = (uniform_offset + 15) / 16 * 16;
  out.uses_sampler = !samplers.empty();
  for (const auto& s : samplers) out.sampler_names.push_back(s.name);
  out.sampler_kinds.clear();
  for (const auto& s : samplers) out.sampler_kinds.push_back(s.kind);
  out.needs_slot2 = has2;
  out.slot2_comps = 0;
  for (const Attr& a : attrs) {
    if (a.loc == 2)
      out.slot2_comps = (a.msl == "float4") ? 4 : (a.msl == "float3" ? 3 : 2);
  }
  out.uniforms = uniforms;
  out.uniform_block_bytes = uniform_block_bytes;
  out.ubo_blocks.clear();
  for (const auto& b : ubos) {
    glsl::UboBlock o;
    o.name = b.name;
    o.total_bytes = b.total_bytes;
    for (const auto& m : b.members) {
      glsl::UboMember om;
      om.name = m.name;
      om.msl_type = m.msl;
      om.float_count = m.floats;
      o.members.push_back(om);
    }
    out.ubo_blocks.push_back(o);
  }
  for (const auto& u : uniforms) {
    if (u.msl_type == "float4x4") {
      out.mvp_name = u.name;  // First mat4 in vs-first order.
      break;
    }
  }
  // Plain uniform names must not collide with UBO member access (B.x would
  // corrupt under prefixing); fail loudly instead of miscompiling.
  for (const auto& u : uniforms) {
    for (const auto& b : ubos) {
      for (const auto& m : b.members) {
        if (m.name == u.name)
          return fail("uniform name collides with UBO member: " + u.name);
      }
    }
  }

  // ---- Varyings (vs `out` -> fs `in`, matched by name). ----
  struct Vary {
    std::string name;
    std::string msl;
  };
  std::vector<Vary> varyings;
  {
    auto vouts = FindDecls(vs, "out");
    auto fins = FindDecls(fs, "in");
    for (const Decl& d : vouts) {
      if (d.array) return fail("varying arrays not supported: " + d.name);
      bool ok = true;
      const std::string msl = MslVaryingType(d.type, &ok);
      if (!ok) return fail("varying type not supported: " + d.type);
      bool found = false;
      for (const Decl& f : fins) {
        if (f.name == d.name) {
          found = true;
          if (f.type != d.type)
            return fail("varying type mismatch: " + d.name);
        }
      }
      // Unused varyings are legal GLSL (linker may cull them); the MSL
      // struct still carries them, the fragment just never reads them.
      (void)found;
      varyings.push_back({d.name, msl});
    }
    for (const Decl& f : fins) {
      bool found = false;
      for (const auto& v : varyings)
        if (v.name == f.name) found = true;
      if (!found) return fail("fs in missing from vs outs: " + f.name);
    }
  }

  // ---- Fragment outs: single / MRT (locations 0..N) / dual-source. ----
  struct FragOut {
    std::string name;
    int location = 0;
    int index = 0;
  };
  std::vector<FragOut> frag_outs;
  bool mrt = false, dual = false;
  int mrt_count = 0;
  {
    auto outs = FindDecls(fs, "out");
    if (outs.empty()) return fail("fragment declares no out");
    for (const Decl& d : outs) {
      if (d.array) return fail("fragment out arrays not supported");
      if (d.type != "vec4")
        return fail("fragment outs must be vec4: " + d.name);
      const int loc = (d.location >= 0) ? d.location : 0;
      const int idx = (d.index >= 0) ? d.index : 0;
      frag_outs.push_back({d.name, loc, idx});
    }
    if (frag_outs.size() == 1) {
      if (frag_outs[0].location != 0 || frag_outs[0].index != 0)
        return fail("single fragment out must be location 0 index 0");
    } else if (frag_outs.size() == 2 && frag_outs[0].location == 0 &&
               frag_outs[1].location == 0 &&
               ((frag_outs[0].index == 0 && frag_outs[1].index == 1) ||
                (frag_outs[0].index == 1 && frag_outs[1].index == 0))) {
      dual = true;  // Dual-source blending pair.
    } else {
      // MRT: locations exactly 0..N-1, all index 0.
      for (const auto& o : frag_outs) {
        if (o.index != 0)
          return fail("MRT outs must all be index 0 (dual-source takes "
                      "exactly one index-1 out at location 0)");
      }
      int top = -1;
      for (const auto& o : frag_outs) top = std::max(top, o.location);
      if (top > 7)
        return fail("MRT supports at most 8 draw buffers");
      std::vector<bool> seen(static_cast<std::size_t>(top) + 1, false);
      for (const auto& o : frag_outs) {
        if (seen[static_cast<std::size_t>(o.location)])
          return fail("duplicate fragment out location");
        seen[static_cast<std::size_t>(o.location)] = true;
      }
      for (bool s : seen) {
        if (!s) return fail("MRT locations must be contiguous from 0");
      }
      mrt = true;
      mrt_count = top + 1;
    }
  }
  out.is_mrt = mrt;
  out.mrt_count = mrt_count;
  out.is_dual_source = dual;
  out.frag_out_names.clear();
  if (dual) {
    // Canonical order: index-0 first.
    for (const auto& o : frag_outs) {
      if (o.index == 0) out.frag_out_names.push_back(o.name);
    }
    for (const auto& o : frag_outs) {
      if (o.index == 1) out.frag_out_names.push_back(o.name);
    }
  } else {
    std::vector<FragOut> sorted = frag_outs;
    std::sort(sorted.begin(), sorted.end(),
              [](const FragOut& a, const FragOut& b) {
                return a.location < b.location;
              });
    for (const auto& o : sorted) out.frag_out_names.push_back(o.name);
  }

  // ---- Bodies. ----
  std::string vbody = MainBody(vs);
  std::string fbody = MainBody(fs);
  if (vbody.empty()) return fail("vertex main body not found");
  if (fbody.empty()) return fail("fragment main body not found");
  if (!HasWord(vbody, "gl_Position"))
    return fail("vertex main must assign gl_Position");
  if (!deny_int_ops(vbody, "vertex") || !deny_int_ops(fbody, "fragment"))
    return fail(out.error);

  const bool use_front_facing = HasWord(fbody, "gl_FrontFacing");
  // 11a: instance/vertex IDs are vertex-only; fragment use fails closed.
  if (HasWord(fbody, "gl_InstanceID") || HasWord(fbody, "gl_VertexID"))
    return fail("gl_InstanceID/gl_VertexID in fragment not supported");
  const bool use_iid = HasWord(vbody, "gl_InstanceID");
  const bool use_vid = HasWord(vbody, "gl_VertexID");
  out.uses_instance_id = use_iid;
  out.uses_vertex_id = use_vid;

  auto mangle = [](const std::string& n) {
    std::string o = n;
    for (char& c : o)
      if (c == '.') c = '_';
    return o;
  };

  // ---- Emit MSL. ----
  std::ostringstream msl;
  msl << "#include <metal_stdlib>\nusing namespace metal;\n";
  // 11b: user struct definitions (MSL spellings).
  for (const auto& s : structs) {
    msl << "struct " << s.name << " {\n";
    for (const auto& m : s.members) {
      UniformLayout lay;
      if (!UniformLayoutOf(m.type, &lay))
        return fail("struct member type not supported in emit: " + m.name);
      msl << "  " << lay.msl << " " << m.name << ";\n";
    }
    msl << "};\n";
  }
  msl << "struct VertexIn {\n";
  for (const Attr& a : attrs)
    msl << "  " << a.msl << " " << a.name << " [[attribute(" << a.loc
        << ")]];\n";
  msl << "};\nstruct Uniforms {\n";
  for (const auto& u : uniforms) {
    msl << "  " << u.msl_type << " " << mangle(u.name);
    if (u.array_size > 1) msl << "[" << u.array_size << "]";
    msl << ";\n";
  }
  if (uniforms.empty()) msl << "  float4 _pad;\n";
  msl << "};\n";
  for (const auto& b : ubos) {
    msl << "struct " << b.name << "_t {\n";
    for (const auto& m : b.members)
      msl << "  " << m.msl << " " << m.name << ";\n";
    msl << "};\n";
  }
  msl << "struct Varyings {\n  float4 position [[position]];\n";
  int locn = 0;
  for (const auto& v : varyings)
    msl << "  " << v.msl << " " << v.name << " [[user(locn" << locn++
        << ")]];\n";
  msl << "};\n";
  if (mrt) {
    msl << "struct FragOut {\n";
    for (std::size_t i = 0; i < out.frag_out_names.size(); ++i)
      msl << "  float4 " << out.frag_out_names[i] << " [[color(" << i
          << ")]];\n";
    msl << "};\n";
  } else if (dual) {
    msl << "struct DualOut {\n  float4 " << out.frag_out_names[0]
        << " [[color(0)]];\n  float4 " << out.frag_out_names[1]
        << " [[color(0), index(1)]];\n};\n";
  }

  // Vertex: prefix attribs with in., outs with out., uniforms with uni.,
  // gl_Position with out.position. Struct leaves (`u.member`) use escaped
  // dots -> `uni.u_member`; arrays (`u[0]`) fall out of the base word rewrite.
  std::string vb = vbody;
  for (const Attr& a : attrs) vb = ReplaceWord(vb, a.name, "in." + a.name);
  for (const auto& v : varyings) vb = ReplaceWord(vb, v.name, "out." + v.name);
  for (const auto& u : uniforms) {
    if (u.name.find('.') != std::string::npos) {
      std::string esc;
      for (char c : u.name) {
        if (c == '.')
          esc += "\\.";
        else
          esc += c;
      }
      std::regex re("\\b" + esc + "\\b");
      vb = std::regex_replace(vb, re, "uni." + mangle(u.name));
    } else {
      vb = ReplaceWord(vb, u.name, "uni." + mangle(u.name));
    }
  }
  // 11a: IDs become Metal stage params (int-cast: GLSL int vs MSL uint).
  if (use_iid) vb = ReplaceWord(vb, "gl_InstanceID", "(int)iid");
  if (use_vid) vb = ReplaceWord(vb, "gl_VertexID", "(int)vid");
  vb = ReplaceWord(vb, "gl_Position", "out.position");
  // Relational built-ins missing from MSL (these renames are the documented
  // GLSL->MSL mapping and the device tests prove them; `not` is safe here
  // because \bnot\b never matches inside notEqual).
  vb = ReplaceWord(vb, "not", "!");
  vb = ReplaceWord(vb, "lessThanEqual", "islessequal");
  vb = ReplaceWord(vb, "greaterThanEqual", "isgreaterequal");
  vb = ReplaceWord(vb, "lessThan", "isless");
  vb = ReplaceWord(vb, "greaterThan", "isgreater");
  vb = ReplaceWord(vb, "notEqual", "isnotequal");
  // `equal(` before `isnotequal` damage: rewrite equal( only when not part
  // of isnotequal/islessequal/isgreaterequal (all already rewritten above,
  // so a bare equal( here is genuinely GLSL equal().
  vb = ReplaceWord(vb, "equal", "isequal");
  msl << "vertex Varyings vs_main(VertexIn in [[stage_in]], constant "
         "Uniforms& uni [[buffer(0)]]";
  if (use_iid) msl << ", uint iid [[instance_id]]";
  if (use_vid) msl << ", uint vid [[vertex_id]]";
  for (std::size_t i = 0; i < ubos.size(); ++i)
    msl << ", constant " << ubos[i].name << "_t& " << ubos[i].name
        << " [[buffer(" << (2 + i) << ")]]";
  msl << ") {\n  Varyings out;\n"
      << SuffixFloatLiterals(RewriteConstructors(vb))
      << "\n  out.position.z = out.position.z * 0.5f + out.position.w * 0.5f;\n"
         "  return out;\n}\n";

  // Fragment: varyings read via in., uniforms via uni., UBO members via
  // Block. (bare member access is correct GLSL for instance-less blocks),
  // texture() -> sample, discard -> discard_fragment(), gl_FrontFacing param
  // when used.
  std::string fb = fbody;
  for (const auto& v : varyings) fb = ReplaceWord(fb, v.name, "in." + v.name);
  for (const auto& u : uniforms) {
    if (u.name.find('.') != std::string::npos) {
      std::string esc;
      for (char c : u.name) {
        if (c == '.')
          esc += "\\.";
        else
          esc += c;
      }
      std::regex re("\\b" + esc + "\\b");
      fb = std::regex_replace(fb, re, "uni." + mangle(u.name));
    } else {
      fb = ReplaceWord(fb, u.name, "uni." + mangle(u.name));
    }
  }
  for (const auto& b : ubos) {
    for (const auto& m : b.members) {
      // Bare member access (correct GLSL for instance-less blocks) gains the
      // param prefix; already-qualified Block.member is left alone (the
      // separator capture avoids corrupting it into Block.Block.member —
      // ECMAScript has no lookbehind, hence the (^|...) prefix).
      std::regex re("(^|[^.\\w])" + m.name + "\\b");
      fb = std::regex_replace(fb, re, "$1" + b.name + "." + m.name);
    }
  }
  for (const auto& s : samplers) {
    std::string err;
    if (!RewriteTextureCalls(fb, s.name, s.kind, &err)) return fail(err);
  }
  fb = ReplaceWord(fb, "discard", "discard_fragment()");
  if (use_front_facing) fb = ReplaceWord(fb, "gl_FrontFacing", "in_ff");
  fb = ReplaceWord(fb, "not", "!");
  fb = ReplaceWord(fb, "lessThanEqual", "islessequal");
  fb = ReplaceWord(fb, "greaterThanEqual", "isgreaterequal");
  fb = ReplaceWord(fb, "lessThan", "isless");
  fb = ReplaceWord(fb, "greaterThan", "isgreater");
  fb = ReplaceWord(fb, "notEqual", "isnotequal");
  fb = ReplaceWord(fb, "equal", "isequal");
  const std::string frag_ret =
      mrt ? "FragOut" : (dual ? "DualOut" : "float4");
  msl << "fragment " << frag_ret << " fs_main(Varyings in [[stage_in]]";
  int slot = 0;
  for (const auto& s : samplers) {
    const char* tex_type = (s.kind == 0)   ? "texture2d<float>"
                           : (s.kind == 1) ? "texturecube<float>"
                           : (s.kind == 2) ? "texture3d<float>"
                                           : "texture2d_array<float>";
    msl << ", " << tex_type << " " << s.name << "_tex [[texture(" << slot
        << ")]], sampler " << s.name << "_smp [[sampler(" << slot << ")]]";
    ++slot;
  }
  if (use_front_facing) msl << ", bool in_ff [[front_facing]]";
  msl << ", constant Uniforms& uni [[buffer(0)]]";
  for (std::size_t i = 0; i < ubos.size(); ++i)
    msl << ", constant " << ubos[i].name << "_t& " << ubos[i].name
        << " [[buffer(" << (2 + i) << ")]]";
  msl << ") {\n";
  if (mrt || dual) {
    msl << "  " << frag_ret << " o;\n";
    std::string fb2 = fb;
    for (const auto& n : out.frag_out_names)
      fb2 = ReplaceWord(fb2, n, "o." + n);
    msl << SuffixFloatLiterals(RewriteConstructors(fb2)) << "\n  return o;\n}\n";
  } else {
    const std::string& n = out.frag_out_names[0];
    msl << "  float4 " << n << ";\n"
        << SuffixFloatLiterals(RewriteConstructors(fb)) << "\n  return " << n
        << ";\n}\n";
  }

  out.ok = true;
  out.library_source = msl.str();
  return out;
}

}  // namespace glsl
}  // namespace tgles
