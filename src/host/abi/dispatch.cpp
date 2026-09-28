// Single name -> address table for the whole host contract (plan-02 Block A).
//
// Why one table: MobileGL resolves GLES names through eglGetProcAddress while
// CTS (and plain dlopen users) resolve the same names with dlsym. If the two
// routes ever disagree, one host gets a different driver than the other. This
// file is the only place that maps a name to an address, so divergence is
// unrepresentable: eglGetProcAddress looks up the same static table the
// dynamic linker serves.
//
// Rules (asserted by tests/host/test_abi_contract.cpp):
//   - every required_egl / required_gles name returns a real function address;
//   - unknown names (including desktop-GL names like glPolygonMode) return
//     NULL — never a sentinel such as (void*)0x1, which a loader would call;
//   - gap functions are real too: they record themselves in the ABI ledger.

#include "tgles/host/abi.h"
#include "tgles/host/abi_gl.h"

#include <cstring>

#include "tgles/base/version.h"
#include "tgles/host/abi_ledger.h"

namespace {

// One entry per exported symbol. GLES bodies come from gl_real.cpp (real) and
// gl_gap.cpp (declared gaps); EGL bodies come from host_c_api.cpp (trial
// subset) and egl_api.cpp (the rest). All are declared by abi.h / abi_gl.h, so
// a signature drift fails here at compile time.
struct Entry {
  const char* name;
  void* address;
};

#define TGL_EGL_ENTRY(name) \
  { #name, reinterpret_cast<void*>(&name) },

// clang-format off
const Entry kEglTable[] = {
    TGL_EGL_ENTRY(eglGetDisplay)
    TGL_EGL_ENTRY(eglGetPlatformDisplay)
    TGL_EGL_ENTRY(eglGetPlatformDisplayEXT)
    TGL_EGL_ENTRY(eglInitialize)
    TGL_EGL_ENTRY(eglTerminate)
    TGL_EGL_ENTRY(eglQueryString)
    TGL_EGL_ENTRY(eglGetConfigs)
    TGL_EGL_ENTRY(eglChooseConfig)
    TGL_EGL_ENTRY(eglGetConfigAttrib)
    TGL_EGL_ENTRY(eglCreateWindowSurface)
    TGL_EGL_ENTRY(eglCreatePlatformWindowSurface)
    TGL_EGL_ENTRY(eglCreatePlatformWindowSurfaceEXT)
    TGL_EGL_ENTRY(eglCreatePbufferSurface)
    TGL_EGL_ENTRY(eglCreatePbufferFromClientBuffer)
    TGL_EGL_ENTRY(eglCreatePixmapSurface)
    TGL_EGL_ENTRY(eglCreatePlatformPixmapSurface)
    TGL_EGL_ENTRY(eglCreatePlatformPixmapSurfaceEXT)
    TGL_EGL_ENTRY(eglDestroySurface)
    TGL_EGL_ENTRY(eglQuerySurface)
    TGL_EGL_ENTRY(eglSurfaceAttrib)
    TGL_EGL_ENTRY(eglBindTexImage)
    TGL_EGL_ENTRY(eglReleaseTexImage)
    TGL_EGL_ENTRY(eglSwapInterval)
    TGL_EGL_ENTRY(eglSwapBuffers)
    TGL_EGL_ENTRY(eglSwapBuffersWithDamageEXT)
    TGL_EGL_ENTRY(eglCopyBuffers)
    TGL_EGL_ENTRY(eglUnlockSurfaceKHR)
    TGL_EGL_ENTRY(eglCreateContext)
    TGL_EGL_ENTRY(eglDestroyContext)
    TGL_EGL_ENTRY(eglMakeCurrent)
    TGL_EGL_ENTRY(eglGetCurrentContext)
    TGL_EGL_ENTRY(eglGetCurrentSurface)
    TGL_EGL_ENTRY(eglGetCurrentDisplay)
    TGL_EGL_ENTRY(eglQueryContext)
    TGL_EGL_ENTRY(eglWaitClient)
    TGL_EGL_ENTRY(eglWaitGL)
    TGL_EGL_ENTRY(eglWaitNative)
    TGL_EGL_ENTRY(eglBindAPI)
    TGL_EGL_ENTRY(eglQueryAPI)
    TGL_EGL_ENTRY(eglReleaseThread)
    TGL_EGL_ENTRY(eglWaitSync)
    TGL_EGL_ENTRY(eglWaitSyncKHR)
    TGL_EGL_ENTRY(eglCreateImage)
    TGL_EGL_ENTRY(eglDestroyImage)
    TGL_EGL_ENTRY(eglCreateSync)
    TGL_EGL_ENTRY(eglDestroySync)
    TGL_EGL_ENTRY(eglClientWaitSync)
    TGL_EGL_ENTRY(eglGetSyncAttrib)
    TGL_EGL_ENTRY(eglGetError)
    TGL_EGL_ENTRY(eglGetProcAddress)
};

const Entry kGlTable[] = {
#include "gl_symbols.inc"
};

// TGL present extensions (abi.h): same single-table rule, so a loader that
// resolves everything through eglGetProcAddress (MobileGL style) finds them
// exactly like dlsym does. NOT part of the Khronos/MobileGL contract counts.
const Entry kTglTable[] = {
    TGL_EGL_ENTRY(tglHostAttachMetalLayer)
    TGL_EGL_ENTRY(tglHostResizeMetalLayer)
    TGL_EGL_ENTRY(tglHostPresent)
    TGL_EGL_ENTRY(tglHostReadbackPixel)
    TGL_EGL_ENTRY(tglHostFrameSerial)
    TGL_EGL_ENTRY(tglHostSetDebugLog)
    TGL_EGL_ENTRY(tglHostGetDebugLog)
};
// clang-format on

const Entry* Lookup(const char* procname) {
  if (procname == nullptr || procname[0] == '\0') return nullptr;
  for (const Entry& e : kEglTable) {
    if (std::strcmp(procname, e.name) == 0) return &e;
  }
  for (const Entry& e : kGlTable) {
    if (std::strcmp(procname, e.name) == 0) return &e;
  }
  for (const Entry& e : kTglTable) {
    if (std::strcmp(procname, e.name) == 0) return &e;
  }
  return nullptr;
}

// Declared gaps, generated so the count can only change by regenerating.
// This is the debt ceiling the contract test asserts: it enumerates gaps
// whether or not they were ever called (the ledger only counts calls).
const char* const kDeclaredGaps[] = {
#include "gap_names.inc"
};

}  // namespace

extern "C" {

// Honest dispatch: real address or NULL. No sentinels, ever.
void* eglGetProcAddress(const char* procname) {
  const Entry* found = Lookup(procname);
  return found != nullptr ? found->address : nullptr;
}

// --- Gap ledger query surface (see abi_ledger.h) ----------------------------
// Hosts and CTS harnesses poll these to decide whether a feature is safely
// usable. The ledger is the honesty mechanism: a gap is measured, never
// hidden (docs/host-abi.md, planned).

unsigned int tglesAbiGapCount(void) {
  return static_cast<unsigned int>(sizeof(kDeclaredGaps) /
                                  sizeof(kDeclaredGaps[0]));
}

const char* tglesAbiGapName(unsigned int index) {
  const unsigned int count = tglesAbiGapCount();
  return index < count ? kDeclaredGaps[index] : nullptr;
}

unsigned long long tglesAbiGapCalls(const char* name) {
  return tgles::host::LedgerCalls(name);
}

void tglesAbiResetGapLedger(void) { tgles::host::LedgerReset(); }

const char* tglesAbiVersion(void) { return tgles::kVersionString; }

}  // extern "C"
