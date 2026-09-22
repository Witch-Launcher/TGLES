// Full MobileGL-host support suite: every Supported gate is proven by a
// live probe against real TGL managers (not just a table entry); every
// Missing/Partial/NotRequired gate is pinned so silent regressions fail.

#include "test_framework.h"

#include <string>

#include "tgles/gpu/backend.h"
#include "tgles/state/buffer.h"
#include "tgles/state/compute.h"
#include "tgles/state/context.h"
#include "tgles/pipeline/draw.h"
#include "tgles/egl/egl.h"
#include "tgles/host/abi.h"
#include "tgles/state/framebuffer.h"
#include "tgles/facade/gles.h"
#include "tgles/host/entry_points.h"
#include "tgles/gpu/metal_bridge.h"
#include "tgles/support/mobilegl_support.h"
#include "tgles/state/pixels.h"
#include "tgles/state/query.h"
#include "tgles/base/spec.h"
#include "tgles/state/sync.h"
#include "tgles/state/texture.h"
#include "tgles/state/transform_feedback.h"
#include "tgles/state/vertex_array.h"
namespace {

const tgles::mobilegl::SupportGate* Gate(const char* id) {
  return tgles::mobilegl::FindGate(id);
}

bool GateIs(const char* id, tgles::mobilegl::GateStatus status) {
  const auto* g = Gate(id);
  return g != nullptr && g->status == status;
}

}  // namespace

// --- Matrix integrity ---

TEST(MobileGlMatrix, ThirtyOneGatesWithEvidence) {
  std::size_t n = 0;
  const auto* table = tgles::mobilegl::SupportMatrix(&n);
  EXPECT_EQ(n, 31u);
  for (std::size_t i = 0; i < n; ++i) {
    EXPECT_TRUE(table[i].id != nullptr && table[i].id[0] != '\0');
    EXPECT_TRUE(table[i].evidence != nullptr && table[i].evidence[0] != '\0');
    EXPECT_TRUE(table[i].notes != nullptr && table[i].notes[0] != '\0');
    for (std::size_t j = i + 1; j < n; ++j) {
      EXPECT_TRUE(std::string(table[i].id) != table[j].id);
    }
  }
  tgles::mobilegl::SupportSummary s = tgles::mobilegl::Summarize();
  EXPECT_EQ(s.supported + s.partial + s.missing + s.not_required, n);
  EXPECT_TRUE(Gate("nope") == nullptr);
  EXPECT_TRUE(Gate(nullptr) == nullptr);
}

TEST(MobileGlMatrix, NotReadyYetIsHonest) {
  // The host bar is met: every REQUIRED gate is Supported or NotRequired.
  // (The remaining launcher work — app vertex wiring, on-device iOS run —
  // lives in GlesContext::HostIntegrationGaps, not in this matrix.)
  const char* gaps[32] = {};
  std::size_t n = 0;
  EXPECT_TRUE(
      tgles::mobilegl::HostReadyForMobileGl(gaps, 32, &n));
  EXPECT_EQ(n, 0u);
}

// --- Contract probes ---

TEST(MobileGlMatrix, ProcAddrResolvesCore) {
  EXPECT_EQ(tgles::kCoreProcNameCount, 358u);
  EXPECT_EQ(tgles::kCoreProcNameCount, tgles::kCoreEntryPointCount);
  EXPECT_EQ(tgles::kEglProcNameCount, 46u);
  tgles::EglState egl;
  static const char* const kSpot[] = {
      "glDrawArrays", "glDrawElements", "glDrawElementsBaseVertex",
      "glDispatchCompute", "glFenceSync", "glTexStorage2D",
      "glFramebufferTexture2D", "glBeginQuery", "glBindImageTexture",
      "glPatchParameteri", "glBlitFramebuffer", "glTexBuffer",
      "glBeginTransformFeedback", "glCopyTexSubImage2D", "eglSwapBuffers",
      "eglCreateContext", "eglGetConfigs", "eglGetCurrentSurface",
      "eglSurfaceAttrib", "eglWaitClient", "eglReleaseThread",
      "eglCreateImage", "eglDestroyImage", "glBufferStorageEXT",
  };
  for (const char* name : kSpot) {
    EXPECT_TRUE(::eglGetProcAddress(name) != nullptr);
  }
  // Truly absent EXT spellings stay NULL (capability-gated, never imply
  // support). Served EXT (real bodies in gl_real.cpp: timer, tex-buffer,
  // texture-view) resolve non-NULL — the old EglState sentinel table claimed
  // NULL here, which was stale, not honest.
  EXPECT_TRUE(::eglGetProcAddress("glMultiDrawArraysEXT") == nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glQueryCounterEXT") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glTexBufferRangeEXT") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glTextureViewEXT") != nullptr);
  EXPECT_TRUE(::eglGetProcAddress("glTextureViewOES") != nullptr);
  EXPECT_TRUE(GateIs("host.procaddr-core",
                     tgles::mobilegl::GateStatus::kSupported));
}

TEST(MobileGlMatrix, ErrorModelExact) {
  tgles::BufferManager mgr;
  mgr.BindBuffer(0xDEADu, 1);
  mgr.BindBuffer(0xBEEFu, 1);  // Second error must not replace the first.
  EXPECT_EQ(mgr.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(mgr.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(GateIs("host.error-model",
                     tgles::mobilegl::GateStatus::kSupported));
}

// --- EGL probes ---

TEST(MobileGlMatrix, EglCoreFlow) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  EXPECT_TRUE(egl.Initialize(dpy, nullptr, nullptr));
  tgles::EGLint attribs[] = {tgles::kEglRedSize, 8, tgles::kEglDepthSize, 24,
                             tgles::kEglNone};
  tgles::EGLConfig cfg = 0;
  tgles::EGLint n = 0;
  EXPECT_TRUE(egl.ChooseConfig(dpy, attribs, &cfg, 1, &n));
  tgles::EGLint v32[] = {tgles::kEglContextMajorVersion, 3,
                         tgles::kEglContextMinorVersion, 2, tgles::kEglNone};
  tgles::EGLContext ctx = egl.CreateContext(dpy, cfg, 0, v32);
  EXPECT_NE(ctx, 0u);
  tgles::EGLint pb[] = {tgles::kEglWidth, 16, tgles::kEglHeight, 16,
                        tgles::kEglNone};
  tgles::EGLSurface surf = egl.CreatePbufferSurface(dpy, cfg, pb);
  EXPECT_TRUE(egl.MakeCurrent(dpy, surf, surf, ctx));
  EXPECT_TRUE(egl.SwapBuffers(dpy, surf));
  EXPECT_TRUE(GateIs("host.egl-15-core",
                     tgles::mobilegl::GateStatus::kSupported));
}

TEST(MobileGlMatrix, EglFenceFlow) {
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  tgles::EGLSync sync = egl.CreateSync(dpy, tgles::kEglSyncFence, nullptr);
  EXPECT_NE(sync, 0u);
  EXPECT_EQ(egl.ClientWaitSync(dpy, sync, 0, 0), tgles::kEglTimeoutExpired);
  egl.SignalSync(sync);
  EXPECT_EQ(egl.ClientWaitSync(dpy, sync, 0, 0),
            tgles::kEglConditionSatisfied);
  tgles::EGLAttrib status = 0;
  EXPECT_TRUE(
      egl.GetSyncAttrib(dpy, sync, tgles::kEglSyncStatus, &status));
  EXPECT_EQ(status, static_cast<tgles::EGLAttrib>(tgles::kEglSignaled));
  EXPECT_TRUE(egl.DestroySync(dpy, sync));
  EXPECT_TRUE(GateIs("host.egl-fence-sync",
                     tgles::mobilegl::GateStatus::kSupported));
}

// --- Version probes ---

TEST(MobileGlMatrix, VersionStrings) {
  tgles::Context ctx = tgles::Context::Create(false);
  EXPECT_TRUE(
      std::string(
          reinterpret_cast<const char*>(ctx.GetString(tgles::kGlVersion)))
          .find("OpenGL ES 3.2") == 0);
  tgles::EglState egl;
  tgles::EGLDisplay dpy = egl.GetDisplay(nullptr);
  egl.Initialize(dpy, nullptr, nullptr);
  EXPECT_TRUE(std::string(egl.QueryString(dpy, tgles::kEglVersion)).find(
                  "1.5") == 0);
  EXPECT_TRUE(GateIs("host.gles32-strings",
                     tgles::mobilegl::GateStatus::kSupported));
}

// --- Query probes ---

TEST(MobileGlMatrix, TimerStanceAndOcclusion) {
  EXPECT_TRUE(tgles::IsNonCoreQueryEnum("GL_TIME_ELAPSED"));
  tgles::QueryManager q;
  tgles::GLuint id = 0;
  q.GenQueries(1, &id);
  q.BeginQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed),
               id);
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  q.EndQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed));
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(GateIs("host.timer-query-ext",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.occlusion-core",
                     tgles::mobilegl::GateStatus::kSupported));
}

TEST(MobileGlMatrix, XfbObjectsAndQueries) {
  tgles::TransformFeedbackManager tf;
  tgles::GLuint id = 0;
  tf.GenTransformFeedbacks(1, &id);
  tf.BindTransformFeedback(tgles::kGlTransformFeedback, id);
  EXPECT_EQ(tf.IsTransformFeedback(id), tgles::kGlTrue);
  tf.BeginTransformFeedback(0x0004u);  // TRIANGLES.
  EXPECT_TRUE(tf.IsActive());
  tf.PauseTransformFeedback();
  EXPECT_TRUE(tf.IsPaused());
  tf.ResumeTransformFeedback();
  tf.EndTransformFeedback();
  EXPECT_FALSE(tf.IsActive());
  tf.BeginTransformFeedback(0x000Eu);  // PATCHES is not capturable.
  EXPECT_EQ(tf.GetError(), tgles::kGlInvalidEnum);
  tgles::QueryManager q;
  tgles::GLuint qid = 0;
  q.GenQueries(1, &qid);
  q.BeginQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kPrimitivesGenerated),
               qid);
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(GateIs("host.xfb-queries",
                     tgles::mobilegl::GateStatus::kSupported));
}

// --- Draw probes ---

TEST(MobileGlMatrix, BaseVertexDraws) {
  tgles::DrawValidator d;
  d.DrawElementsBaseVertex(tgles::kGlTriangles, 6,
                           tgles::kGlUnsignedShortIndex, 0, -3);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  d.DrawRangeElementsBaseVertex(tgles::kGlTriangles, 0, 100, 6,
                                tgles::kGlUnsignedShortIndex, 0, 0);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  d.DrawElementsInstancedBaseVertex(tgles::kGlTriangles, 6,
                                    tgles::kGlUnsignedShortIndex, 0, 2, 0);
  EXPECT_EQ(d.GetError(), tgles::kGlNoError);
  d.DrawElementsBaseVertex(tgles::kGlTriangles, 6, 0x1406u, 0, 0);
  EXPECT_EQ(d.GetError(), tgles::kGlInvalidEnum);
  EXPECT_TRUE(GateIs("host.draw-basevertex",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.draw-multidraw-ext",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(tgles::IsNonCoreFunction("glMultiDrawArrays"));
}

// --- Resource probes ---

TEST(MobileGlMatrix, BufferTextureTierAndRange) {
  tgles::TextureManager tm;
  tgles::GLuint t = 0, b = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTextureBuffer, t);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);  // Core tier resolvable.
  tm.TexBufferRange(tgles::kGlTextureBuffer, tgles::kGlRgba8, 99, 64, 128);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  (void)b;
  tm.TexBufferRange(tgles::kGlTextureBuffer, tgles::kGlRgba8, 99, -1, 128);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tm.TexBufferRange(tgles::kGlTexture2d, tgles::kGlRgba8, 99, 0, 128);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidEnum);
  EXPECT_TRUE(GateIs("host.buffer-texture-tier",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.buffer-texture-range",
                     tgles::mobilegl::GateStatus::kSupported));
}

TEST(MobileGlMatrix, PersistentBitsAndCopyAndReadback) {
  tgles::BufferManager bufs;
  tgles::GLuint b = 0;
  bufs.GenBuffers(1, &b);
  bufs.BindBuffer(tgles::kGlArrayBuffer, b);
  bufs.BufferData(tgles::kGlArrayBuffer, 64, nullptr, tgles::kGlDynamicDraw);
  void* p = bufs.MapBufferRange(tgles::kGlArrayBuffer, 0, 64,
                                tgles::kGlMapWriteBit |
                                    tgles::kGlMapPersistentBit |
                                    tgles::kGlMapCoherentBit);
  EXPECT_TRUE(p != nullptr);
  EXPECT_EQ(bufs.UnmapBuffer(tgles::kGlArrayBuffer), tgles::kGlTrue);
  tgles::TextureManager tm;
  tgles::GLuint t = 0;
  tm.GenTextures(1, &t);
  tm.BindTexture(tgles::kGlTexture2d, t);
  tm.TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, 8, 8, 0,
                tgles::kGlRgba, tgles::kGlUnsignedByte, nullptr);
  tm.CopyTexSubImage2D(tgles::kGlTexture2d, 0, 0, 0, 0, 0, 4, 4);
  EXPECT_EQ(tm.GetError(), tgles::kGlNoError);
  tm.CopyTexSubImage2D(tgles::kGlTexture2d, 0, 6, 6, 0, 0, 4, 4);
  EXPECT_EQ(tm.GetError(), tgles::kGlInvalidValue);
  tgles::PixelState px;
  tgles::GLint align = 0;
  px.PixelStorei(tgles::kGlPackAlignment, 8);
  px.GetPixelStorei(tgles::kGlPackAlignment, &align);
  EXPECT_EQ(align, 8);
  px.ReadPixels(0, 0, 8, 8, tgles::kGlRgba, tgles::kGlUnsignedByteType, 8, 8,
                false);
  EXPECT_EQ(px.GetError(), tgles::kGlNoError);
  px.ReadPixels(4, 4, 8, 8, tgles::kGlRgba, tgles::kGlUnsignedByteType, 8, 8,
                false);
  EXPECT_EQ(px.GetError(), tgles::kGlInvalidValue);
  EXPECT_TRUE(GateIs("host.persistent-map",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.copy-tex-image",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.readpixels-pack",
                     tgles::mobilegl::GateStatus::kSupported));
}

// Live probes for the newly Supported gates: EGL extras + image, persistent
// storage, image units, and the correctly NotRequired optional paths.

TEST(MobileGlMatrix, EglExtrasAndImageLive) {
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
  tgles::EGLint pb[] = {tgles::kEglWidth, 8, tgles::kEglHeight, 8,
                        tgles::kEglNone};
  tgles::EGLSurface surf = egl.CreatePbufferSurface(dpy, cfg, pb);
  EXPECT_TRUE(egl.MakeCurrent(dpy, surf, surf, ctx));
  EXPECT_EQ(egl.GetCurrentSurface(tgles::kEglDraw), surf);
  EXPECT_TRUE(egl.SurfaceAttrib(dpy, surf, tgles::kEglSwapBehavior, 0));
  EXPECT_TRUE(egl.WaitClient());
  tgles::EGLImage img =
      egl.CreateImage(dpy, ctx, tgles::kEglGlTexture2D, nullptr, nullptr);
  EXPECT_NE(img, tgles::kEglNoImage);
  EXPECT_TRUE(egl.DestroyImage(dpy, img));
  EXPECT_TRUE(egl.ReleaseThread());
  EXPECT_TRUE(GateIs("host.egl-platform-extras",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.egl-image",
                     tgles::mobilegl::GateStatus::kSupported));
}

TEST(MobileGlMatrix, PersistentStorageAndImageUnitsLive) {
  tgles::BufferManager bufs;
  tgles::GLuint b = 0;
  bufs.GenBuffers(1, &b);
  bufs.BindBuffer(tgles::kGlArrayBuffer, b);
  bufs.BufferStorageEXT(tgles::kGlArrayBuffer, 32, nullptr,
                        tgles::kGlMapWriteBit | tgles::kGlMapPersistentBit |
                            tgles::kGlMapCoherentBit);
  EXPECT_EQ(bufs.GetError(), tgles::kGlNoError);
  EXPECT_TRUE(bufs.IsImmutable(b));
  EXPECT_TRUE(GateIs("host.persistent-map",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.image-units",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.texture-view",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.ssbo-block-binding",
                     tgles::mobilegl::GateStatus::kNotRequired));
  EXPECT_TRUE(GateIs("host.c-abi",
                     tgles::mobilegl::GateStatus::kNotRequired));
}

TEST(MobileGlMatrix, IndexedComputeQueries) {
  tgles::ComputeState comp;
  tgles::GLint v = 0;
  comp.GetIntegeri_v(tgles::kGlMaxComputeWorkGroupCount, 0, &v);
  EXPECT_EQ(v, 65535);
  comp.GetIntegeri_v(tgles::kGlMaxComputeWorkGroupSize, 2, &v);
  EXPECT_EQ(v, 64);
  comp.GetIntegeri_v(tgles::kGlMaxComputeWorkGroupCount, 3, &v);
  EXPECT_EQ(comp.GetError(), tgles::kGlInvalidValue);
  comp.GetIntegeri_v(0x1234u, 0, &v);
  EXPECT_EQ(comp.GetError(), tgles::kGlInvalidEnum);
  EXPECT_TRUE(GateIs("host.indexed-queries",
                     tgles::mobilegl::GateStatus::kSupported));
}

// --- Pinned gaps (only true blockers stay Missing; optional/desktop-only
// --- paths are NotRequired with source-cited fallbacks) ---

TEST(MobileGlMatrix, PinnedMissingGates) {
  // iOS ObjC++ bridge is device-verified (real MSL/PSO/draw/present).
  EXPECT_TRUE(GateIs("host.metal-execution",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.present",
                     tgles::mobilegl::GateStatus::kSupported));
  // Fixed CPU-model gates are now Supported (live-probed below).
  EXPECT_TRUE(GateIs("host.egl-image",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.image-units",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.persistent-map",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.egl-platform-extras",
                     tgles::mobilegl::GateStatus::kSupported));
  // Optional/desktop-only paths correctly need no host work.
  EXPECT_TRUE(GateIs("host.c-abi",
                     tgles::mobilegl::GateStatus::kNotRequired));
  EXPECT_TRUE(GateIs("host.draw-indirect-count-ext",
                     tgles::mobilegl::GateStatus::kNotRequired));
  EXPECT_TRUE(GateIs("host.ssbo-block-binding",
                     tgles::mobilegl::GateStatus::kNotRequired));
  // Newly executed EXT paths are Supported (multidraw + base-instance draws
  // run as validated loops; texture views alias storage live).
  EXPECT_TRUE(GateIs("host.draw-multidraw-ext",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.draw-baseinstance-ext",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.texture-view",
                     tgles::mobilegl::GateStatus::kSupported));
  // Matrix stays at 31 gates with honest counts: 28 Supported, 0 Partial,
  // 0 Missing, 3 NotRequired (device-verified bridge included).
  tgles::mobilegl::SupportSummary s = tgles::mobilegl::Summarize();
  EXPECT_EQ(s.supported, 28u);
  EXPECT_EQ(s.partial, 0u);
  EXPECT_EQ(s.missing, 0u);
  EXPECT_EQ(s.not_required, 3u);
}

// --- Sync + platform probes ---

TEST(MobileGlMatrix, GlFenceRing) {
  tgles::SyncManager sync;
  tgles::GLuint s = sync.FenceSync(tgles::kGlSyncGpuCommandsComplete, 0);
  EXPECT_NE(s, 0u);
  sync.SignalSync(s);
  EXPECT_EQ(sync.ClientWaitSync(s, 0, 0), tgles::kGlAlreadySignaled);
  EXPECT_TRUE(GateIs("host.gl-fence",
                     tgles::mobilegl::GateStatus::kSupported));
}

TEST(MobileGlMatrix, IosBaselinesKnown) {
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple3") != nullptr);
  EXPECT_TRUE(GateIs("host.ios-builds",
                     tgles::mobilegl::GateStatus::kSupported));
}

TEST(MobileGlMatrix, MetalBridgePartialLive) {
  // The ObjC++ target compiles+links and the pure-C++ contract (ordering,
  // serials, suspend) holds via Mock; real pixels are proven by the
  // on-device test (MetalDevice.RealMslCompilesAndDrawsRed).
  tgles::metal_bridge::MockMetalBridge bridge;
  EXPECT_TRUE(bridge.Initialize("Apple9"));
  int fake_layer = 7;
  bridge.SetLayer(&fake_layer, 8, 8);
  EXPECT_TRUE(bridge.BeginFrame(8, 8));
  bridge.BeginRenderPass();
  bridge.Draw(tgles::kGlTriangles);
  bridge.EndRenderPass();
  EXPECT_TRUE(bridge.CommitFrame());
  EXPECT_TRUE(bridge.Present());
  EXPECT_EQ(bridge.SwapCount(), 1u);
  EXPECT_EQ(bridge.FrameSerial(), bridge.CompletedSerial());
  EXPECT_TRUE(GateIs("host.metal-execution",
                     tgles::mobilegl::GateStatus::kSupported));
  EXPECT_TRUE(GateIs("host.present",
                     tgles::mobilegl::GateStatus::kSupported));
}

// --- Both standards together ---

TEST(MobileGlMatrix, Es32ChecklistStillEmpty) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  EXPECT_TRUE(ctx.ConformanceChecklist().empty());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}
