#include "tgles/mobilegl_support.h"

#include <cstring>

namespace tgles {
namespace mobilegl {

namespace {

const SupportGate kGates[] = {
    // --- Loading contract (BackendObject_DirectGLES.cpp:846 acquires the
    // --- whole host table through eglGetProcAddress; Loader.cpp:130) ---
    {"host.procaddr-core", "contract",
     "AcquireGLESFunctions via eglGetProcAddress (BackendObject_DirectGLES.cpp:846, Loader.cpp:130)",
     GateStatus::kSupported,
     "Resolves 358 core + 46 EGL + glBufferStorageEXT; other EXT stay NULL by design."},
    {"host.c-abi", "contract",
     "Host loaded via dlopen+eglGetProcAddress, not link-time (Loader.cpp OpenLib+Acquire)",
     GateStatus::kNotRequired,
     "C symbols are MobileGL-to-app ABI (Definitions.cpp/GetProcAddress.cpp/dylib injection), not host requirement."},
    {"host.error-model", "contract",
     "CallAndCheck macro (DirectGLES.h:17) depends on exact GL errors",
     GateStatus::kSupported,
     "First-error latch, no side effects, zero returns (spec 2.3.1)."},
    // --- EGL (loader EGLFunctionsTable: 46 functions, all INIT_EGL_FUNC) ---
    {"host.egl-15-core", "egl", "EGL 1.5 display/config/context/surface/current/swap",
     GateStatus::kSupported, "EglState covers all 46 loader names incl. pixmap/platform stubs."},
    {"host.egl-fence-sync", "egl",
     "eglCreateSync/ClientWaitSync/WaitSync (loader table; presentation.md fence ring)",
     GateStatus::kSupported, "EGL fence sync with BAD_PARAMETER (spec-verified)."},
    {"host.egl-image", "egl", "eglCreateImage/DestroyImage (loader table, EGLState/Core.cpp)",
     GateStatus::kSupported, "EGLImage handles with BAD_CONTEXT/PARAMETER/MATCH validation."},
    {"host.egl-platform-extras", "egl",
     "GetConfigs/CurrentSurface/SurfaceAttrib/Wait*/ReleaseThread (all INIT_EGL_FUNC)",
     GateStatus::kSupported,
     "GetConfigs enumerates; CurrentSurface tracks DRAW/READ; SurfaceAttrib validates; Wait*/ReleaseThread no-op success."},
    // --- Version identity (Driver POST) ---
    {"host.gles32-strings", "version",
     "POST identity: VERSION 3.2 + ESSL 3.20 (capabilities.md)",
     GateStatus::kSupported, "Context reports 3.2 / GLSL ES 3.20."},
    {"host.egl-strings", "version", "EGL 1.5 + OpenGL_ES client APIs",
     GateStatus::kSupported, "EglState::QueryString."},
    // --- Queries (DirectGLES.h AreTimerQueriesSupported et al.) ---
    {"host.timer-query-ext", "query",
     "GL_EXT_disjoint_timer_query + MOBILEGL_DISABLE_TIMERQUERY escape hatch",
     GateStatus::kSupported,
     "Timer enums are EXT-only here, matching the opt-out stance."},
    {"host.occlusion-core", "query",
     "ANY_SAMPLES_PASSED independent of timer flag (DirectGLES.h)",
     GateStatus::kSupported, "Core Table 4.2 targets in QueryManager."},
    {"host.xfb-queries", "query",
     "PRIMITIVES_GENERATED (ES 3.2+) + TF capture spans (XfbImpl)",
     GateStatus::kSupported,
     "Query targets + TF objects (begin/end/pause/resume, deferred model)."},
    // --- Draw family (DirectGLES.h backend surface) ---
    {"host.draw-core", "draw", "11 core draw entry points + PATCHES",
     GateStatus::kSupported, "DrawValidator with indirect alignment rules."},
    {"host.draw-basevertex", "draw",
     "DrawElementsBaseVertex core ES 3.2 (DirectGLES.h backend surface)",
     GateStatus::kSupported, "BaseVertex/Range/Instanced variants validated."},
    {"host.draw-multidraw-ext", "draw",
     "MultiDraw*EXT emulated in MultiDraw.cpp (frontend-side)",
     GateStatus::kNotRequired,
     "Host must NOT expose core multdraw (correctly rejected)."},
    {"host.draw-baseinstance-ext", "draw",
     "gl_BaseInstance lowered to uniforms (shader-and-essl.md)",
     GateStatus::kNotRequired, "Shader-side lowering, no host entry needed."},
    {"host.draw-indirect-count-ext", "draw", "IndirectCount (frontend-side)",
     GateStatus::kNotRequired, "Count-driven loops live above the host."},
    // --- Resources ---
    {"host.buffer-texture-tier", "resource",
     "Tier 'core (ES 3.2)' via glTexBuffer (DirectGLES.h)",
     GateStatus::kSupported, "TextureManager::TexBuffer."},
    {"host.buffer-texture-range", "resource",
     "CallTexBufferRange with whole-buffer fallback (DirectGLES.h)",
     GateStatus::kSupported, "TexBufferRange recorded; bounds at draw time."},
    {"host.texture-view", "resource",
     "SupportsTextureView capability gate (InitCapabilities:853, Managers.cpp:3594, BackendObject:1202)",
     GateStatus::kNotRequired, "EXT/OES optional; withheld string + INVALID_OPERATION is valid ES behavior."},
    {"host.persistent-map", "resource",
     "Persistent mapping via glBufferStorageEXT (Managers.cpp:986/1793) + MOBILEGL_COHERENT_AS_FLUSH",
     GateStatus::kSupported,
     "BufferStorageEXT immutable + Map PERSISTENT/COHERENT + FlushMappedBufferRange live."},
    {"host.copy-tex-image", "resource",
     "CopyTexImage/SubImage backend paths (DirectGLES.h)",
     GateStatus::kSupported, "CopyTexSubImage2D validated (src rect at facade)."},
    {"host.readpixels-pack", "resource",
     "Packed readback + PACK/UNPACK shadows (backends.md, presentation.md)",
     GateStatus::kSupported, "PixelState: 10 store params + ReadPixels checks."},
    {"host.indexed-queries", "resource",
     "GetIntegeri_v/64i_v (DirectGLES.h backend surface)",
     GateStatus::kSupported, "Compute COUNT/SIZE indexed queries live."},
    {"host.ssbo-block-binding", "resource",
     "Desktop-only glShaderStorageBlockBinding needs no ES host (Managers.cpp:8229, Utils.cpp:855)",
     GateStatus::kNotRequired, "Baked via SPIRV-Cross SetShaderStorageBlockBinding; host uses GetProgramResource core."},
    {"host.image-units", "resource",
     "BindImageTexture units core ES 3.1+ (gl32.h) REQUIRED (Loader INIT_GLES_FUNC, DirectGLES.cpp:1706/1772)",
     GateStatus::kSupported, "8 image units with access/format validation live."},
    // --- Sync (presentation.md fence ring) ---
    {"host.gl-fence", "sync",
     "Per-frame glFenceSync + poll ring; null-degrade documented (DirectGLES.h)",
     GateStatus::kSupported, "SyncManager with backend SignalSync hook."},
    // --- Execution + present: device-verified on host Metal (Intel KBL)
    // --- via tests/test_metal_device.mm (MSL compile, PSO, triangle draw,
    // --- presentDrawable, async fence ring, exact red pixels). A-series
    // --- on-device run is still pending (see HostIntegrationGaps).
    {"host.metal-execution", "execution",
     "MTL device/queue/library/PSO/buffer/encoder via tgles_metal_bridge (metal_bridge_apple.mm)",
     GateStatus::kSupported, "Real MSL+PSO+draw+blit+fence, device-tested."},
    {"host.present", "execution",
     "CAMetalLayer layer/drawable/present + fence serials (metal_bridge_apple.mm Present, EGLImpl MetalLayer)",
     GateStatus::kSupported, "Real presentDrawable+commit, device-tested."},
    // --- Platform & method ---
    {"host.ios-builds", "platform",
     "MOBILEGL_IOS=ON pattern; arm64 + simulator (platforms.md)",
     GateStatus::kSupported, "Both Apple targets compile clean."},
    {"host.post-model", "platform",
     "Driver POST categories mirrored as tests (validation.md)",
     GateStatus::kSupported, "This matrix + live probes per Supported gate."},
};

}  // namespace

const SupportGate* SupportMatrix(std::size_t* out_count) {
  if (out_count != nullptr) {
    *out_count = sizeof(kGates) / sizeof(kGates[0]);
  }
  return kGates;
}

const SupportGate* FindGate(const char* id) {
  if (id == nullptr) return nullptr;
  for (const auto& g : kGates) {
    if (std::strcmp(g.id, id) == 0) return &g;
  }
  return nullptr;
}

SupportSummary Summarize() {
  SupportSummary s;
  for (const auto& g : kGates) {
    switch (g.status) {
      case GateStatus::kSupported:
        ++s.supported;
        break;
      case GateStatus::kPartial:
        ++s.partial;
        break;
      case GateStatus::kMissing:
        ++s.missing;
        break;
      case GateStatus::kNotRequired:
        ++s.not_required;
        break;
    }
  }
  return s;
}

bool HostReadyForMobileGl(const char** out_gaps, std::size_t capacity,
                          std::size_t* out_count) {
  std::size_t n = 0;
  for (const auto& g : kGates) {
    if (g.status == GateStatus::kMissing ||
        g.status == GateStatus::kPartial) {
      if (n < capacity && out_gaps != nullptr) out_gaps[n] = g.id;
      ++n;
    }
  }
  if (out_count != nullptr) *out_count = n;
  return n == 0;
}

}  // namespace mobilegl
}  // namespace tgles
