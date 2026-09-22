#!/usr/bin/env python3
"""MobileGL-faithful dlopen check for the TGL host library.

Replicates EXACTLY what MobileGL does at startup on macOS
(MobileGL/MG_Util/BackendLoaders/OpenGL/Loader.cpp, AcquireGLESFunctions):

  1. dlopen("libtinygl4angle.dylib")                     (Loader.cpp:585)
  2. resolve every INIT_EGL_FUNC(name) via dlsym;
     a NULL is FATAL in MobileGL (MGLOG_F, Loader.cpp:631)
  3. resolve every INIT_GLES_FUNC(name) via eglGetProcAddress;
     a NULL is an error log (Loader.cpp:141) - the entry point is unusable
  4. resolve every INIT_GLES_FUNC_OPTIONAL(name) via eglGetProcAddress;
     a NULL is expected on drivers without the extension (Loader.cpp:145)

The symbol lists are parsed from the LIVE Loader.cpp (not from a cached
JSON), so loader drift fails loudly. Afterwards a real EGL+GLES sequence
is driven exclusively through the resolved pointers (bootstrap, version
strings, buffer upload, shader compile/link, VAO setup, draw, error drain).

Exit 0 iff: no EGL missing, no required-GLES missing, drive sequence clean.
Optional misses never fail (matches MobileGL semantics).

Usage:
  python3 tools/mobilegl/dlopen_check.py \
      --loader /path/to/MobileGL/MobileGL/MG_Util/BackendLoaders/OpenGL/Loader.cpp \
      --lib /path/to/libtinygl4angle.dylib
"""

import argparse
import ctypes
import re
import sys


def parse_loader(path):
    text = open(path).read()
    # Only real invocations (gl*/egl* names); the macro DEFINITIONS contain
    # the literal placeholder `name` and must not be counted.
    req_gles = re.findall(r"INIT_GLES_FUNC\((gl\w+)\)", text)
    opt_gles = re.findall(r"INIT_GLES_FUNC_OPTIONAL\((gl\w+)\)", text)
    req_egl = re.findall(r"INIT_EGL_FUNC\((egl\w+)\)", text)
    # Deduplicate while preserving order (macros may repeat across blocks).
    def uniq(xs):
        seen, out = set(), []
        for x in xs:
            if x not in seen:
                seen.add(x)
                out.append(x)
        return out
    return uniq(req_gles), uniq(opt_gles), uniq(req_egl)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--loader", required=True)
    ap.add_argument("--lib", required=True)
    args = ap.parse_args()

    req_gles, opt_gles, req_egl = parse_loader(args.loader)
    print(f"loader lists: {len(req_gles)} required GLES, "
          f"{len(opt_gles)} optional GLES, {len(req_egl)} required EGL")

    try:
        lib = ctypes.CDLL(args.lib)
    except OSError as e:
        print(f"FATAL: dlopen({args.lib}) failed: {e}")
        print("(MobileGL would MGLOG_F here: no EGL library)")
        return 1

    fails = []

    # --- EGL via dlsym (FATAL on NULL, like MobileGL) ---
    egl_missing = []
    egl = {}
    for name in req_egl:
        try:
            egl[name] = getattr(lib, name)
        except AttributeError:
            egl_missing.append(name)
    if egl_missing:
        print(f"FATAL: {len(egl_missing)} EGL symbols missing (MobileGL "
              f"would MGLOG_F):")
        for n in egl_missing:
            print(f"  missing EGL: {n}")
        fails.append("egl-missing")
    else:
        print(f"EGL: all {len(req_egl)} required symbols resolve via dlsym")

    # --- GLES via eglGetProcAddress (error log on NULL, like MobileGL) ---
    if "eglGetProcAddress" not in egl:
        print("FATAL: eglGetProcAddress itself missing")
        return 1
    egl["eglGetProcAddress"].restype = ctypes.c_void_p
    egl["eglGetProcAddress"].argtypes = [ctypes.c_char_p]

    def proc(name):
        return egl["eglGetProcAddress"](name.encode())

    gles_missing = [n for n in req_gles if not proc(n)]
    if gles_missing:
        print(f"ERROR: {len(gles_missing)} required GLES symbols NULL "
              f"(MobileGL would MGLOG_E each):")
        for n in gles_missing:
            print(f"  missing GLES: {n}")
        fails.append("gles-missing")
    else:
        print(f"GLES: all {len(req_gles)} required symbols resolve via "
              f"eglGetProcAddress")

    opt_missing = [n for n in opt_gles if not proc(n)]
    print(f"GLES optional: {len(opt_gles) - len(opt_missing)}/"
          f"{len(opt_gles)} resolve "
          f"(NULL expected without the extension)")
    for n in opt_missing:
        print(f"  optional absent (ok): {n}")

    # --- dlsym/proc agreement for required GLES (MobileGL resolves GLES
    # --- only via procAddress, but TGL promises both views agree) ---
    disagree = []
    for n in req_gles:
        try:
            direct = getattr(lib, n)
        except AttributeError:
            continue  # already counted in gles_missing
        if ctypes.cast(direct, ctypes.c_void_p).value != proc(n):
            disagree.append(n)
    if disagree:
        print(f"WARN: {len(disagree)} dlsym/proc disagreements:")
        for n in disagree[:10]:
            print(f"  disagree: {n}")
    else:
        print("dlsym/eglGetProcAddress agree on all required GLES names")

    # --- drive a real sequence through resolved pointers only ---
    print("--- drive sequence (resolved pointers only) ---")
    c_void_p = ctypes.c_void_p
    c_int = ctypes.c_int

    def fn(name, restype, argtypes):
        addr = proc(name)
        if not addr:
            raise RuntimeError(f"cannot drive: {name} is NULL")
        f = ctypes.CFUNCTYPE(restype, *argtypes)(addr)
        return f

    EGLDisplay = c_void_p
    ok = True

    def check(cond, what):
        nonlocal ok
        if cond:
            print(f"  ok: {what}")
        else:
            print(f"  FAIL: {what}")
            ok = False

    eglGetDisplay = fn("eglGetDisplay", EGLDisplay, [c_void_p])
    eglInitialize = fn("eglInitialize", c_int, [EGLDisplay,
                                                ctypes.POINTER(c_int),
                                                ctypes.POINTER(c_int)])
    eglChooseConfig = fn("eglChooseConfig", c_int,
                         [EGLDisplay, ctypes.POINTER(c_int), c_void_p,
                          c_int, ctypes.POINTER(c_int)])
    eglCreateContext = fn("eglCreateContext", c_void_p,
                          [EGLDisplay, c_void_p, c_void_p,
                           ctypes.POINTER(c_int)])
    eglCreatePbufferSurface = fn("eglCreatePbufferSurface", c_void_p,
                                 [EGLDisplay, c_void_p,
                                  ctypes.POINTER(c_int)])
    eglMakeCurrent = fn("eglMakeCurrent", c_int,
                        [EGLDisplay, c_void_p, c_void_p, c_void_p])
    eglGetError = fn("eglGetError", c_int, [])
    eglTerminate = fn("eglTerminate", c_int, [EGLDisplay])
    eglDestroyContext = fn("eglDestroyContext", c_int, [EGLDisplay, c_void_p])
    eglDestroySurface = fn("eglDestroySurface", c_int, [EGLDisplay, c_void_p])

    dpy = eglGetDisplay(None)
    check(dpy, "eglGetDisplay")
    check(eglInitialize(dpy, None, None), "eglInitialize")
    # Minimal config request: RENDERABLE_TYPE=ES3, terminated by EGL_NONE.
    cfg_attribs = (c_int * 4)(0x3040, 0x40, 0x3038, 0)
    cfg = c_void_p()
    num = c_int(0)
    check(eglChooseConfig(dpy, cfg_attribs, ctypes.byref(cfg), 1,
                          ctypes.byref(num)) and num.value >= 1,
          "eglChooseConfig")
    ctx_attribs = (c_int * 5)(0x3098, 3, 0x30FB, 2, 0x3038)
    ctx = eglCreateContext(dpy, cfg, None, ctx_attribs)
    check(ctx, "eglCreateContext")
    pb_attribs = (c_int * 5)(0x3057, 16, 0x3056, 16, 0x3038)
    surf = eglCreatePbufferSurface(dpy, cfg, pb_attribs)
    check(surf, "eglCreatePbufferSurface")
    check(eglMakeCurrent(dpy, surf, surf, ctx), "eglMakeCurrent")
    check(eglGetError() == 0x3000, "egl no error")

    glGetString = fn("glGetString", c_void_p, [ctypes.c_uint])
    glGetError = fn("glGetError", ctypes.c_uint, [])
    glGetIntegerv = fn("glGetIntegerv", None,
                       [ctypes.c_uint, ctypes.POINTER(c_int)])
    glGenBuffers = fn("glGenBuffers", None,
                      [c_int, ctypes.POINTER(ctypes.c_uint)])
    glBindBuffer = fn("glBindBuffer", None,
                      [ctypes.c_uint, ctypes.c_uint])
    glBufferData = fn("glBufferData", None,
                      [ctypes.c_uint, ctypes.c_ssize_t, c_void_p,
                       ctypes.c_uint])
    glCreateShader = fn("glCreateShader", ctypes.c_uint, [ctypes.c_uint])
    glShaderSource = fn("glShaderSource", None,
                        [ctypes.c_uint, c_int,
                         ctypes.POINTER(ctypes.c_char_p),
                         ctypes.POINTER(c_int)])
    glCompileShader = fn("glCompileShader", None, [ctypes.c_uint])
    glGetShaderiv = fn("glGetShaderiv", None,
                       [ctypes.c_uint, ctypes.c_uint,
                        ctypes.POINTER(c_int)])
    glCreateProgram = fn("glCreateProgram", ctypes.c_uint, [])
    glAttachShader = fn("glAttachShader", None,
                        [ctypes.c_uint, ctypes.c_uint])
    glLinkProgram = fn("glLinkProgram", None, [ctypes.c_uint])
    glGetProgramiv = fn("glGetProgramiv", None,
                        [ctypes.c_uint, ctypes.c_uint,
                         ctypes.POINTER(c_int)])
    glUseProgram = fn("glUseProgram", None, [ctypes.c_uint])
    glGenVertexArrays = fn("glGenVertexArrays", None,
                           [c_int, ctypes.POINTER(ctypes.c_uint)])
    glBindVertexArray = fn("glBindVertexArray", None, [ctypes.c_uint])
    glEnableVertexAttribArray = fn("glEnableVertexAttribArray", None,
                                   [ctypes.c_uint])
    glVertexAttribPointer = fn("glVertexAttribPointer", None,
                               [ctypes.c_uint, c_int, ctypes.c_uint,
                                ctypes.c_ubyte, c_int, c_void_p])
    glDrawArrays = fn("glDrawArrays", None,
                      [ctypes.c_uint, c_int, c_int])
    glDeleteBuffers = fn("glDeleteBuffers", None,
                         [c_int, ctypes.POINTER(ctypes.c_uint)])

    GL_VERSION = 0x1F02
    s = glGetString(GL_VERSION)
    ver = ctypes.string_at(s).decode() if s else ""
    check("OpenGL ES 3.2" in ver, f"GL_VERSION='{ver}'")
    major = c_int(0)
    glGetIntegerv(0x821B, ctypes.byref(major))
    check(major.value == 3, "GL_MAJOR_VERSION==3")

    buf = ctypes.c_uint(0)
    glGenBuffers(1, ctypes.byref(buf))
    check(buf.value != 0, "glGenBuffers")
    verts = (ctypes.c_float * 9)(0, 0, 0, 1, 0, 0, 0, 1, 0)
    glBindBuffer(0x8892, buf.value)
    glBufferData(0x8892, ctypes.sizeof(verts), verts, 0x88E4)
    check(glGetError() == 0, "buffer upload clean")

    vs_src = (b"#version 320 es\nlayout(location=0) in vec3 a_pos;\n"
              b"void main(){gl_Position=vec4(a_pos,1.0);}\n")
    fs_src = (b"#version 320 es\nprecision mediump float;\n"
              b"layout(location=0) out vec4 o;\nvoid main(){o=vec4(1.0);}\n")
    vs = glCreateShader(0x8B31)
    fs = glCreateShader(0x8B30)
    check(vs and fs, "glCreateShader")
    for sh, src in ((vs, vs_src), (fs, fs_src)):
        arr = (ctypes.c_char_p * 1)(src)
        glShaderSource(sh, 1, arr, None)
        glCompileShader(sh)
        st = c_int(0)
        glGetShaderiv(sh, 0x8B81, ctypes.byref(st))
        check(st.value == 1, "compile ok")
    pr = glCreateProgram()
    glAttachShader(pr, vs)
    glAttachShader(pr, fs)
    glLinkProgram(pr)
    lk = c_int(0)
    glGetProgramiv(pr, 0x8B82, ctypes.byref(lk))
    check(lk.value == 1, "link ok")
    glUseProgram(pr)
    vao = ctypes.c_uint(0)
    glGenVertexArrays(1, ctypes.byref(vao))
    glBindVertexArray(vao.value)
    glBindBuffer(0x8892, buf.value)
    glEnableVertexAttribArray(0)
    glVertexAttribPointer(0, 3, 0x1406, 0, 0, None)
    glDrawArrays(0x0004, 0, 3)
    check(glGetError() == 0, "draw clean")
    glDeleteBuffers(1, ctypes.byref(buf))
    check(glGetError() == 0, "final error clean")

    check(eglDestroyContext(dpy, ctx) != 0, "destroy context")
    check(eglDestroySurface(dpy, surf) != 0, "destroy surface")
    check(eglTerminate(dpy) != 0, "terminate")

    if fails or not ok:
        print(f"RESULT: FAIL ({fails}, drive_ok={ok})")
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
