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

#include <cctype>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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
    // Drop every preprocessor line: #version, #moj_import (MC expands these
    // before glShaderSource — if any survive, they only confuse decl parse),
    // #extension/#define/#ifdef — and standalone precision declarations.
    if (!t.empty() && t[0] == '#') continue;
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

// Global-scope variable declarations before `void main` (brace-depth 0 only).
// MC animate_sprite.vsh keeps `const vec2 positions[] = vec2[](...)` outside
// main; SPIRV-Cross renames it to `_60`. MainBody only takes inside main, so
// those decls must be re-emitted or `_60[index]` is undeclared in MSL.
std::string ExtractGlobalConstDecls(const std::string& src) {
  static const std::regex kMain(R"(void\s+main\s*\()");
  std::smatch mm;
  if (!std::regex_search(src, mm, kMain)) return {};
  const std::string prefix =
      src.substr(0, static_cast<std::size_t>(mm.position()));
  std::vector<int> depth(prefix.size() + 1, 0);
  int d = 0;
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (prefix[i] == '{') ++d;
    if (prefix[i] == '}') --d;
    depth[i + 1] = d;
  }
  // Require a statement boundary before the type so `in vec2 foo;` / uniform
  // block members (depth>0) never match; optional `const` covers SPIRV-Cross
  // dropping the keyword.
  static const std::regex kVar(
      R"((?:^|[;{}])\s*((?:const\s+)?(?:lowp\s+|mediump\s+|highp\s+)?(?:float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234](?:x[234])?)\s+[A-Za-z_]\w*\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;))");
  std::string out;
  for (std::sregex_iterator it(prefix.begin(), prefix.end(), kVar), end;
       it != end; ++it) {
    const auto pos = static_cast<std::size_t>((*it).position(1));
    if (depth[pos] != 0) continue;
    out += (*it)[1].str();
    if (out.empty() || out.back() != '\n') out += '\n';
  }
  return out;
}

// MSL has no `float2[]( ... )` array constructor; GLSL / SPIRV-Cross emit
// `vec2[](a,b)` / `vec2[6](a,b)` which RewriteConstructors turns into
// `float2[](...)`. Map the whole ctor to a brace initializer.
std::string RewriteArrayCtors(std::string s) {
  static const std::regex kCtor(
      R"((?:float2|float3|float4|float|int|uint|bool|int2|int3|int4|uint2|uint3|uint4)\s*\[\s*\d*\s*\]\s*\()");
  for (;;) {
    std::smatch m;
    if (!std::regex_search(s, m, kCtor)) break;
    const auto abs = static_cast<std::size_t>(m.position());
    const auto open = abs + m.length() - 1;
    int depth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t i = open; i < s.size(); ++i) {
      if (s[i] == '(') ++depth;
      if (s[i] == ')') {
        if (--depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) break;
    s = s.substr(0, abs) + "{" +
        s.substr(open + 1, close - open - 1) + "}" + s.substr(close + 1);
  }
  return s;
}

// Free function definitions outside `void main` (fog.glsl / light.glsl after
// moj_import / SPIRV-Cross). MainBody only takes inside main; dropping these
// leaves fog_spherical_distance etc. undeclared once those programs draw.
// Returns concatenated source of every non-main top-level function.
std::string ExtractGlobalFunctions(const std::string& src) {
  static const std::regex kFn(
      R"(\b(?:void|float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234](?:x[234])?)\s+([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{)");
  std::string out;
  for (std::sregex_iterator it(src.begin(), src.end(), kFn), end; it != end;
       ++it) {
    const std::string name = (*it)[1].str();
    if (name == "main") continue;
    const auto brace = static_cast<std::size_t>((*it).position() + (*it).length() - 1);
    int depth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t i = brace; i < src.size(); ++i) {
      if (src[i] == '{') ++depth;
      if (src[i] == '}') {
        if (--depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) continue;
    out += src.substr(static_cast<std::size_t>((*it).position()),
                      close - static_cast<std::size_t>((*it).position()) + 1);
    out += "\n";
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
  if (glsl == "ivec2") {
    *out = {"int2", 2, 8};
    return true;
  }
  if (glsl == "ivec3") {
    *out = {"int3", 3, 12};
    return true;
  }
  if (glsl == "ivec4") {
    *out = {"int4", 4, 16};
    return true;
  }
  if (glsl == "uvec2") {
    *out = {"uint2", 2, 8};
    return true;
  }
  if (glsl == "uvec3") {
    *out = {"uint3", 3, 12};
    return true;
  }
  if (glsl == "uvec4") {
    *out = {"uint4", 4, 16};
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

// texture(sampler, args...) / textureLod(sampler, coord, lod) with balanced
// parens -> MSL sample() call. kind: 0=2D (uv), 1=cube (dir), 2=3D (coord),
// 3=2D-array (vec3: xy + layer).
// MSL mapping (Metal Shading Language 3.x):
//   texture(s, P)           -> s_tex.sample(s_smp, P)
//   texture(s, P, bias)     -> s_tex.sample(s_smp, P, bias(b))
//   textureLod(s, P, lod)   -> s_tex.sample(s_smp, P, level(lod))
// For array textures P is vec3 (xy + layer): the layer component is split
// into the separate uint argument Metal's array sample() expects.

// SPIRV-Cross lowers clamp() to a NaN-safe form using mix(x, y, isnan(...)).
// GLSL mix accepts a bvec third arg (component select); Metal's mix requires
// a float weight — bool3 fails with "no matching function for call to mix".
// Metal spelling for the bvec form is select(a, b, cond) (true -> b).
std::string RewriteBoolMixToSelect(std::string s) {
  auto is_bool_arg = [](const std::string& a) {
    // Trim.
    std::size_t b = a.find_first_not_of(" \t\n\r");
    if (b == std::string::npos) return false;
    std::size_t e = a.find_last_not_of(" \t\n\r");
    const std::string t = a.substr(b, e - b + 1);
    if (t == "true" || t == "false") return true;
    // Boolean builtins (post-relational rename + SPIRV-Cross NaN clamp).
    static const char* kBoolFns[] = {
        "isnan(",   "isinf(",     "isfinite(",  "isunordered(",
        "isless(",  "isgreater(", "isequal(",   "isnotequal(",
        "islessequal(", "isgreaterequal(", "lessThan(", "greaterThan(",
        "lessThanEqual(", "greaterThanEqual(", "equal(", "notEqual(",
        "any(",     "all(",
    };
    for (const char* fn : kBoolFns) {
      if (t.find(fn) != std::string::npos) return true;
    }
    // Leading ! on the whole expression.
    if (!t.empty() && t[0] == '!') return true;
    return false;
  };
  // Walk every top-level mix( ... ) and rewrite when arg3 is boolean.
  std::string out;
  std::size_t pos = 0;
  while (pos < s.size()) {
    const auto at = s.find("mix(", pos);
    if (at == std::string::npos) {
      out += s.substr(pos);
      break;
    }
    // Word boundary before "mix".
    if (at > 0 && (std::isalnum(static_cast<unsigned char>(s[at - 1])) ||
                   s[at - 1] == '_' || s[at - 1] == '.')) {
      out += s.substr(pos, at + 4 - pos);
      pos = at + 4;
      continue;
    }
    out += s.substr(pos, at - pos);
    const std::size_t open = at + 3;  // points at '('
    int depth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t i = open; i < s.size(); ++i) {
      if (s[i] == '(') ++depth;
      if (s[i] == ')') {
        if (--depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) {
      out += s.substr(at);
      break;
    }
    // Rewrite nested mix( inside args first (SPIRV-Cross clamp is nested).
    const std::string args = RewriteBoolMixToSelect(
        s.substr(open + 1, close - open - 1));
    // Split top-level commas.
    std::vector<std::string> parts;
    {
      std::string cur;
      int d = 0;
      for (char c : args) {
        if (c == '(') ++d;
        if (c == ')') --d;
        if (c == ',' && d == 0) {
          parts.push_back(cur);
          cur.clear();
        } else {
          cur.push_back(c);
        }
      }
      parts.push_back(cur);
    }
    const bool use_select = parts.size() == 3 && is_bool_arg(parts[2]);
    out += use_select ? "select" : "mix";
    out += "(" + args + ")";
    pos = close + 1;
  }
  return out;
}

bool RewriteTextureCalls(std::string& body, const std::string& sampler,
                         int kind, std::string* error) {
  std::string out;
  std::size_t pos = 0;
  bool any = false;
  // Match texture( or textureLod( for this sampler (word-boundary before so
  // textureLodOffset etc. never match; Lod captured for the rewrite form).
  const std::regex kCall(
      "\\btexture(Lod|Grad)?\\s*\\(\\s*" + sampler + "\\s*,");
  // Split top-level commas into args (paren depth only; no nesting of []).
  auto split_args = [](const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    int depth = 0;
    for (char c : s) {
      if (c == '(') ++depth;
      if (c == ')') --depth;
      if (c == ',' && depth == 0) {
        parts.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    parts.push_back(cur);
    return parts;
  };
  while (true) {
    std::smatch m;
    std::string rest = body.substr(pos);
    if (!std::regex_search(rest, m, kCall)) {
      out += rest;
      break;
    }
    any = true;
    const std::string tex_kind = m[1].matched ? m[1].str() : std::string();
    const bool is_lod = tex_kind == "Lod";
    const bool is_grad = tex_kind == "Grad";
    out += rest.substr(0, static_cast<std::size_t>(m.position()));
    std::size_t arg_start =
        pos + static_cast<std::size_t>(m.position()) +
        static_cast<std::size_t>(m.length());
    // Scan to the matching close paren of texture(/textureLod(.
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
    const std::string args = body.substr(arg_start, i - arg_start - 1);
    const std::vector<std::string> parts = split_args(args);
    if (is_lod) {
      // textureLod(s, P, lod): exactly coord + lod.
      if (parts.size() != 2) {
        *error = "textureLod needs (sampler, coord, lod): " + sampler;
        return false;
      }
      const std::string& coord = parts[0];
      const std::string& lod = parts[1];
      if (kind == 3) {
        out += sampler + "_tex.sample(" + sampler + "_smp, ((" + coord +
               ").xy), uint((" + coord + ").z), level(" + lod + "))";
      } else {
        out += sampler + "_tex.sample(" + sampler + "_smp, (" + coord +
               "), level(" + lod + "))";
      }
    } else if (is_grad) {
      // textureGrad(s, P, ddx, ddy) -> s_tex.sample(s_smp, P, gradientN(...))
      if (parts.size() != 3) {
        *error = "textureGrad needs (sampler, coord, ddx, ddy): " + sampler;
        return false;
      }
      const std::string grad =
          (kind == 1) ? "gradientcube("
                      : (kind == 2) ? "gradient3d(" : "gradient2d(";
      const std::string gargs = parts[1] + ", " + parts[2] + ")";
      if (kind == 3) {
        out += sampler + "_tex.sample(" + sampler + "_smp, ((" + parts[0] +
               ").xy), uint((" + parts[0] + ").z), " + grad + gargs + ")";
      } else {
        out += sampler + "_tex.sample(" + sampler + "_smp, (" + parts[0] +
               "), " + grad + gargs + ")";
      }
    } else if (parts.size() == 1) {
      // texture(s, P)
      if (kind == 3) {
        out += sampler + "_tex.sample(" + sampler + "_smp, ((" + parts[0] +
               ").xy), uint((" + parts[0] + ").z))";
      } else {
        out += sampler + "_tex.sample(" + sampler + "_smp, (" + parts[0] + "))";
      }
    } else if (parts.size() == 2) {
      // texture(s, P, bias)
      const std::string& coord = parts[0];
      const std::string& bias = parts[1];
      if (kind == 3) {
        out += sampler + "_tex.sample(" + sampler + "_smp, ((" + coord +
               ").xy), uint((" + coord + ").z), bias(" + bias + "))";
      } else {
        out += sampler + "_tex.sample(" + sampler + "_smp, (" + coord +
               "), bias(" + bias + "))";
      }
    } else {
      *error = "unsupported texture() arity for sampler " + sampler;
      return false;
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
  std::string name;        // MSL param name (instance or block name).
  std::string block_name;  // GLSL block name (for reflection lookup).
  struct Member {
    std::string name;
    std::string msl;
    int floats = 0;
    int bytes = 0;
    int offset = 0;
  };
  std::vector<Member> members;
  int total_bytes = 0;
};

}  // namespace

bool LooksLegacyTrivial(const std::string& vs_src, const std::string& fs_src) {
  const std::string vs = StripComments(vs_src);
  const std::string fs = StripComments(fs_src);
  for (const char* tok :
       {"sampler", "texture(", "textureLod(", "dot(", "normalize("}) {
    if (vs.find(tok) != std::string::npos) return false;
    if (fs.find(tok) != std::string::npos) return false;
  }
  return true;
}

TranslatedProgram TranslateProgram(const std::string& vs_src,
                                   const std::string& fs_src,
                                   const std::map<std::string, int>* attrib_locs) {
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
  // Only reject bare `return` inside void main — helpers (fog/light after
  // moj_import) legitimately return values and must not trip this gate.
  if (HasWord(MainBody(fs), "return"))
    return fail("fragment main with early return is not supported");
  if (HasWord(vs, "discard"))
    return fail("discard in vertex shader is not supported");

  // ---- Uniform blocks / UBO (both stages, deduped by instance name; ----
  // ---- std140 mat4/vec4/vec3/vec2/float/int/uint/bool members; ----
  // ---- 16B-start total shared with the facade; vs_main/fs_main both ----
  // ---- get buffer(2+i) and the bridge binds ubo_bytes_ both stages). ----
  std::vector<ParsedUbo> ubos;
  {
    static const std::regex kBlock(
        R"(uniform\s+(\w+)\s*\{([^}]*)\}\s*(\w+)?\s*;)");
    auto parse_stage = [&](const std::string& src) -> std::string {
      for (std::sregex_iterator it(src.begin(), src.end(), kBlock), end;
           it != end; ++it) {
        const std::string block_name = (*it)[1].str();
        const std::string members_src = (*it)[2].str();
        const std::string instance =
            (*it)[3].matched ? (*it)[3].str() : block_name;
        bool known = false;
        for (const auto& existing : ubos)
          if (existing.name == instance) known = true;
        if (known) continue;
        ParsedUbo b;
        b.name = instance;
        b.block_name = block_name;
        static const std::regex kMember(R"((\w+)\s+(\w+)\s*;)");
        int offset = 0;
        for (std::sregex_iterator mi(members_src.begin(), members_src.end(),
                                     kMember),
             mend;
             mi != mend; ++mi) {
          const std::string type = (*mi)[1].str();
          const std::string name = (*mi)[2].str();
          UniformLayout lay;
          if (!UniformLayoutOf(type, &lay) ||
              (lay.msl != "float4x4" && lay.msl != "float4" &&
               lay.msl != "float3" && lay.msl != "float2" &&
               lay.msl != "float" && lay.msl != "int" && lay.msl != "int2" &&
               lay.msl != "int3" && lay.msl != "int4" && lay.msl != "uint" &&
               lay.msl != "uint2" && lay.msl != "uint3" &&
               lay.msl != "uint4" && lay.msl != "bool"))
            return "UBO member type not supported (mat4/vec4/vec3/vec2/"
                   "float/int/uint/bool/ivec*/uvec*): " +
                   type + " " + name;
          // 16-byte-start rule shared with plain uniforms (over-allocates for
          // consecutive scalars but keeps std140 offsets for the tested
          // mat4/vec4/vec3 shapes; Metal pads bool(1B) so later offsets
          // still match on little-endian).
          offset = (offset + 15) / 16 * 16;
          b.members.push_back({name, lay.msl, lay.floats, lay.bytes, offset});
          offset += lay.bytes;
        }
        if (b.members.empty())
          return "empty uniform block: " + block_name;
        b.total_bytes = (offset + 15) / 16 * 16;
        ubos.push_back(b);
      }
      return {};
    };
    // vs first (same packing order as plain uniforms), then fs dedupes.
    {
      const std::string err = parse_stage(vs);
      if (!err.empty()) return fail(err);
    }
    {
      const std::string err = parse_stage(fs);
      if (!err.empty()) return fail(err);
    }
  }

  // ---- Attributes (vertex `in`, slot convention 0/1[/2[/3]]). ----
  struct Attr {
    std::string name;
    std::string msl;
    int loc = -1;
    bool is_int = false;  // ivec/uvec/int/uint: float-converted (see docs).
    // MSL cast type for reading the attr back (VertexIn stores floatN, so
    // body reads must restore the GLSL int/uint type: `int2(in.UV2)`).
    std::string int_cast;
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
      const int explicit_loc =
          (d.location >= 0)
              ? d.location
              : (attrib_locs != nullptr
                     ? [&]() -> int {
                         auto it = attrib_locs->find(d.name);
                         return it == attrib_locs->end() ? -1 : it->second;
                       }()
                     : -1);
      const int loc = (explicit_loc >= 0) ? explicit_loc : next_loc;
      if (loc < 0 || loc > 3)
        return fail("attribute location must be 0..3: " + d.name);
      // Location 2 carries uv/normal/dir (any float width; the facade
      // interleaves slot2_comps*4 bytes and the descriptor matches). When a
      // bound location is missing, sequential declaration order may put uv
      // at 1 with no color — that is still accepted (vec2/float at 1 means
      // "no color stream"; the facade fills color from the current value).
      // Location 3 is UV2/lightmap (float2 after int-attr conversion).
      if (loc == 0 && msl != "float4" && msl != "float3")
        return fail("location 0 must be vec4/vec3: " + d.name);
      if (loc == 1 && msl != "float4" && msl != "float3" && msl != "float2" &&
          msl != "float")
        return fail("location 1 must be a float vector: " + d.name);
      if (loc == 3 && msl != "float2" && msl != "float")
        return fail("location 3 must be float2/float: " + d.name);
      for (const Attr& a : attrs) {
        if (a.loc == loc)
          return fail("duplicate attribute location: " + d.name);
      }
      std::string int_cast;
      if (is_int) {
        UniformLayout lay;
        if (UniformLayoutOf(d.type, &lay)) int_cast = lay.msl;
      }
      attrs.push_back({d.name, msl, loc, is_int, int_cast});
      int_attribs = int_attribs || is_int;
      // Sequential counter tracks max used+1 so a later unbound `in` never
      // collides with a bound location assigned out of declaration order.
      if (loc + 1 > next_loc) next_loc = loc + 1;
    }
  }
  bool has0 = false, has2 = false, has3 = false;
  for (const Attr& a : attrs) {
    if (a.loc == 0) has0 = true;
    if (a.loc == 2) has2 = true;
    if (a.loc == 3) has3 = true;
  }
  // Position is required unless the body synthesizes vertices from
  // gl_VertexID (MC screenquad/clouds/panorama blit). Color is optional:
  // POSITION-only formats never enable attrib 1 — the facade fills the
  // color stream from the current generic value (spec 10.3.1).
  const bool uses_vid_early = HasWord(vs, "gl_VertexID");
  if (!has0 && !uses_vid_early)
    return fail("vertex needs an attrib at location 0 (or gl_VertexID)");
  if (attrs.empty() && !uses_vid_early)
    return fail("vertex needs at least one attrib (or gl_VertexID)");

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
    bool in_vs = false;
    bool in_fs = false;
  };
  std::vector<SamplerInfo> samplers;
  int uniform_offset = 0;
  auto add_uniforms = [&](const std::string& src, const char* stage,
                          bool* failed) {
    const bool is_vs = (std::string(stage) == "vertex");
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
        // Vertex texture fetch (lightmap/overlay) and fragment sampling both
        // supported; stage membership drives vs_main/fs_main texture params.
        bool known = false;
        for (auto& s : samplers) {
          if (s.name == d.name) {
            known = true;
            if (is_vs)
              s.in_vs = true;
            else
              s.in_fs = true;
          }
        }
        if (!known) {
          const int kind = (d.type == "sampler2D")
                               ? 0
                               : (d.type == "samplerCube")
                                     ? 1
                                     : (d.type == "sampler3D") ? 2 : 3;
          SamplerInfo si;
          si.name = d.name;
          si.kind = kind;
          si.in_vs = is_vs;
          si.in_fs = !is_vs;
          samplers.push_back(si);
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
  out.uses_vertex_sampler = false;
  out.uses_fragment_sampler = false;
  out.sampler_in_vs.clear();
  out.sampler_in_fs.clear();
  for (const auto& s : samplers) {
    out.sampler_names.push_back(s.name);
    out.sampler_kinds.push_back(s.kind);
    out.sampler_in_vs.push_back(s.in_vs);
    out.sampler_in_fs.push_back(s.in_fs);
    if (s.in_vs) out.uses_vertex_sampler = true;
    if (s.in_fs) out.uses_fragment_sampler = true;
  }
  out.needs_slot2 = has2;
  out.slot2_comps = 0;
  for (const Attr& a : attrs) {
    if (a.loc == 2)
      out.slot2_comps = (a.msl == "float4") ? 4 : (a.msl == "float3" ? 3 : 2);
  }
  out.needs_slot3 = has3;
  out.slot3_comps = 0;
  for (const Attr& a : attrs) {
    if (a.loc == 3)
      out.slot3_comps = (a.msl == "float4") ? 4 : (a.msl == "float3" ? 3 : 2);
  }
  out.uniforms = uniforms;
  out.uniform_block_bytes = uniform_block_bytes;
  out.ubo_blocks.clear();
  for (const auto& b : ubos) {
    glsl::UboBlock o;
    o.name = b.name;
    o.block_name = b.block_name;
    o.total_bytes = b.total_bytes;
    for (const auto& m : b.members) {
      glsl::UboMember om;
      om.name = m.name;
      om.msl_type = m.msl;
      om.float_count = m.floats;
      om.offset = m.offset;
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
  // Re-emit depth-0 global var decls (animate_sprite positions[] / SPIRV-Cross
  // `_60`) at the top of main — MainBody alone would drop them.
  const std::string vmain = MainBody(vs);
  const std::string fmain = MainBody(fs);
  if (vmain.empty()) return fail("vertex main body not found");
  if (fmain.empty()) return fail("fragment main body not found");
  std::string vbody = ExtractGlobalConstDecls(vs) + vmain;
  std::string fbody = ExtractGlobalConstDecls(fs) + fmain;
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
  // Metal stage_in still needs the attribute(0)/(1) slots the keyed vertex
  // descriptor always sets (pos/col, stride 32) even when the app shader
  // never declares them (gl_VertexID-only / POSITION-only). Unused members
  // are legal MSL; omitting attribute(0) while the descriptor provides it
  // is also legal, but an empty VertexIn with a non-empty descriptor has
  // been rejected by older Metal compilers — pad to match the facade.
  bool saw0 = false, saw1 = false;
  for (const Attr& a : attrs) {
    msl << "  " << a.msl << " " << a.name << " [[attribute(" << a.loc
        << ")]];\n";
    if (a.loc == 0) saw0 = true;
    if (a.loc == 1) saw1 = true;
  }
  if (!saw0) msl << "  float4 _tgl_pad_pos [[attribute(0)]];\n";
  if (!saw1) msl << "  float4 _tgl_pad_col [[attribute(1)]];\n";
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

  // Free functions outside main (fog/light helpers). Emit once (vs-first
  // dedupe), rewritten like a stage body so constructors/UBO members match
  // MSL. Helpers that sample textures (sample_lightmap) rewrite their
  // sampler params to MSL texture/sampler pairs and texture() to sample();
  // call sites in main expand `fn(Sampler, ...)` to `fn(Sampler_tex,
  // Sampler_smp, ...)` during the stage-body rewrite below.
  struct FreeFnInfo {
    std::string name;
    std::vector<int> sampler_positions;  // 0-based param indices that are samplers.
    // Params whose GLSL int/uint type was widened to float (MSL texture
    // sample coords are float): call-site args need an explicit floatN(...)
    // wrap — MSL has no implicit intN->floatN vector conversion.
    std::map<int, std::string> float_cast_positions;
    std::vector<std::string> sampler_param_names;  // bare param names of samplers.
    bool has_texture = false;
  };
  std::vector<FreeFnInfo> free_fns;
  std::vector<std::string> free_fn_srcs;
  {
    std::string gsrc = ExtractGlobalFunctions(vs) + ExtractGlobalFunctions(fs);
    std::vector<std::string> seen_names;
    static const std::regex kFnName(
        R"(\b(?:void|float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234](?:x[234])?)\s+([A-Za-z_]\w*)\s*\()");
    std::size_t pos = 0;
    while (pos < gsrc.size()) {
      std::smatch m;
      const std::string tail = gsrc.substr(pos);
      if (!std::regex_search(tail, m, kFnName)) break;
      const auto full = pos + static_cast<std::size_t>(m.position());
      const std::string name = m[1].str();
      const auto brace_rel = full + static_cast<std::size_t>(m.length()) - 1;
      int depth = 0;
      std::size_t close = std::string::npos;
      for (std::size_t i = brace_rel; i < gsrc.size(); ++i) {
        if (gsrc[i] == '{') ++depth;
        if (gsrc[i] == '}') {
          if (--depth == 0) {
            close = i;
            break;
          }
        }
      }
      if (close == std::string::npos) break;
      std::string fn = gsrc.substr(full, close - full + 1);
      pos = close + 1;
      // Dedupe key = name + raw params so overloads (sampleNearest x2 in
      // terrain.fsh) both emit, while the same fog.glsl copy from vs+fs
      // collapses to one definition.
      const auto sig_paren = fn.find('(');
      const auto sig_brace = fn.find('{');
      const std::string sig_key =
          (sig_paren != std::string::npos && sig_brace != std::string::npos &&
           sig_brace > sig_paren)
              ? fn.substr(sig_paren, sig_brace - sig_paren)
              : std::string();
      const std::string dedupe_key = name + sig_key;
      bool dup = false;
      for (const auto& n : seen_names)
        if (n == dedupe_key) dup = true;
      if (dup || name == "main") continue;
      seen_names.push_back(dedupe_key);
      // Free functions have no `uni`/`UBO` params (those live on entry
      // points only) — only rewrite constructors and MSL-missing builtins.
      // Call sites inside main already carry Block.member / uni. prefixes.
      fn = ReplaceWord(fn, "not", "!");
      fn = ReplaceWord(fn, "lessThanEqual", "islessequal");
      fn = ReplaceWord(fn, "greaterThanEqual", "isgreaterequal");
      fn = ReplaceWord(fn, "lessThan", "isless");
      fn = ReplaceWord(fn, "greaterThan", "isgreater");
      fn = ReplaceWord(fn, "notEqual", "isnotequal");
      fn = ReplaceWord(fn, "equal", "isequal");
      fn = ReplaceWord(fn, "dFdx", "dfdx");
      fn = ReplaceWord(fn, "dFdy", "dfdy");

      // ---- Texture-sampling helpers (sample_lightmap / sampleNearest). ----
      const bool has_tex = fn.find("texture(") != std::string::npos ||
                           fn.find("textureLod(") != std::string::npos ||
                           fn.find("textureGrad(") != std::string::npos;
      const bool has_texel = fn.find("texelFetch(") != std::string::npos;
      FreeFnInfo info;
      info.name = name;
      info.has_texture = has_tex;
      if (has_texel) {
        return fail(
            "global helper uses texelFetch (not rewritten): " + name);
      }
      // Parse params whenever the signature is present: sampler detection
      // must not depend on a direct texture( call — wrappers (terrain.fsh's
      // 3-param sampleNearest) only forward to another helper and still
      // need their sampler2D param rewritten to a tex/smp pair.
      struct Fp {
        std::string type;
        std::string name;
      };
      std::vector<Fp> params;
      const auto paren = fn.find('(');
      const auto body_brace = fn.find('{');
      const bool sig_ok =
          paren != std::string::npos && body_brace != std::string::npos &&
          body_brace > paren;
      if (has_tex && !sig_ok) {
        return fail("global helper signature parse failed: " + name);
      }
      auto is_sampler_type = [](const std::string& t) {
        return t == "sampler2D" || t == "samplerCube" || t == "sampler3D" ||
               t == "sampler2DArray";
      };
      auto int_to_float = [](const std::string& t) -> std::string {
        if (t == "int" || t == "uint") return "float";
        if (t == "ivec2" || t == "uvec2") return "float2";
        if (t == "ivec3" || t == "uvec3") return "float3";
        if (t == "ivec4" || t == "uvec4") return "float4";
        return {};
      };
      if (sig_ok) {
        const std::string params_src =
            fn.substr(paren + 1, body_brace - paren - 1);
        {
          int d = 0;
          std::string cur;
          auto flush = [&]() {
            std::string t = cur;
            // trim
            while (!t.empty() && (t.front() == ' ' || t.front() == '\n'))
              t.erase(t.begin());
            while (!t.empty() && (t.back() == ' ' || t.back() == '\n'))
              t.pop_back();
            if (t.empty()) return;
            auto sp = t.find_last_of(" \t\n");
            if (sp == std::string::npos) return;
            params.push_back({t.substr(0, sp), t.substr(sp + 1)});
            cur.clear();
          };
          for (char c : params_src) {
            if (c == '(' || c == '<' ) ++d;
            if (c == ')' || c == '>') --d;
            if (c == ',' && d == 0) {
              flush();
            } else {
              cur.push_back(c);
            }
          }
          flush();
        }
      }
      bool has_sampler = false;
      for (const Fp& p : params)
        if (is_sampler_type(p.type)) has_sampler = true;
      // texture() on a helper with no sampler param samples a stage-global
      // sampler that MSL free fns cannot see — fail closed.
      if (has_tex && !has_sampler)
        return fail("global helper texture sample not on a sampler param: " +
                    name);
      if (has_sampler) {
        // Rewrite signature params: samplers → MSL tex/smp pair; integer
        // coords → float (texture() sample coords are float in MSL).
        std::string new_params;
        for (std::size_t pi = 0; pi < params.size(); ++pi) {
          const Fp& p = params[pi];
          if (!new_params.empty()) new_params += ", ";
          if (is_sampler_type(p.type)) {
            const char* tex_type =
                (p.type == "sampler2D")
                    ? "texture2d<float>"
                    : (p.type == "samplerCube")
                          ? "texturecube<float>"
                          : (p.type == "sampler3D") ? "texture3d<float>"
                                                    : "texture2d_array<float>";
            new_params += std::string(tex_type) + " " + p.name +
                          "_tex, sampler " + p.name + "_smp";
            info.sampler_positions.push_back(static_cast<int>(pi));
            info.sampler_param_names.push_back(p.name);
          } else {
            const std::string ft = int_to_float(p.type);
            if (!ft.empty())
              info.float_cast_positions[static_cast<int>(pi)] = ft;
            new_params += (ft.empty() ? p.type : ft) + " " + p.name;
          }
        }
        fn = fn.substr(0, paren + 1) + new_params + fn.substr(body_brace);
        if (has_tex) {
          // Rewrite texture(param, ...) inside the helper body.
          bool any_tex = false;
          for (std::size_t pi = 0; pi < params.size(); ++pi) {
            if (!is_sampler_type(params[pi].type)) continue;
            const int kind = (params[pi].type == "sampler2D")
                                 ? 0
                                 : (params[pi].type == "samplerCube")
                                       ? 1
                                       : (params[pi].type == "sampler3D") ? 2 : 3;
            std::string err;
            if (RewriteTextureCalls(fn, params[pi].name, kind, &err)) {
              any_tex = true;
            } else if (err.find("never sampled") != std::string::npos) {
              // texture() in the helper used a non-param (global) sampler —
              // MSL free fns cannot see stage texture bindings; fail closed.
              return fail("global helper uses non-param texture(): " + name);
            } else {
              return fail("global helper texture rewrite: " + name + ": " + err);
            }
          }
          if (!any_tex)
            return fail("global helper texture sample not on a sampler param: " +
                        name);
        }
      }
      free_fn_srcs.push_back(fn);
      free_fns.push_back(info);
    }
  }

  // Expand free-fn call sites that pass a sampler: `fn(S, args)` →
  // `fn(S_tex, S_smp, args)`. Stage mains match known sampler uniform names;
  // free-fn bodies additionally match the enclosing helper's own sampler
  // param names (extra_known — terrain.fsh sampleRGSS forwarding `source`).
  auto expand_free_fn_calls = [&](std::string& body,
                                  const std::vector<std::string>& extra_known,
                                  std::size_t search_from = 0) {
    for (const FreeFnInfo& info : free_fns) {
      if (info.sampler_positions.empty()) continue;
      std::size_t search = search_from;
      while (search < body.size()) {
        const auto at = body.find(info.name + "(", search);
        if (at == std::string::npos) break;
        // Word-boundary before the name.
        if (at > 0 && (std::isalnum(static_cast<unsigned char>(body[at - 1])) ||
                       body[at - 1] == '_')) {
          search = at + info.name.size();
          continue;
        }
        const auto open = at + info.name.size();
        int d = 0;
        std::size_t close = std::string::npos;
        for (std::size_t i = open; i < body.size(); ++i) {
          if (body[i] == '(') ++d;
          if (body[i] == ')') {
            if (--d == 0) {
              close = i;
              break;
            }
          }
        }
        if (close == std::string::npos) break;
        const std::string args_src = body.substr(open + 1, close - open - 1);
        std::vector<std::string> args;
        {
          int ad = 0;
          std::string cur;
          for (char c : args_src) {
            if (c == '(') ++ad;
            if (c == ')') --ad;
            if (c == ',' && ad == 0) {
              args.push_back(cur);
              cur.clear();
            } else {
              cur.push_back(c);
            }
          }
          args.push_back(cur);
        }
        bool changed = false;
        // int/uint params widened to float: wrap the call-site arg so the
        // type matches the rewritten signature (no implicit intN->floatN).
        for (const auto& fc : info.float_cast_positions) {
          const std::size_t pi = fc.first;
          if (pi >= args.size()) continue;
          const std::string& raw = args[pi];
          const std::size_t b =
              raw.find_first_not_of(" \t\n\r");
          if (b == std::string::npos) continue;
          const std::size_t e = raw.find_last_not_of(" \t\n\r");
          args[pi] = raw.substr(0, b) + fc.second + "(" +
                     raw.substr(b, e - b + 1) + ")" + raw.substr(e + 1);
          changed = true;
        }
        for (int sp : info.sampler_positions) {
          if (sp < 0 || static_cast<std::size_t>(sp) >= args.size()) continue;
          std::string a = args[static_cast<std::size_t>(sp)];
          std::string t = a;
          while (!t.empty() && (t.front() == ' ' || t.front() == '\n'))
            t.erase(t.begin());
          while (!t.empty() && (t.back() == ' ' || t.back() == '\n'))
            t.pop_back();
          // Strip optional in./uni. prefix for matching the uniform name.
          std::string bare = t;
          std::string prefix;
          if (bare.rfind("in.", 0) == 0) {
            prefix = "in.";
            bare = bare.substr(3);
          } else if (bare.rfind("uni.", 0) == 0) {
            prefix = "uni.";
            bare = bare.substr(4);
          }
          bool known = false;
          for (const auto& sn : out.sampler_names)
            if (sn == bare) known = true;
          if (!known)
            for (const auto& sn : extra_known)
              if (sn == bare) known = true;
          if (!known) continue;
          // Rebuild with leading whitespace preserved on the first half.
          // Suffix = text AFTER the uniform name only (was: everything from
          // li, which re-appended the name itself -> `S_smpS` garbage).
          std::string lead;
          std::size_t li = 0;
          while (li < a.size() && (a[li] == ' ' || a[li] == '\n')) {
            lead.push_back(a[li]);
            ++li;
          }
          const std::size_t name_at = li + prefix.size();
          args[static_cast<std::size_t>(sp)] =
              lead + bare + "_tex, " + bare + "_smp" +
              a.substr(name_at + bare.size());
          changed = true;
        }
        if (changed) {
          std::string rebuilt;
          for (std::size_t ai = 0; ai < args.size(); ++ai) {
            if (ai) rebuilt += ", ";
            rebuilt += args[ai];
          }
          body = body.substr(0, open + 1) + rebuilt + body.substr(close);
          search = open + 1 + rebuilt.size();
        } else {
          search = close + 1;
        }
      }
    }
  };

  // Emit collected helpers now that expand_free_fn_calls exists: first the
  // in-helper forwarders (sampleRGSS -> sampleNearest(source,...)), then the
  // rewritten source itself. Scanning starts after the body's '{' so the
  // helper's own signature head is never treated as a call site (float_cast
  // would wrap signature params: `float2(sampler x_smp)`).
  for (std::size_t fi = 0; fi < free_fn_srcs.size(); ++fi) {
    const auto body_at = free_fn_srcs[fi].find('{');
    const std::size_t from =
        body_at == std::string::npos ? 0 : body_at + 1;
    expand_free_fn_calls(free_fn_srcs[fi], free_fns[fi].sampler_param_names,
                         from);
    msl << SuffixFloatLiterals(
               RewriteArrayCtors(RewriteConstructors(free_fn_srcs[fi])))
        << "\n";
  }

  // Vertex: prefix attribs with in., outs with out., uniforms with uni.,
  // gl_Position with out.position. Struct leaves (`u.member`) use escaped
  // dots -> `uni.u_member`; arrays (`u[0]`) fall out of the base word rewrite.
  std::string vb = vbody;
  for (const Attr& a : attrs) {
    // Integer attribs arrive as floatN in VertexIn; cast back on read so
    // `ivec2 p = UV2;` becomes `int2 p = int2(in.UV2);` (valid MSL).
    if (a.is_int && !a.int_cast.empty())
      vb = ReplaceWord(vb, a.name, a.int_cast + "(in." + a.name + ")");
    else
      vb = ReplaceWord(vb, a.name, "in." + a.name);
  }
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
  for (const auto& b : ubos) {
    for (const auto& m : b.members) {
      // Same bare-member / Block.member rewrite as the fragment body
      // (vertex UBOs: DynamicTransforms/Projection in MC gui/shader pairs).
      std::regex re("(^|[^.\\w])" + m.name + "\\b");
      vb = std::regex_replace(vb, re, "$1" + b.name + "." + m.name);
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
  // Derivative built-ins: MSL spells them `dfdx`/`dfdy` (lowercase, proven
  // by compiling both spellings on-device); `fwidth` matches in both
  // languages. Coarse variants never reach here (kDeny fails them closed).
  // \b keeps `dFdxCoarse` safe even if the deny ever lifted (no boundary
  // between `dFdx` and `Coarse`).
  vb = ReplaceWord(vb, "dFdx", "dfdx");
  vb = ReplaceWord(vb, "dFdy", "dfdy");
  // Vertex texture fetch: expand free-fn sampler args, then rewrite any
  // direct texture()/textureLod( for samplers declared in the VS.
  expand_free_fn_calls(vb, {});
  std::vector<bool> sampler_sampled(samplers.size(), false);
  std::string tex_err;
  auto rewrite_stage_textures = [&](std::string& body,
                                    bool stage_is_vs) -> bool {
    for (std::size_t si = 0; si < samplers.size(); ++si) {
      const SamplerInfo& s = samplers[si];
      if (stage_is_vs ? !s.in_vs : !s.in_fs) continue;
      const std::regex re("\\btexture(Lod|Grad)?\\s*\\(\\s*" + s.name + "\\s*,");
      const bool has_direct = std::regex_search(body, re);
      const bool has_via_fn = body.find(s.name + "_tex") != std::string::npos;
      if (has_direct) {
        if (!RewriteTextureCalls(body, s.name, s.kind, &tex_err))
          return false;
        sampler_sampled[si] = true;
      } else if (has_via_fn) {
        sampler_sampled[si] = true;
      }
    }
    return true;
  };
  if (!rewrite_stage_textures(vb, true)) return fail(tex_err);
  msl << "vertex Varyings vs_main(VertexIn in [[stage_in]], constant "
         "Uniforms& uni [[buffer(0)]]";
  if (use_iid) msl << ", uint iid [[instance_id]]";
  if (use_vid) msl << ", uint vid [[vertex_id]]";
  {
    int slot = 0;
    for (const auto& s : samplers) {
      if (!s.in_vs) continue;
      const char* tex_type = (s.kind == 0)   ? "texture2d<float>"
                             : (s.kind == 1) ? "texturecube<float>"
                             : (s.kind == 2) ? "texture3d<float>"
                                             : "texture2d_array<float>";
      msl << ", " << tex_type << " " << s.name << "_tex [[texture(" << slot
          << ")]], sampler " << s.name << "_smp [[sampler(" << slot << ")]]";
      ++slot;
    }
  }
  for (std::size_t i = 0; i < ubos.size(); ++i)
    msl << ", constant " << ubos[i].name << "_t& " << ubos[i].name
        << " [[buffer(" << (2 + i) << ")]]";
  msl << ") {\n  Varyings out;\n"
      << SuffixFloatLiterals(RewriteArrayCtors(RewriteConstructors(vb)))
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
  expand_free_fn_calls(fb, {});
  if (!rewrite_stage_textures(fb, false)) return fail(tex_err);
  fb = ReplaceWord(fb, "discard", "discard_fragment()");
  if (use_front_facing) fb = ReplaceWord(fb, "gl_FrontFacing", "in_ff");
  fb = ReplaceWord(fb, "not", "!");
  fb = ReplaceWord(fb, "lessThanEqual", "islessequal");
  fb = ReplaceWord(fb, "greaterThanEqual", "isgreaterequal");
  fb = ReplaceWord(fb, "lessThan", "isless");
  fb = ReplaceWord(fb, "greaterThan", "isgreater");
  fb = ReplaceWord(fb, "notEqual", "isnotequal");
  fb = ReplaceWord(fb, "equal", "isequal");
  // Same derivative spelling fix as the vertex stage (see above).
  fb = ReplaceWord(fb, "dFdx", "dfdx");
  fb = ReplaceWord(fb, "dFdy", "dfdy");
  const std::string frag_ret =
      mrt ? "FragOut" : (dual ? "DualOut" : "float4");
  msl << "fragment " << frag_ret << " fs_main(Varyings in [[stage_in]]";
  int slot = 0;
  for (const auto& s : samplers) {
    if (!s.in_fs) continue;
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
    msl << SuffixFloatLiterals(RewriteArrayCtors(RewriteConstructors(fb2)))
        << "\n  return o;\n}\n";
  } else {
    const std::string& n = out.frag_out_names[0];
    msl << "  float4 " << n << ";\n"
        << SuffixFloatLiterals(RewriteArrayCtors(RewriteConstructors(fb)))
        << "\n  return " << n << ";\n}\n";
  }

  for (std::size_t si = 0; si < samplers.size(); ++si) {
    if ((samplers[si].in_vs || samplers[si].in_fs) && !sampler_sampled[si])
      return fail(
          "declared sampler never sampled with texture(): " +
          samplers[si].name);
  }

  out.ok = true;
  out.library_source = RewriteBoolMixToSelect(msl.str());
  return out;
}

}  // namespace glsl
}  // namespace tgles
