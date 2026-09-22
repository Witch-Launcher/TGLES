#!/usr/bin/env python3
"""Generate the host ABI prototype header and the gap table for TGL.

Why a generator: the ABI is data, not logic. 371 GLES names plus 46 EGL names
must match Khronos headers verbatim; typing them by hand invites silent ABI
mismatches, which is the one class of bug a GLES host cannot defend against. So:

  in : tools/mobilegl/host_contract.json   (names the host loader requires)
       docs/reference/*.h                  (Khronos reference headers)
       src/host/abi/gl_real.cpp            (names already implemented)
  out: include/tgles/host/abi_gl.h         (prototypes, verbatim from Khronos)
       src/host/abi/gl_gap.cpp             (declared gaps for the rest)
       src/host/abi/gl_symbols.inc         (name -> address table for dispatch)

Run:   python3 tools/host_abi/gen_gl_abi.py
Check: python3 tools/host_abi/gen_gl_abi.py --check
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REFERENCE = os.path.join(ROOT, "docs", "reference")
CONTRACT = os.path.join(ROOT, "tools", "mobilegl", "host_contract.json")
# Extra vendor prototypes for contract names no Khronos header declares
# (e.g. ANGLE-suffixed entry points MobileGL loads as OPTIONAL).
EXTRA_PROTOTYPES = os.path.join(ROOT, "tools", "host_abi",
                                "extra_ext_prototypes.h")
REAL_SOURCE = os.path.join(ROOT, "src", "host", "abi", "gl_real.cpp")
# Trial-subset real implementations live here until they move into gl_real.cpp
# (Block A). Scanning both files keeps gl_gap.cpp from redefining a name that
# already has a real body — duplicate symbols would fail the link.
REAL_SOURCES = (
    REAL_SOURCE,
    os.path.join(ROOT, "src", "host", "host_c_api.cpp"),
)
HEADER_OUT = os.path.join(ROOT, "include", "tgles", "host", "abi_gl.h")
GAP_OUT = os.path.join(ROOT, "src", "host", "abi", "gl_gap.cpp")
SYMBOLS_OUT = os.path.join(ROOT, "src", "host", "abi", "gl_symbols.inc")
# Declared-gap name list consumed by dispatch.cpp for tglesAbiGapCount/Name.
# The ledger only counts gaps *after* they are called; this file enumerates
# gaps *whether or not* they were called, so the debt ceiling is checkable.
GAP_NAMES_OUT = os.path.join(ROOT, "src", "host", "abi", "gap_names.inc")
# Per-entry support table for programmers (requested review artifact): every
# contract name with its status, regenerated so it can never go stale.
ENTRIES_EN_OUT = os.path.join(ROOT, "docs", "en", "host-entries.md")
ENTRIES_VI_OUT = os.path.join(ROOT, "docs", "vi", "host-entries.md")

# Khronos prototype lines look like:
#   GL_APICALL void GL_APIENTRY glActiveTexture (GLenum texture);   (gl32.h)
#   GLAPI void APIENTRY glPointSize (GLfloat size);                 (glcorearb.h)
#   GL_APICALL const GLubyte *GL_APIENTRY glGetString (GLenum name); (gl32.h)
# Both spellings must be accepted: MobileGL's contract mixes ES core names with
# a few desktop/EXT names (glPointSize, glTexStorage1D, glMapBufferOES, ...).
# NOTE: Khronos writes `*GL_APIENTRY` with NO space between `*` and the calling
# convention macro. The regex below therefore tolerates an optional `*` glued to
# the macro, and clean_prototype() strips any trailing macro token from the
# captured return type (otherwise `APIENTRY` leaks into abi_gl.h and breaks
# every translation unit that includes it — see Block 0 fix).
PROTO_RE = re.compile(
    r"^\s*(?:GL_APICALL|EGLAPI|GLAPI)\s+"
    r"(?P<ret>[A-Za-z_][A-Za-z0-9_ \t\*]*?)\s*"
    r"(?:GL_APIENTRY|EGLAPIENTRY|APIENTRY|GL_APIENTRYP|EGLAPIENTRYP)?\s*"
    r"(?P<name>gl[A-Za-z0-9_]+|egl[A-Za-z0-9_]+)\s*"
    r"\((?P<args>[^;]*)\)\s*;",
    re.MULTILINE,
)

_CALL_CONV_SUFFIX = re.compile(
    r"\s*\*?\s*(?:GL_APIENTRY|EGLAPIENTRY|APIENTRY|GL_APIENTRYP|EGLAPIENTRYP)\s*\*?\s*$"
)


def clean_prototype(ret: str, name: str, args: str) -> str:
    """Return `ret name(args);` with calling-convention macros stripped.

    Human-readable rule: abi_gl.h stores plain C prototypes (no GL_APICALL /
    APIENTRY), because TGL compiles them itself. The Khronos spelling is only
    the *source* of the signature, not something we propagate.
    """
    ret = " ".join(ret.split())
    # Strip a calling-convention token glued to the return type, e.g.
    #   "const GLubyte *APIENTRY" -> "const GLubyte *"
    #   "void *GL_APIENTRY"       -> "void *"
    ret = _CALL_CONV_SUFFIX.sub("", ret).strip()
    # Normalise pointer spacing: "void*" / "void *" / "void  *" -> "void *".
    ret = re.sub(r"\s*\*\s*", " *", ret).strip()
    args = args.strip()
    return f"{ret} {name}({args});"


def strip_arg_names(args: str) -> str:
    """Drop parameter names, keep types: `GLenum target` -> `GLenum`.

    Gap bodies never touch their arguments (they only record the call), so
    named parameters would trip -Wunused-parameter in all 341 definitions.
    The declarations in abi_gl.h keep the Khronos names for readers; the
    definitions here keep only types.
    """
    args = args.strip()
    if args in ("", "void"):
        return args
    parts, depth, cur = [], 0, ""
    for ch in args:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    out = []
    for part in parts:
        part = part.strip()
        unnamed = re.sub(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*$",
                         r"\2", part).strip()
        out.append(unnamed if unnamed else part)
    return ", ".join(out)


def default_for_return(ret: str) -> str:
    """Spec default for a gap body: 'no effect' as seen by the caller."""
    ret = ret.strip()
    if ret == "void":
        return "return;"
    if "GLboolean" in ret:
        return "return GL_FALSE;"
    if "*" in ret or "GLsync" in ret:
        return "return nullptr;"
    return DEFAULT_BY_RET.get(ret, "return 0;")

# Zero-return defaults for generated gaps, per return type. A gap returns the
# spec default for its call class: "the command had no effect" as seen by the
# caller (0 / GL_FALSE / NULL), never a sentinel address.
DEFAULT_BY_RET = {
    "void": "return;",
    "GLboolean": "return GL_FALSE;",
    "GLuint": "return 0;",
    "GLint": "return 0;",
    "GLsizei": "return 0;",
    "GLenum": "return 0;",
    "GLfloat": "return 0.0f;",
    "GLint64": "return 0;",
    "GLuint64": "return 0;",
    "GLint64EXT": "return 0;",
    "GLuint64EXT": "return 0;",
    "GLsync": "return 0;",
}

# Definitions in gl_real.cpp are written without GL_APICALL (they are C++
# translation units compiled by TGL itself), so they need their own pattern.
DEFINITION_RE = re.compile(
    r"^\s*(?P<ret>[A-Za-z_][A-Za-z0-9_ \t\*]*?)\s+"
    r"(?P<name>gl[A-Za-z0-9_]+|egl[A-Za-z0-9_]+)\s*\(",
    re.MULTILINE,
)


def reference_prototypes() -> dict[str, str]:
    """name -> clean prototype text, taken from the Khronos reference headers.

    Priority is ES-first: glcorearb.h (desktop) is read first so that gl32.h /
    glext.h overwrite it. A GLES host must expose the ES spelling, not the
    desktop one, when both declare the same name (glGetString, glMapBufferRange,
    glDebugMessageCallback, ...).
    """
    found: dict[str, str] = {}
    for fname in ("glcorearb.h", "gl3.h", "gl31.h", "glext.h", "gl32.h",
                  "eglext.h", "egl.h"):
        path = os.path.join(REFERENCE, fname)
        if not os.path.exists(path):
            continue
        for line in open(path, encoding="utf-8", errors="replace").read().splitlines():
            m = PROTO_RE.match(line)
            if not m:
                continue
            name = m.group("name")
            # Desktop headers come first in the list, ES headers last, so a
            # later ES spelling overwrites an earlier desktop one. That is
            # exactly what a GLES host wants (glGetString, glMapBufferRange,
            # glDebugMessageCallback, ...).
            found[name] = clean_prototype(m.group("ret"), name, m.group("args"))
    # Vendor extras last: they only add names no Khronos header declares.
    if os.path.exists(EXTRA_PROTOTYPES):
        for line in open(EXTRA_PROTOTYPES, encoding="utf-8",
                         errors="replace").read().splitlines():
            m = PROTO_RE.match(line)
            if not m:
                continue
            name = m.group("name")
            if name not in found:
                found[name] = clean_prototype(m.group("ret"), name,
                                              m.group("args"))
    return found


def implemented_names() -> set[str]:
    """Names with a real body. Deriving the list from the sources removes list
    drift: move a function from gl_gap.cpp into gl_real.cpp and regenerate."""
    names: set[str] = set()
    for path in REAL_SOURCES:
        try:
            text = open(path, encoding="utf-8").read()
        except FileNotFoundError:
            continue
        names.update(m.group("name") for m in DEFINITION_RE.finditer(text))
    return names


def implemented_names_by_file() -> dict[str, set[str]]:
    """Per-file real-name sets, for the entries table's Served-by column."""
    out: dict[str, set[str]] = {}
    for path in REAL_SOURCES:
        try:
            text = open(path, encoding="utf-8").read()
        except FileNotFoundError:
            continue
        short = os.path.relpath(path, ROOT)
        out[short] = {m.group("name")
                      for m in DEFINITION_RE.finditer(text)}
    return out


def emit(header: list[str], gap_lines: list[str], symbols: list[str],
         gap_names: list[str], en_doc: list[str], vi_doc: list[str]) -> None:
    for path, text in ((HEADER_OUT, "\n".join(header)),
                       (GAP_OUT, "\n".join(gap_lines)),
                       (SYMBOLS_OUT, "\n".join(symbols) + "\n"),
                       (GAP_NAMES_OUT, "\n".join(gap_names) + "\n"),
                       (ENTRIES_EN_OUT, "\n".join(en_doc)),
                       (ENTRIES_VI_OUT, "\n".join(vi_doc))):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w", encoding="utf-8").write(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="report drift, do not write")
    args = parser.parse_args()

    contract = json.load(open(CONTRACT, encoding="utf-8"))
    required = contract["required_gles"]
    optional = contract.get("optional_gles", [])
    reference = reference_prototypes()
    implemented = implemented_names()

    missing = [n for n in required if n not in reference]
    if missing:
        print(f"error: no Khronos prototype for {len(missing)} names, "
              f"e.g. {missing[:5]}", file=sys.stderr)
        return 1

    gaps = sorted(set(required) - implemented)

    # OPTIONAL names with real bodies are emitted too (prototypes + dispatch
    # addresses) so eglGetProcAddress serves them; unimplemented optional
    # names stay NULL, which is the correct MobileGL semantic (call sites
    # null-check). An implemented optional without a prototype is a bug.
    optional_implemented = sorted(set(optional) & implemented)
    optional_missing_proto = [n for n in optional_implemented
                              if n not in reference]
    if optional_missing_proto:
        print(f"error: no Khronos prototype for implemented optional names: "
              f"{optional_missing_proto}", file=sys.stderr)
        return 1

    header = [
        "// GENERATED by tools/host_abi/gen_gl_abi.py - do not edit by hand.",
        "//",
        f"// Prototypes for the {len(required)} GLES entry points that a host",
        "// loader requires. Signatures are normalised from docs/reference/*.h",
        "// (Khronos) to plain C — no GL_APICALL / APIENTRY macros — so TGL and",
        "// a host using its own typedefs expose the identical ABI.",
        "//",
        f"// Implemented for real in src/host/abi/gl_real.cpp: "
        f"{len(required) - len(gaps)}.",
        f"// Declared gaps in src/host/abi/gl_gap.cpp: {len(gaps)}.",
        f"// Implemented OPTIONAL entry points (MobileGL INIT_GLES_FUNC_OPTIONAL"
        f" with real bodies, served like required): "
        f"{len(optional_implemented)}.",
        "",
        "#ifndef TGLES_HOST_ABI_GL_H",
        "#define TGLES_HOST_ABI_GL_H",
        "",
        "#include \"tgles/host/abi.h\"",
        "",
        "// Compatibility for any hand-written prototype that still spells the",
        "// Khronos calling convention explicitly. Generated lines below never",
        "// use these macros; they exist so a stale edit fails loudly nowhere.",
        "#ifndef GL_APICALL",
        "#define GL_APICALL",
        "#endif",
        "#ifndef GL_APIENTRY",
        "#define GL_APIENTRY",
        "#endif",
        "#ifndef APIENTRY",
        "#define APIENTRY GL_APIENTRY",
        "#endif",
        "#ifndef EGLAPIENTRY",
        "#define EGLAPIENTRY",
        "#endif",
        "// KHR_debug callback type (ES 3.2 spec 18 / docs/reference/gl32.h:1505).",
        "// Defined here so glDebugMessageCallback compiles without pulling a",
        "// system GLES header (macOS ships none).",
        "#ifndef TGLES_GLDEBUGPROC_DEFINED",
        "#define TGLES_GLDEBUGPROC_DEFINED",
        "typedef void (*GLDEBUGPROC)(GLenum source, GLenum type, GLuint id,",
        "                            GLenum severity, GLsizei length,",
        "                            const GLchar *message,",
        "                            const void *userParam);",
        "#endif",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    header += [reference[n] for n in required]
    if optional_implemented:
        header += ["",
                   "// OPTIONAL entry points with real bodies (not required by",
                   "// the loader, but served when present so eglGetProcAddress",
                   "// agrees with dlsym; unimplemented optionals stay NULL).",
                   ""]
        header += [reference[n] for n in optional_implemented]
    header += ["", "#ifdef __cplusplus", "}", "#endif", "",
               "#endif  // TGLES_HOST_ABI_GL_H", ""]

    gap_lines = [
        "// GENERATED gap bodies by tools/host_abi/gen_gl_abi.py.",
        "//",
        "// These entry points exist so the ABI is complete - a host that resolves",
        "// them gets a real function, never a sentinel address - but the Metal",
        "// execution path does not serve them yet. Every call records itself in the",
        "// ABI ledger, so the gap is measured instead of hidden. Move a function",
        "// into gl_real.cpp and regenerate to close one.",
        "",
        "#include \"tgles/host/abi_gl.h\"",
        "",
        "#include <cstddef>",
        "",
        "#include \"tgles/host/abi_ledger.h\"",
        "",
    ]
    for name in gaps:
        # reference[name] is a prototype ending in ";". The definition keeps
        # the return type and name but drops parameter names (see
        # strip_arg_names): a gap never reads its arguments.
        proto = reference[name]
        ret = proto.split(name)[0].strip()
        arg_text = proto[proto.index("(") + 1:proto.rindex(")")]
        signature = f"{ret} {name}({strip_arg_names(arg_text)})"
        default = default_for_return(ret)
        gap_lines.append(f"{signature} {{  // gap")
        gap_lines.append(f'  tgles::host::LedgerRecord("{name}");')
        gap_lines.append(f"  {default}")
        gap_lines.append("}")
        gap_lines.append("")

    symbols = ["// GENERATED by tools/host_abi/gen_gl_abi.py.",
               "// name -> address table consumed by src/host/abi/dispatch.cpp.",
               "// The table covers required_gles plus implemented optional_gles;",
               "// EGL names live in abi.h and are",
               "// served by src/host/abi/egl_api.cpp via the same lookup.",
               ""]
    symbols += [f'  {{"{n}", reinterpret_cast<void*>({n})}},' for n in required]
    symbols += [f'  {{"{n}", reinterpret_cast<void*>({n})}},'
                for n in optional_implemented]

    gap_names = ["// GENERATED by tools/host_abi/gen_gl_abi.py.",
                 "// Declared-gap names consumed by dispatch.cpp for",
                 "// tglesAbiGapCount/tglesAbiGapName. Sorted so the enumeration",
                 "// is deterministic across runs and machines.",
                 ""]
    gap_names += [f'  "{n}",' for n in gaps]

    by_file = implemented_names_by_file()
    trial_names = by_file.get(os.path.join("src", "host", "host_c_api.cpp"),
                              set())

    def served_by(name: str) -> str:
        if name in trial_names:
            return "`host_c_api.cpp` (trial subset)"
        if name in implemented:
            return "`gl_real.cpp`"
        return "ledger gap"

    en_doc = [
        "<!-- GENERATED by tools/host_abi/gen_gl_abi.py - do not edit. -->",
        "",
        "# Host entries: support status per contract name",
        "",
        f"{len(required) - len(gaps)} real, {len(gaps)} declared gaps "
        f"(of {len(required)} required GLES). A gap resolves to a real "
        "function that records itself in the ledger and returns its call "
        "class's spec default — see `host-abi.md`. Regenerate with "
        "`python3 tools/host_abi/gen_gl_abi.py`.",
        "",
        "| Entry point | Status | Served by |",
        "|---|---|---|",
    ]
    vi_doc = [
        "<!-- GENERATED by tools/host_abi/gen_gl_abi.py - do not edit. -->",
        "",
        "# Host entries: trạng thái từng tên trong contract",
        "",
        f"{len(required) - len(gaps)} real, {len(gaps)} gaps khai báo "
        f"(trên {len(required)} GLES required). Gap resolve ra hàm thật: tự "
        "ghi vào ledger rồi trả default của spec — xem `host-abi.md`. "
        "Regenerate bằng `python3 tools/host_abi/gen_gl_abi.py`.",
        "",
        "| Entry point | Status | Served by |",
        "|---|---|---|",
    ]
    for n in required:
        status = "gap" if n in gaps else "real"
        row = f"| `{n}` | {status} | {served_by(n)} |"
        en_doc.append(row)
        vi_doc.append(row)
    en_doc += ["",
               "## Optional (INIT_GLES_FUNC_OPTIONAL): "
               f"{len(optional_implemented)} real of {len(optional)} "
               "(the rest correctly resolve to NULL)",
               "",
               "| Entry point | Status | Served by |",
               "|---|---|---|"]
    vi_doc += ["",
               "## Optional (INIT_GLES_FUNC_OPTIONAL): "
               f"{len(optional_implemented)} real trên {len(optional)} "
               "(phần còn lại resolve NULL đúng semantic)",
               "",
               "| Entry point | Status | Served by |",
               "|---|---|---|"]
    for n in optional:
        status = ("real" if n in implemented
                  else "null (not required)")
        row = f"| `{n}` | {status} | {served_by(n) if n in implemented else '-'} |"
        en_doc.append(row)
        vi_doc.append(row)
    en_doc.append("")
    vi_doc.append("")
    en_text = "\n".join(en_doc)
    vi_text = "\n".join(vi_doc)

    header_text = "\n".join(header)
    gap_text = "\n".join(gap_lines)
    symbols_text = "\n".join(symbols) + "\n"
    gap_names_text = "\n".join(gap_names) + "\n"
    if args.check:
        drift = []
        for path, fresh in ((HEADER_OUT, header_text), (GAP_OUT, gap_text),
                            (SYMBOLS_OUT, symbols_text),
                            (GAP_NAMES_OUT, gap_names_text),
                            (ENTRIES_EN_OUT, en_text),
                            (ENTRIES_VI_OUT, vi_text)):
            try:
                current = open(path, encoding="utf-8").read()
            except FileNotFoundError:
                drift.append(f"{path} (missing)")
                continue
            if current != fresh:
                drift.append(path)
        if drift:
            print("error: generated host ABI is stale:", file=sys.stderr)
            for path in drift:
                print(f"  {path}", file=sys.stderr)
            print("run: python3 tools/host_abi/gen_gl_abi.py", file=sys.stderr)
            return 1
        print(f"contract GLES names : {len(required)}")
        print(f"implemented (real)  : {len(required) - len(gaps)}")
        print(f"declared gaps       : {len(gaps)}")
        print(f"optional real       : {len(optional_implemented)} "
              f"(of {len(optional)})")
        print("host ABI in sync.")
        return 0

    emit(header, gap_lines, symbols, gap_names, en_doc, vi_doc)
    print(f"contract GLES names : {len(required)}")
    print(f"implemented (real)  : {len(required) - len(gaps)}")
    print(f"declared gaps       : {len(gaps)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
