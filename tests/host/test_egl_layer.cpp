// EGL 1.5 layer tests (TDD — written before src/egl.cpp).
// Every case cites the EGL 1.5 spec section or docs/reference/egl.h value.

#include "test_framework.h"

#include <string>

#include "tgles/egl/egl.h"
#include "tgles/host/abi.h"

namespace {

// PojavLauncher iOS gl_bridge.m attribute set: RGBA8888 + Depth24,
// window+pbuffer surfaces. Any host for a mobile launcher must match it.
tgles::EGLint PojavAttribs[] = {
    tgles::kEglRedSize,   8, tgles::kEglGreenSize, 8,
    tgles::kEglBlueSize,  8, tgles::kEglAlphaSize, 8,
    tgles::kEglDepthSize, 24, tgles::kEglSurfaceType,
    tgles::kEglWindowBit | tgles::kEglPbufferBit, tgles::kEglNone,
};

tgles::EGLint Es32Attribs[] = {
    tgles::kEglContextMajorVersion, 3,
    tgles::kEglContextMinorVersion, 2, tgles::kEglNone,
};

}  // namespace

// --- Error model (spec 3.1) ---

TEST(EglLayer, ErrorLatchAndDoubleGet) {
  tgles::EglState egl;
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
  egl.BindApi(0x1234u);  // Bad API -> BAD_PARAMETER (spec 3.7).
  EXPECT_EQ(egl.GetError(), tgles::kEglBadParameter);
  // Second call reports the success of the first GetError.
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

TEST(EglLayer, ErrorCodesMatchHeader) {
  EXPECT_EQ(tgles::kEglSuccess, 0x3000);
  EXPECT_EQ(tgles::kEglBadDisplay, 0x3008);
  EXPECT_EQ(tgles::kEglBadMatch, 0x3009);
  EXPECT_EQ(tgles::kEglBadParameter, 0x300Cu);
  EXPECT_EQ(tgles::kEglNotInitialized, 0x3001);
  EXPECT_EQ(tgles::kEglContextLost, 0x300E);
}

// --- Displays (spec 3.2) ---

TEST(EglLayer, DisplayInitTerminate) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);  // Default display.
  EXPECT_NE(dpy, tgles::kEglNoDisplay);
  EXPECT_TRUE(egl.Initialize(dpy, nullptr, nullptr));
  tgles::EGLint major = 0, minor = 0;
  EXPECT_TRUE(egl.Initialize(dpy, &major, &minor));
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 5);  // EGL 1.5.
  EXPECT_TRUE(egl.Terminate(dpy));
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
  EXPECT_FALSE(egl.Initialize(tgles::kEglNoDisplay, nullptr, nullptr));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadDisplay);
}

TEST(EglLayer, QueryStringSet) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  const char* ver = egl.QueryString(dpy, tgles::kEglVersion);
  EXPECT_TRUE(ver != nullptr);
  EXPECT_TRUE(std::string(ver).find("1.5") == 0);  // "1.5 ..."
  EXPECT_TRUE(egl.QueryString(dpy, tgles::kEglVendor) != nullptr);
  const char* apis = egl.QueryString(dpy, tgles::kEglClientApis);
  EXPECT_TRUE(std::string(apis).find("OpenGL_ES") != std::string::npos);
  EXPECT_TRUE(egl.QueryString(dpy, 0x1234) == nullptr);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadParameter);
}

// --- Client API binding (spec 3.7: initial is ES, others BAD_PARAMETER) ---

TEST(EglLayer, InitialApiIsEs) {
  tgles::EglState egl;
  EXPECT_EQ(egl.QueryApi(), tgles::kEglOpenGlEsApi);
  EXPECT_TRUE(egl.BindApi(tgles::kEglOpenGlEsApi));
  // TGL is a GLES host: desktop GL and VG are not supported.
  EXPECT_FALSE(egl.BindApi(tgles::kEglOpenGlApi));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadParameter);
  EXPECT_FALSE(egl.BindApi(tgles::kEglOpenVgApi));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadParameter);
  // Failed binds change nothing.
  EXPECT_EQ(egl.QueryApi(), tgles::kEglOpenGlEsApi);
}

// --- Configs (spec 3.4) ---

TEST(EglLayer, ChooseLauncherConfig) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  EXPECT_TRUE(
      egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n));
  EXPECT_TRUE(n >= 1);
  EXPECT_NE(cfg, 0);
  tgles::EGLint red = 0, depth = 0, samples = 0;
  EXPECT_TRUE(egl.GetConfigAttrib(dpy, cfg, tgles::kEglRedSize, &red));
  EXPECT_TRUE(egl.GetConfigAttrib(dpy, cfg, tgles::kEglDepthSize, &depth));
  EXPECT_TRUE(egl.GetConfigAttrib(dpy, cfg, tgles::kEglSamples, &samples));
  EXPECT_EQ(red, 8);
  EXPECT_TRUE(depth >= 24);
  // Count-only query.
  tgles::EGLint total = 0;
  EXPECT_TRUE(egl.ChooseConfig(dpy, PojavAttribs, nullptr, 0, &total));
  EXPECT_TRUE(total >= n);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

TEST(EglLayer, ChooseConfigValidation) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLint bad[] = {0x1234, 1, tgles::kEglNone};
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  EXPECT_FALSE(egl.ChooseConfig(dpy, bad, &cfg, 1, &n));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadAttribute);
  // Uninitialized display.
  tgles::EGLDisplay dpy2 = egl.GetDisplay(reinterpret_cast<void*>(0x77));
  EXPECT_FALSE(egl.ChooseConfig(dpy2, PojavAttribs, &cfg, 1, &n));
  EXPECT_EQ(egl.GetError(), tgles::kEglNotInitialized);
}

// --- Contexts (spec 3.7.1) ---

TEST(EglLayer, CreateEs32Context) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  tgles::EGLContext ctx =
      egl.CreateContext(dpy, cfg, tgles::kEglNoContext, Es32Attribs);
  EXPECT_NE(ctx, tgles::kEglNoContext);
  tgles::EGLint version = 0;
  EXPECT_TRUE(egl.QueryContext(dpy, ctx,
                               tgles::kEglContextClientVersion, &version));
  EXPECT_EQ(version, 3);  // CLIENT_VERSION is an alias of MAJOR (spec fn.18).
  EXPECT_TRUE(egl.DestroyContext(dpy, ctx));
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

TEST(EglLayer, CreateContextBackCompatAndMismatch) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  // ES 2.0 request is satisfied by our backwards-compatible 3.2 context.
  tgles::EGLint es2[] = {tgles::kEglContextMajorVersion, 2,
                         tgles::kEglNone};
  tgles::EGLContext c2 =
      egl.CreateContext(dpy, cfg, tgles::kEglNoContext, es2);
  EXPECT_NE(c2, tgles::kEglNoContext);
  // Undefined ES version -> BAD_MATCH.
  tgles::EGLint es40[] = {tgles::kEglContextMajorVersion, 4,
                          tgles::kEglNone};
  EXPECT_EQ(egl.CreateContext(dpy, cfg, tgles::kEglNoContext, es40),
            tgles::kEglNoContext);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadMatch);
  // Bad config / bad share / unknown attrib.
  EXPECT_EQ(
      egl.CreateContext(dpy, 9999, tgles::kEglNoContext, Es32Attribs),
      tgles::kEglNoContext);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadConfig);
  EXPECT_EQ(egl.CreateContext(dpy, cfg, 9999, Es32Attribs),
            tgles::kEglNoContext);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadContext);
  tgles::EGLint badattr[] = {0x1234, 1, tgles::kEglNone};
  EXPECT_EQ(egl.CreateContext(dpy, cfg, tgles::kEglNoContext, badattr),
            tgles::kEglNoContext);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadAttribute);
  // NULL attribs mean ES 1.0 (spec 3.7.1 defaults); TGL has no 1.x contexts.
  EXPECT_EQ(egl.CreateContext(dpy, cfg, tgles::kEglNoContext, nullptr),
            tgles::kEglNoContext);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadMatch);
}

// --- Surfaces (spec 3.5) ---

TEST(EglLayer, PbufferSurface) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  tgles::EGLint pb[] = {tgles::kEglWidth, 64, tgles::kEglHeight, 64,
                        tgles::kEglNone};
  tgles::EGLSurface surf = egl.CreatePbufferSurface(dpy, cfg, pb);
  EXPECT_NE(surf, tgles::kEglNoSurface);
  tgles::EGLint w = 0, h = 0;
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglWidth, &w));
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglHeight, &h));
  EXPECT_EQ(w, 64);
  EXPECT_EQ(h, 64);
  EXPECT_TRUE(egl.DestroySurface(dpy, surf));
  tgles::EGLint zero[] = {tgles::kEglWidth, 0, tgles::kEglHeight, 64,
                          tgles::kEglNone};
  EXPECT_EQ(egl.CreatePbufferSurface(dpy, cfg, zero), tgles::kEglNoSurface);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadMatch);  // Zero-area drawable.
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

TEST(EglLayer, WindowSurfaceNativeCheck) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  // Null native window is invalid (spec BAD_NATIVE_WINDOW).
  EXPECT_EQ(egl.CreateWindowSurface(dpy, cfg, nullptr, nullptr),
            tgles::kEglNoSurface);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadNativeWindow);
  int fake_window = 1;
  tgles::EGLSurface surf =
      egl.CreateWindowSurface(dpy, cfg, &fake_window, nullptr);
  EXPECT_NE(surf, tgles::kEglNoSurface);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

TEST(EglLayer, WindowSurfaceSizeFollowsHostAttach) {
  // Regression for launcher logs showing "egl surface 0x0": window surfaces
  // carry no WxH at creation; the Metal attach/resize path mirrors the
  // drawable size so QuerySurface stays truthful for host readback probes.
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  int fake_window = 1;
  tgles::EGLSurface surf =
      egl.CreateWindowSurface(dpy, cfg, &fake_window, nullptr);
  EXPECT_NE(surf, tgles::kEglNoSurface);
  tgles::EGLint w = -1, h = -1;
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglWidth, &w));
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglHeight, &h));
  EXPECT_EQ(w, 0);
  EXPECT_EQ(h, 0);
  egl.SetWindowSurfaceSize(2208, 1242);
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglWidth, &w));
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglHeight, &h));
  EXPECT_EQ(w, 2208);
  EXPECT_EQ(h, 1242);
  // Non-positive sizes are ignored, never clobber the last good size.
  egl.SetWindowSurfaceSize(0, 0);
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglWidth, &w));
  EXPECT_EQ(w, 2208);
}

TEST(EglLayer, WindowSurfaceCreatedAfterAttachInheritsSize) {
  // Hosts that tglHostAttachMetalLayer BEFORE eglCreateWindowSurface must
  // not report 0x0 on the later surface (readback/skip gap in the launcher).
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  egl.SetWindowSurfaceSize(1280, 720);
  int fake_window = 1;
  tgles::EGLSurface surf =
      egl.CreateWindowSurface(dpy, cfg, &fake_window, nullptr);
  EXPECT_NE(surf, tgles::kEglNoSurface);
  tgles::EGLint w = 0, h = 0;
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglWidth, &w));
  EXPECT_TRUE(egl.QuerySurface(dpy, surf, tgles::kEglHeight, &h));
  EXPECT_EQ(w, 1280);
  EXPECT_EQ(h, 720);
}

// --- MakeCurrent (spec 3.7.3 error matrix) ---

TEST(EglLayer, MakeCurrentMatrix) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  tgles::EGLContext ctx =
      egl.CreateContext(dpy, cfg, tgles::kEglNoContext, Es32Attribs);
  tgles::EGLint pb[] = {tgles::kEglWidth, 32, tgles::kEglHeight, 32,
                        tgles::kEglNone};
  tgles::EGLSurface surf = egl.CreatePbufferSurface(dpy, cfg, pb);
  EXPECT_TRUE(egl.MakeCurrent(dpy, surf, surf, ctx));
  EXPECT_EQ(egl.GetCurrentContext(), ctx);
  EXPECT_EQ(egl.GetCurrentDisplay(), dpy);
  // surface + NO_CONTEXT -> BAD_MATCH.
  EXPECT_FALSE(egl.MakeCurrent(dpy, surf, surf, tgles::kEglNoContext));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadMatch);
  // draw valid + read NO_SURFACE -> BAD_MATCH.
  EXPECT_FALSE(egl.MakeCurrent(dpy, surf, tgles::kEglNoSurface, ctx));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadMatch);
  // Bad context / bad surface.
  EXPECT_FALSE(egl.MakeCurrent(dpy, surf, surf, 9999));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadContext);
  EXPECT_FALSE(egl.MakeCurrent(dpy, 9999, 9999, ctx));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadSurface);
  // Full unbind works.
  EXPECT_TRUE(egl.MakeCurrent(dpy, tgles::kEglNoSurface, tgles::kEglNoSurface,
                              tgles::kEglNoContext));
  EXPECT_EQ(egl.GetCurrentContext(), tgles::kEglNoContext);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

// --- Swap + interval (spec 3.9-3.10) ---

TEST(EglLayer, SwapBuffersSemantics) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, PojavAttribs, &cfg, 1, &n);
  tgles::EGLint pb[] = {tgles::kEglWidth, 16, tgles::kEglHeight, 16,
                        tgles::kEglNone};
  tgles::EGLSurface psurf = egl.CreatePbufferSurface(dpy, cfg, pb);
  // Pbuffer swap is a spec no-op but still succeeds.
  EXPECT_TRUE(egl.SwapBuffers(dpy, psurf));
  EXPECT_EQ(egl.SwapCount(psurf), 0);
  int fake_window = 2;
  tgles::EGLSurface wsurf =
      egl.CreateWindowSurface(dpy, cfg, &fake_window, nullptr);
  EXPECT_TRUE(egl.SwapBuffers(dpy, wsurf));
  EXPECT_EQ(egl.SwapCount(wsurf), 1);
  EXPECT_FALSE(egl.SwapBuffers(dpy, 9999));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadSurface);
  // Interval is silently clamped into [0, 4].
  EXPECT_TRUE(egl.SwapInterval(dpy, 99));
  EXPECT_EQ(egl.SwapIntervalValue(), 4);
  EXPECT_TRUE(egl.SwapInterval(dpy, 0));
  EXPECT_EQ(egl.SwapIntervalValue(), 0);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

// --- Proc addresses (spec: NULL = missing, no error) ---
//
// EglState no longer serves proc addresses itself: plan-02 Block A moved the
// single name -> address table to src/host/abi/dispatch.cpp so dlsym and
// eglGetProcAddress can never diverge (the old EglState method returned
// (void*)0x1 sentinels and was deleted). This test pins the real table.

TEST(EglLayer, GetProcAddressMap) {
  EXPECT_TRUE(::eglGetProcAddress("eglGetError") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("eglSwapBuffers") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glDrawArrays") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glDispatchCompute") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glFenceSync") != nullptr);
  // Full MobileGL loader surface: 46 EGL + required EXT.
  EXPECT_TRUE(::eglGetProcAddress("eglGetConfigs") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("eglGetCurrentSurface") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("eglSurfaceAttrib") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("eglWaitClient") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("eglReleaseThread") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("eglCreateImage") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("eglDestroyImage") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glBufferStorageEXT") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glBindImageTexture") != nullptr);
  // Served EXT (real bodies in gl_real.cpp) resolve non-NULL.
  EXPECT_TRUE(::eglGetProcAddress("glTextureViewEXT") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glTextureViewOES") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("no_such_function_xyz") == nullptr);
  EXPECT_TRUE(::eglGetProcAddress(nullptr) == nullptr);
  // Unknown names never raise errors (spec 3.12).
  tgles::EglState egl;
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

// --- Platform extras + images (EGL 1.5 core, all REQUIRED by loader) ---

TEST(EglLayer, PlatformExtrasAndImages) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLint n = 0;
  EXPECT_TRUE(egl.GetConfigs(dpy, nullptr, 0, &n));
  EXPECT_TRUE(n >= 1);
  tgles::EGLint attribs[] = {tgles::kEglRedSize, 8, tgles::kEglNone};
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n2 = 0;
  egl.ChooseConfig(dpy, attribs, &cfg, 1, &n2);
  tgles::EGLint v32[] = {tgles::kEglContextMajorVersion, 3,
                         tgles::kEglContextMinorVersion, 2, tgles::kEglNone};
  tgles::EGLContext ctx = egl.CreateContext(dpy, cfg, 0, v32);
  tgles::EGLint pb[] = {tgles::kEglWidth, 16, tgles::kEglHeight, 16,
                        tgles::kEglNone};
  tgles::EGLSurface surf = egl.CreatePbufferSurface(dpy, cfg, pb);
  EXPECT_TRUE(egl.MakeCurrent(dpy, surf, surf, ctx));
  EXPECT_EQ(egl.GetCurrentSurface(tgles::kEglDraw), surf);
  EXPECT_EQ(egl.GetCurrentSurface(tgles::kEglRead), surf);
  EXPECT_EQ(egl.GetCurrentSurface(0x1234), tgles::kEglNoSurface);
  EXPECT_EQ(egl.GetError(), tgles::kEglBadParameter);
  EXPECT_TRUE(egl.SurfaceAttrib(dpy, surf, tgles::kEglSwapBehavior, 0));
  EXPECT_FALSE(egl.SurfaceAttrib(dpy, surf, 0x1234, 0));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadAttribute);
  EXPECT_TRUE(egl.WaitClient());
  EXPECT_TRUE(egl.WaitGL());
  EXPECT_TRUE(egl.WaitNative(0));
  tgles::EGLImage img =
      egl.CreateImage(dpy, ctx, tgles::kEglGlTexture2D, nullptr, nullptr);
  EXPECT_NE(img, tgles::kEglNoImage);
  EXPECT_TRUE(egl.DestroyImage(dpy, img));
  EXPECT_TRUE(egl.ReleaseThread());
  EXPECT_EQ(egl.GetCurrentContext(), tgles::kEglNoContext);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}
