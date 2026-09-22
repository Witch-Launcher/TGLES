// TDD gap tests: MobileGL host + ES 3.2 conformance for the 7 Missing /
// 2 Partial gates. Written BEFORE implementation (red first), citing:
// - MobileGL dev source (/tmp/mobilegl): Loader.h/.cpp (46 EGL + 385 GLES,
//   AcquireGLESFunctions via eglGetProcAddress, INIT_EGL_FUNC fatal,
//   INIT_GLES_FUNC required incl. glBufferStorageEXT + glBindImageTexture,
//   OPTIONAL TextureView/MultiDraw/BaseInstance), BackendObject_DirectGLES
//   (InitCapabilities, SupportsTextureView gate, CAMetalLayer window check),
//   DirectGLES.h (CallAndCheck, AreTimerQueriesSupported, CallTexBufferRange),
//   Managers.cpp (TextureView 3594, SSBO 6100/8229 "no real ES driver exposes",
//   persistent rings 986/1793 via glBufferStorageEXT+Map+PERSISTENT/COHERENT),
//   EGLState/Core (CreateImage/DestroyImage logical objects).
// - Khronos: gl32.h (358 core; BindImageTexture/GetProgramResource/
//   UniformBlockBinding core; TextureView/BufferStorage/PERSISTENT only EXT),
//   egl.h (eglCreateImage/DestroyImage, GetConfigs, GetCurrentSurface,
//   SurfaceAttrib, WaitClient/GL/Native, ReleaseThread all core), EGL 1.5 spec.
#include "test_framework.h"

#include <string>

#include "tgles/state/buffer.h"
#include "tgles/egl/egl.h"
#include "tgles/host/abi.h"
#include "tgles/facade/gles.h"
#include "tgles/state/image_units.h"
#include "tgles/support/mobilegl_support.h"
#include "tgles/state/program.h"
#include "tgles/state/shader.h"
#include "tgles/state/texture.h"

// --- EGL platform extras (MobileGL loader: all 46 EGL REQUIRED, INIT_EGL_FUNC
// fatal if NULL; TGL EglState lacks implementations but advertises names) ---
TEST(GapEgl, GetConfigsEnumerates) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLint n = 0;
  EXPECT_TRUE(egl.GetConfigs(dpy, nullptr, 0, &n));
  EXPECT_TRUE(n >= 1);
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n2 = 0;
  EXPECT_TRUE(egl.GetConfigs(dpy, &cfg, 1, &n2));
  EXPECT_TRUE(n2 >= 1);
  EXPECT_NE(cfg, 0u);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

TEST(GapEgl, GetCurrentSurfaceTracksDrawRead) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLint attribs[] = {tgles::kEglRedSize, 8, tgles::kEglNone};
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, attribs, &cfg, 1, &n);
  tgles::EGLint v32[] = {tgles::kEglContextMajorVersion, 3,
                         tgles::kEglContextMinorVersion, 2, tgles::kEglNone};
  tgles::EGLContext ctx = egl.CreateContext(dpy, cfg, 0, v32);
  tgles::EGLint pb[] = {tgles::kEglWidth, 16, tgles::kEglHeight, 16,
                        tgles::kEglNone};
  tgles::EGLSurface surf = egl.CreatePbufferSurface(dpy, cfg, pb);
  EXPECT_TRUE(egl.MakeCurrent(dpy, surf, surf, ctx));
  EXPECT_EQ(egl.GetCurrentSurface(0x3059 /*EGL_DRAW*/), surf);
  EXPECT_EQ(egl.GetCurrentSurface(0x305A /*EGL_READ*/), surf);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

TEST(GapEgl, SurfaceAttribAndWaitAndReleaseThread) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLint attribs[] = {tgles::kEglRedSize, 8, tgles::kEglNone};
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, attribs, &cfg, 1, &n);
  tgles::EGLint pb[] = {tgles::kEglWidth, 8, tgles::kEglHeight, 8,
                        tgles::kEglNone};
  tgles::EGLSurface surf = egl.CreatePbufferSurface(dpy, cfg, pb);
  // Known attrib accepted (swap-behavior), unknown rejected with BAD_ATTRIBUTE.
  EXPECT_TRUE(egl.SurfaceAttrib(dpy, surf, 0x3093 /*EGL_SWAP_BEHAVIOR*/, 0));
  EXPECT_FALSE(egl.SurfaceAttrib(dpy, surf, 0x1234, 0));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadAttribute);
  EXPECT_TRUE(egl.WaitClient());
  EXPECT_TRUE(egl.WaitGL());
  EXPECT_TRUE(egl.WaitNative(0));
  EXPECT_TRUE(egl.ReleaseThread());
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

// --- EGL images (loader REQUIRED eglCreateImage/DestroyImage; EGLState logical
// objects in MobileGL MG_State/EGLState/Core.cpp) ---
TEST(GapEgl, ImageCreateDestroyRoundTrip) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLint attribs[] = {tgles::kEglRedSize, 8, tgles::kEglNone};
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  egl.ChooseConfig(dpy, attribs, &cfg, 1, &n);
  tgles::EGLint v32[] = {tgles::kEglContextMajorVersion, 3,
                         tgles::kEglContextMinorVersion, 2, tgles::kEglNone};
  tgles::EGLContext ctx = egl.CreateContext(dpy, cfg, 0, v32);
  tgles::EGLImage img = egl.CreateImage(dpy, ctx, 0x30B1 /*GL_TEXTURE_2D*/,
                                        nullptr, nullptr);
  EXPECT_NE(img, tgles::kEglNoImage);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
  EXPECT_TRUE(egl.DestroyImage(dpy, img));
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
  EXPECT_FALSE(egl.DestroyImage(dpy, 9999u));
  EXPECT_EQ(egl.GetError(), tgles::kEglBadParameter);
}

// --- Proc addresses must cover the full MobileGL loader surface ---
// (Single table in dispatch.cpp since plan-02 Block A; the old EglState
// sentinel method was deleted. MobileGL resolves through eglGetProcAddress.)
TEST(GapProc, Egl46AndBufferStorageExtResolvable) {
  tgles::EglState egl;
  static const char* const kRequiredEgl[] = {
      "eglGetConfigs",        "eglGetCurrentSurface", "eglSurfaceAttrib",
      "eglWaitClient",        "eglWaitGL",            "eglWaitNative",
      "eglReleaseThread",     "eglCreateImage",       "eglDestroyImage",
      "eglCreateSync",        "eglDestroySync",       "eglClientWaitSync",
      "eglGetSyncAttrib",     "eglWaitSync",          "eglBindAPI",
      "eglChooseConfig",      "eglCreateContext",     "eglMakeCurrent",
      "eglSwapBuffers",       "eglGetProcAddress",    "eglGetDisplay",
      "eglInitialize",
  };
  for (const char* name : kRequiredEgl) {
    EXPECT_TRUE(::eglGetProcAddress(name) != nullptr);
  }
  // glBufferStorageEXT is REQUIRED (INIT_GLES_FUNC, persistent rings
  // Managers.cpp:986/1793). Served EXT spellings (real gl_real.cpp bodies)
  // resolve non-NULL; unserved ones stay NULL (capability-gated).
  EXPECT_TRUE(::eglGetProcAddress("glBufferStorageEXT") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glBindImageTexture") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glTexBuffer") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glTexBufferRange") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glTextureViewEXT") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glMultiDrawElementsBaseVertexEXT") !=
              nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glMultiDrawArraysEXT") == nullptr);
  EXPECT_EQ(egl.GetError(), tgles::kEglSuccess);
}

// --- Persistent mapping: EXT_buffer_storage is the only way DirectGLES can do
// persistent coherent maps (Managers.cpp:986 AcquirePersistentMap,
// 1793 CreateRingStorage; MOBILEGL_COHERENT_AS_FLUSH compat flag) ---
TEST(GapBuffer, StorageExtImmutableAndPersistentMap) {
  tgles::BufferManager bufs;
  tgles::GLuint b = 0;
  bufs.GenBuffers(1, &b);
  bufs.BindBuffer(tgles::kGlArrayBuffer, b);
  bufs.BufferStorageEXT(tgles::kGlArrayBuffer, 64, nullptr,
                        tgles::kGlMapWriteBit | tgles::kGlMapPersistentBit |
                            tgles::kGlMapCoherentBit);
  EXPECT_EQ(bufs.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(bufs.IsImmutable(b));
  // Immutable store cannot be re-specified via BufferData.
  bufs.BufferData(tgles::kGlArrayBuffer, 64, nullptr, tgles::kGlDynamicDraw);
  EXPECT_EQ(bufs.GetError(), tgles::kGlInvalidOperation);
  void* p = bufs.MapBufferRange(
      tgles::kGlArrayBuffer, 0, 64,
      tgles::kGlMapWriteBit | tgles::kGlMapPersistentBit |
          tgles::kGlMapCoherentBit);
  EXPECT_TRUE(p != nullptr);
  // Flush while still mapped (explicit-flush compat, MOBILEGL_COHERENT_AS_FLUSH).
  bufs.FlushMappedBufferRange(tgles::kGlArrayBuffer, 0, 64);
  EXPECT_EQ(bufs.GetError(), tgles::kGlNoError);
  EXPECT_EQ(bufs.UnmapBuffer(tgles::kGlArrayBuffer), tgles::kGlTrue);
  // Flush after unmap must fail (spec: not mapped -> INVALID_OPERATION).
  bufs.FlushMappedBufferRange(tgles::kGlArrayBuffer, 0, 64);
  EXPECT_EQ(bufs.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(bufs.GetError(), tgles::kGlNoError);
}

// --- Image units: glBindImageTexture is CORE ES 3.1+ (gl32.h) and REQUIRED
// (INIT_GLES_FUNC); DirectGLES issues it per unit (DirectGLES.cpp:1706/1772) ---
TEST(GapImage, BindImageTextureUnits) {
  tgles::ImageUnitManager imgs;
  EXPECT_EQ(imgs.MaxUnits(), 8);
  imgs.BindImageTexture(0, 1, 0, tgles::kGlFalse, 0, 0x88B8 /*READ_ONLY*/,
                        tgles::kGlRgba8);
  EXPECT_EQ(imgs.GetError(), tgles::kGlNoError);
  EXPECT_EQ(imgs.BoundTexture(0), 1u);
  imgs.BindImageTexture(8, 1, 0, tgles::kGlFalse, 0, 0x88B8, tgles::kGlRgba8);
  EXPECT_EQ(imgs.GetError(), tgles::kGlInvalidValue);
  imgs.BindImageTexture(0, 0, 0, tgles::kGlFalse, 0, 0x88B8, tgles::kGlRgba8);
  EXPECT_EQ(imgs.GetError(), tgles::kGlNoError);
  EXPECT_EQ(imgs.BoundTexture(0), 0u);
}

// --- SSBO + UBO reflection is CORE (GetProgramResource/UniformBlockBinding in
// gl32.h); glShaderStorageBlockBinding itself is desktop-only ("no real ES
// driver exposes", Managers.cpp:8229) so the host gate for it is NotRequired
// and the real host work is program reflection ---
TEST(GapProgram, UniformBlockBindingAndSsboReflection) {
  tgles::ShaderManager sm;
  tgles::ProgramManager pm(&sm);
  const char* vs = "#version 320 es\nvoid main() {}";
  const char* fs = "#version 320 es\nvoid main() {}";
  tgles::GLuint v = sm.CreateShader(tgles::kGlVertexShader);
  sm.ShaderSource(v, 1, &vs, nullptr);
  sm.CompileShader(v);
  tgles::GLuint f = sm.CreateShader(tgles::kGlFragmentShader);
  sm.ShaderSource(f, 1, &fs, nullptr);
  sm.CompileShader(f);
  tgles::GLuint prog = pm.CreateProgram();
  pm.AttachShader(prog, v);
  pm.AttachShader(prog, f);
  pm.LinkProgram(prog);
  EXPECT_TRUE(pm.LinkSucceeded(prog));
  pm.UniformBlockBinding(prog, 0, 1);
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
  tgles::GLuint idx = pm.GetProgramResourceIndex(
      prog, tgles::kGlShaderStorageBlock, "SsboBlock");
  (void)idx;
  EXPECT_EQ(pm.GetError(), tgles::kGlNoError);
}
