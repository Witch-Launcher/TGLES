#!/usr/bin/env python3
"""CTS preflight: the F5 surfaceless sequence against the built libtgles.

Why ctypes and not the C++ test: this runs without rebuilding anything, so a
CTS farmer can check prerequisites (library present, F5 order works, server
strings sane, no sentinels) before spending an hour building VK-GL-CTS.
It mirrors framework/platform/surfaceless/tcuSurfacelessPlatform.cpp +
framework/egl/egluGLContextFactory.cpp (see plan/plan-02-cts-es32.md F5).

Run:   python3 tools/cts/preflight.py --lib build/libtgles.dylib
Check: exit 0 == CTS prerequisites met; anything else prints the failing step.
"""

from __future__ import annotations

import argparse
import ctypes
import sys

# EGL 1.5 tokens (docs/reference/egl.h). Only the F5 subset is needed here.
EGL_SUCCESS = 0x3000
EGL_NONE = 0x3038
EGL_RED_SIZE = 0x3024
EGL_DEPTH_SIZE = 0x3025
EGL_STENCIL_SIZE = 0x3026
EGL_SURFACE_TYPE = 0x3033
EGL_RENDERABLE_TYPE = 0x3040
EGL_PBUFFER_BIT = 0x0001
EGL_OPENGL_ES3_BIT = 0x40
EGL_WIDTH = 0x3057
EGL_HEIGHT = 0x3056
EGL_CONTEXT_MAJOR_VERSION = 0x3098
EGL_CONTEXT_MINOR_VERSION = 0x30FB
EGL_OPENGL_ES_API = 0x30A0
EGL_VENDOR = 0x3053
EGL_VERSION = 0x3054
EGL_EXTENSIONS = 0x3055
GL_VERSION = 0x1F02

FAILURES: list[str] = []


def check(cond: bool, label: str) -> bool:
    print(("PASS " if cond else "FAIL ") + label)
    if not cond:
        FAILURES.append(label)
    return cond


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lib", default="build/libtgles.dylib")
    args = parser.parse_args()

    try:
        lib = ctypes.CDLL(args.lib)
    except OSError as exc:
        print(f"FAIL dlopen({args.lib}): {exc}")
        print("hint: cmake -S . -B build && cmake --build build --target tgles_host")
        return 1

    lib.eglGetDisplay.restype = ctypes.c_void_p
    lib.eglGetDisplay.argtypes = [ctypes.c_void_p]
    lib.eglInitialize.restype = ctypes.c_uint
    lib.eglInitialize.argtypes = [ctypes.c_void_p,
                                  ctypes.POINTER(ctypes.c_int),
                                  ctypes.POINTER(ctypes.c_int)]
    lib.eglGetError.restype = ctypes.c_int
    lib.eglGetError.argtypes = []
    lib.eglGetProcAddress.restype = ctypes.c_void_p
    lib.eglGetProcAddress.argtypes = [ctypes.c_char_p]

    display = lib.eglGetDisplay(None)
    if not check(display not in (None, 0), "eglGetDisplay(NULL)"):
        return 1

    major, minor = ctypes.c_int(0), ctypes.c_int(0)
    check(lib.eglInitialize(display, ctypes.byref(major),
                            ctypes.byref(minor)) == 1, "eglInitialize")
    check(lib.eglGetError() == EGL_SUCCESS, "eglGetError == SUCCESS")

    # eglChooseConfig twice: first count, then fetch (CTS does both).
    attribs = (ctypes.c_int * 13)(
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_NONE)
    num = ctypes.c_int(0)
    lib.eglChooseConfig.restype = ctypes.c_uint
    lib.eglChooseConfig.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int),
                                    ctypes.c_void_p, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_int)]
    check(lib.eglChooseConfig(display, attribs, None, 0,
                              ctypes.byref(num)) == 1, "eglChooseConfig(count)")
    check(num.value >= 1, f"config count >= 1 (got {num.value})")
    config = ctypes.c_void_p(0)
    check(lib.eglChooseConfig(display, attribs, ctypes.byref(config), 1,
                              ctypes.byref(num)) == 1, "eglChooseConfig(fetch)")

    lib.eglGetConfigAttrib.restype = ctypes.c_uint
    lib.eglGetConfigAttrib.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_int,
                                       ctypes.POINTER(ctypes.c_int)]
    for attr, name, want in ((EGL_RED_SIZE, "RED_SIZE", 8),
                             (EGL_DEPTH_SIZE, "DEPTH_SIZE", 24),
                             (EGL_STENCIL_SIZE, "STENCIL_SIZE", 8)):
        value = ctypes.c_int(0)
        ok = lib.eglGetConfigAttrib(display, config, attr,
                                    ctypes.byref(value)) == 1
        check(ok and value.value >= want, f"eglGetConfigAttrib({name})")

    lib.eglBindAPI.restype = ctypes.c_uint
    lib.eglBindAPI.argtypes = [ctypes.c_uint]
    check(lib.eglBindAPI(EGL_OPENGL_ES_API) == 1, "eglBindAPI(OPENGL_ES)")

    ctx_attribs = (ctypes.c_int * 5)(
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE)
    lib.eglCreateContext.restype = ctypes.c_void_p
    lib.eglCreateContext.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                     ctypes.c_void_p,
                                     ctypes.POINTER(ctypes.c_int)]
    context = lib.eglCreateContext(display, config, None, ctx_attribs)
    check(context not in (None, 0), "eglCreateContext(3.2)")

    pb_attribs = (ctypes.c_int * 5)(EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE)
    lib.eglCreatePbufferSurface.restype = ctypes.c_void_p
    lib.eglCreatePbufferSurface.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                            ctypes.POINTER(ctypes.c_int)]
    surface = lib.eglCreatePbufferSurface(display, config, pb_attribs)
    check(surface not in (None, 0), "eglCreatePbufferSurface(64x64)")

    lib.eglMakeCurrent.restype = ctypes.c_uint
    lib.eglMakeCurrent.argtypes = [ctypes.c_void_p] * 4
    check(lib.eglMakeCurrent(display, surface, surface, context) == 1,
          "eglMakeCurrent")

    # Server strings through the real ABI (glReal: GetString).
    get_string = lib.eglGetProcAddress(b"glGetString")
    check(get_string not in (None, 0, 1, 2), "eglGetProcAddress(glGetString)")
    if get_string not in (None, 0):
        proto = ctypes.CFUNCTYPE(ctypes.c_char_p, ctypes.c_uint)(get_string)
        version = proto(GL_VERSION)
        text = version.decode() if version else ""
        check("OpenGL ES 3.2" in text, f'glGetString(VERSION) = "{text}"')

    # No sentinels on the required names (spot-check the historically deadly).
    for name in (b"glDrawArrays", b"glDrawElements", b"glGetString",
                 b"eglSwapBuffers", b"eglCreateImage"):
        addr = lib.eglGetProcAddress(name)
        check(addr not in (None, 0, 1, 2),
              f"procaddr real: {name.decode()}")
    check(lib.eglGetProcAddress(b"glPolygonMode") in (None, 0),
          "desktop glPolygonMode is NULL (not a GLES host)")
    check(lib.eglGetProcAddress(b"glTotallyMadeUp") in (None, 0),
          "unknown name is NULL")

    # Gap ledger: informative, not pass/fail (the ceiling lives in the C++
    # contract test). Print it so farmers see the number.
    try:
        lib.tglesAbiGapCount.restype = ctypes.c_uint
        lib.tglesAbiGapCount.argtypes = []
        print(f"INFO declared gaps: {lib.tglesAbiGapCount()}")
        lib.tglesAbiVersion.restype = ctypes.c_char_p
        lib.tglesAbiVersion.argtypes = []
        print(f"INFO version: {lib.tglesAbiVersion().decode()}")
    except AttributeError:
        print("INFO no gap ledger (older library?)")

    if FAILURES:
        print(f"\n{len(FAILURES)} preflight checks failed.")
        return 1
    print("\npreflight PASS: CTS surfaceless prerequisites met.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
