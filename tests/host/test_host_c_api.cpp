// Host C API tests (red first): real OpenGL ES 3.2 C entry points
// (gl*/egl*) dispatching into the TGL managers through HostRuntime.
// EGL setup mirrors a minimal ES 3.2 app; GL setup mirrors the cube trial
// but asserts through the C ABI on a Mock bridge (no GPU needed).
// eglGetProcAddress returns REAL pointers for the implemented subset and
// NULL otherwise (honest dispatch table, not a sentinel).

#include "test_framework.h"

#include <string>

#include "tgles/host/host_c_api.h"
#include "tgles/host/abi.h"
#include "tgles/host/host_runtime.h"
#include "tgles/gpu/metal_bridge.h"

TEST(HostCApi, EglSetupFlow) {
  EGLDisplay dpy = eglGetDisplay(nullptr);
  EXPECT_TRUE(dpy != nullptr);
  EXPECT_TRUE(eglInitialize(dpy, nullptr, nullptr));
  EGLint attribs[] = {0x3024 /*RED_SIZE*/, 8, 0x3038 /*NONE*/};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  EXPECT_TRUE(eglChooseConfig(dpy, attribs, &cfg, 1, &n));
  EXPECT_TRUE(cfg != nullptr && n >= 1);
  EGLint ctx_attribs[] = {0x3098 /*MAJOR*/, 3, 0x30FB /*MINOR*/, 2,
                                 0x3038};
  EGLContext ctx = eglCreateContext(dpy, cfg, nullptr, ctx_attribs);
  EXPECT_TRUE(ctx != nullptr);
  EGLint pb[] = {0x3057 /*WIDTH*/, 16, 0x3056 /*HEIGHT*/, 16,
                        0x3038};
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
  EXPECT_TRUE(surf != nullptr);
  EXPECT_TRUE(eglMakeCurrent(dpy, surf, surf, ctx));
  EXPECT_EQ(eglGetError(), 0x3000);
  EXPECT_TRUE(eglSwapBuffers(dpy, surf));
  EXPECT_TRUE(eglDestroySurface(dpy, surf));
  EXPECT_TRUE(eglTerminate(dpy));
  EXPECT_EQ(eglGetError(), 0x3000);
}

TEST(HostCApi, GlCallsNeedCurrentContext) {
  // Fresh runtime starts with no current context: GL calls fail closed.
  tgles::HostRuntime::Instance().SetCurrent(false);
  GLuint b = 0;
  glGenBuffers(1, &b);
  EXPECT_EQ(glGetError(), 0x0502u);  // INVALID_OPERATION, no context.
  EXPECT_EQ(glGetError(), tgles::kGlNoError);
}

TEST(HostCApi, DrawTriangleThroughMock) {
  EGLDisplay dpy = eglGetDisplay(nullptr);
  eglInitialize(dpy, nullptr, nullptr);
  EGLint attribs[] = {0x3038};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  eglChooseConfig(dpy, attribs, &cfg, 1, &n);
  EGLint ctx_attribs[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext ctx = eglCreateContext(dpy, cfg, nullptr, ctx_attribs);
  EGLint pb[] = {0x3057, 16, 0x3056, 16, 0x3038};
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
  EXPECT_TRUE(eglMakeCurrent(dpy, surf, surf, ctx));

  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  GLuint pb_id = 0, cb_id = 0;
  glGenBuffers(1, &pb_id);
  glGenBuffers(1, &cb_id);
  glBindBuffer(0x8892 /*ARRAY_BUFFER*/, pb_id);
  glBufferData(0x8892, sizeof(kPos), kPos, 0x88E4 /*STATIC_DRAW*/);
  glBindBuffer(0x8892, cb_id);
  glBufferData(0x8892, sizeof(kCol), kCol, 0x88E4);
  GLuint vao = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  // NOTE: a null pointer means offset 0 in the CURRENTLY bound buffer.
  glBindBuffer(0x8892 /*ARRAY_BUFFER*/, pb_id);
  glVertexAttribPointer(0, 3, 0x1406 /*FLOAT*/, 0, 0, nullptr);
  glBindBuffer(0x8892, cb_id);
  glVertexAttribPointer(1, 4, 0x1406, 0, 0, nullptr);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  // Draw target: 16x16 texture on draw-FBO COLOR_ATTACHMENT0 (window size
  // is unknown in the CPU model, so an FBO is required).
  GLuint tex = 0, fbo = 0;
  glGenTextures(1, &tex);
  glBindTexture(0x0DE1 /*TEXTURE_2D*/, tex);
  glTexImage2D(0x0DE1, 0, 0x8058 /*RGBA8*/, 16, 16, 0, 0x1908 /*RGBA*/,
               0x1401 /*UNSIGNED_BYTE*/, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(0x8CA9 /*FRAMEBUFFER*/, fbo);
  glFramebufferTexture2D(0x8CA9, 0x8CE0 /*COLOR_ATTACHMENT0*/, 0x0DE1, tex,
                         0);
  EXPECT_EQ(glGetError(), tgles::kGlNoError);

  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  tgles::HostRuntime::Instance().SetMetalBridge(&bridge);
  glDrawArrays(0x0004 /*TRIANGLES*/, 0, 3);
  EXPECT_EQ(glGetError(), tgles::kGlNoError);
  EXPECT_EQ(bridge.DrawCount(), 1u);
  tgles::HostRuntime::Instance().SetMetalBridge(nullptr);
  EXPECT_TRUE(eglDestroySurface(dpy, surf));
  EXPECT_TRUE(eglTerminate(dpy));
  EXPECT_EQ(eglGetError(), 0x3000);
}

TEST(HostCApi, WindowSwapPresentsPbufferDoesNot) {
  // Launcher path (headless, Mock bridge): window-surface eglSwapBuffers
  // presents through the attached bridge (serial advances); pbuffer swaps
  // stay spec no-ops (serial untouched). No GPU needed: the Mock validates
  // ordering with a fake layer pointer (documented in metal_bridge.h).
  EGLDisplay dpy = eglGetDisplay(nullptr);
  eglInitialize(dpy, nullptr, nullptr);
  EGLint attribs[] = {0x3038};
  EGLConfig cfg = nullptr;
  EGLint n = 0;
  eglChooseConfig(dpy, attribs, &cfg, 1, &n);
  EGLint ctx_attribs[] = {0x3098, 3, 0x30FB, 2, 0x3038};
  EGLContext ctx = eglCreateContext(dpy, cfg, nullptr, ctx_attribs);
  EGLint pb[] = {0x3057, 16, 0x3056, 16, 0x3038};
  EGLSurface pbuf = eglCreatePbufferSurface(dpy, cfg, pb);
  void* fake_window = reinterpret_cast<void*>(0x1234);
  EGLSurface win = eglCreateWindowSurface(dpy, cfg, fake_window, nullptr);
  EXPECT_TRUE(win != nullptr);
  EXPECT_TRUE(eglMakeCurrent(dpy, win, win, ctx));

  static const float kPos[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  static const float kCol[12] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  GLuint pb_id = 0, cb_id = 0;
  glGenBuffers(1, &pb_id);
  glGenBuffers(1, &cb_id);
  glBindBuffer(0x8892, pb_id);
  glBufferData(0x8892, sizeof(kPos), kPos, 0x88E4);
  glBindBuffer(0x8892, cb_id);
  glBufferData(0x8892, sizeof(kCol), kCol, 0x88E4);
  GLuint vao = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glBindBuffer(0x8892, pb_id);
  glVertexAttribPointer(0, 3, 0x1406, 0, 0, nullptr);
  glBindBuffer(0x8892, cb_id);
  glVertexAttribPointer(1, 4, 0x1406, 0, 0, nullptr);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  GLuint tex = 0, fbo = 0;
  glGenTextures(1, &tex);
  glBindTexture(0x0DE1, tex);
  glTexImage2D(0x0DE1, 0, 0x8058, 16, 16, 0, 0x1908, 0x1401, nullptr);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(0x8CA9, fbo);
  glFramebufferTexture2D(0x8CA9, 0x8CE0, 0x0DE1, tex, 0);
  EXPECT_EQ(glGetError(), 0u);

  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple3"));
  int fake_layer = 0;
  bridge.SetLayer(&fake_layer, 16, 16);
  tgles::HostRuntime::Instance().SetMetalBridge(&bridge);
  glDrawArrays(0x0004, 0, 3);
  EXPECT_EQ(glGetError(), 0u);
  // Pbuffer swap: success, no present (serial stays 0).
  EXPECT_TRUE(eglSwapBuffers(dpy, pbuf));
  EXPECT_EQ(bridge.FrameSerial(), 0u);
  // Window swap: success AND present (serial advances to 1).
  EXPECT_TRUE(eglSwapBuffers(dpy, win));
  EXPECT_EQ(bridge.FrameSerial(), 1u);
  EXPECT_EQ(eglGetError(), 0x3000);
  tgles::HostRuntime::Instance().SetMetalBridge(nullptr);
  EXPECT_TRUE(eglDestroySurface(dpy, win));
  EXPECT_TRUE(eglDestroySurface(dpy, pbuf));
  EXPECT_TRUE(eglTerminate(dpy));
}

TEST(HostCApi, ProcAddressIsHonestTable) {
  // Full-contract table (plan-02 Block A): every required name resolves to a
  // real function — either served by a manager or a declared gap that records
  // itself. Unknown and desktop-only names resolve to NULL, never a sentinel.
  EXPECT_TRUE(eglGetProcAddress("glDrawArrays") != nullptr);
  EXPECT_TRUE(eglGetProcAddress("glBufferData") != nullptr);
  EXPECT_TRUE(eglGetProcAddress("glCreateShader") != nullptr);
  EXPECT_TRUE(eglGetProcAddress("eglSwapBuffers") != nullptr);
  EXPECT_TRUE(eglGetProcAddress("glDrawElements") != nullptr);
  EXPECT_TRUE(eglGetProcAddress("glGetString") != nullptr);
  EXPECT_TRUE(eglGetProcAddress("glMultiDrawArraysEXT") == nullptr);
  EXPECT_TRUE(eglGetProcAddress("glPolygonMode") == nullptr);
  EXPECT_TRUE(eglGetProcAddress("glTotallyMadeUp") == nullptr);
  EXPECT_TRUE(eglGetProcAddress(nullptr) == nullptr);
  EXPECT_EQ(eglGetError(), 0x3000);
}
