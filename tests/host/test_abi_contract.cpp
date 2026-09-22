// Host ABI contract test (written before the implementation).
//
// What this pins, and why: a GLES host is only useful if the *shape* of its ABI
// is exactly what the loader expects. Two failure modes matter, and both are
// invisible to normal unit tests:
//
//   1. A missing symbol. MobileGL's loader resolves 46 EGL + 371 GLES names with
//      dlsym and treats any non-NULL result as callable; Khronos CTS resolves
//      core ES 3.2 names through eglGetProcAddress. A missing name = a crash in
//      someone else's process, not a test failure here.
//   2. A fake address. Historical TGL returned (void*)0x1 for "not supported".
//      MobileGL would call it. A call must either do something real or be a
//      declared gap that records itself (see docs/host-abi.md).
//
// So every assertion below is about *loading and calling*, not internal state:
// dlopen the built library, resolve every contract name, compare dlsym against
// eglGetProcAddress, refuse sentinels, call every declared gap and check it
// reported itself, then drive a real ES 3.2 conversation through the ABI.

#include "test_framework.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "tgles/host/abi.h"
#include "tgles/host/abi_gl.h"  // generated prototypes (decltype source)


// Compile-time ABI check: the C header a host includes must agree with the C++
// ground truth used inside the library. Drift fails at compile time, not at the
// first call.
#include "tgles/egl/egl.h"
#include "tgles/base/gl_types.h"

static_assert(sizeof(GLenum) == sizeof(tgles::GLenum), "GLenum width");
static_assert(sizeof(GLboolean) == 1, "GLboolean is khronos_uint8_t");
static_assert(sizeof(GLint) == sizeof(tgles::GLint), "GLint width");
static_assert(sizeof(GLsizei) == 4, "GLsizei is 32-bit");
static_assert(sizeof(GLintptr) == sizeof(void*), "GLintptr is pointer-sized");
static_assert(sizeof(GLsizeiptr) == sizeof(void*), "GLsizeiptr is pointer-sized");
static_assert(sizeof(GLint64) == 8 && sizeof(GLuint64) == 8, "64-bit scalars");
static_assert(sizeof(EGLint) == 4, "EGLint is 32-bit");
static_assert(sizeof(EGLBoolean) == 4, "EGLBoolean is 32-bit");
static_assert(sizeof(EGLAttrib) == sizeof(void*), "EGLAttrib is uintptr_t");
static_assert(sizeof(EGLTime) == 8, "EGLTime is 64-bit");

static_assert(GL_NO_ERROR == tgles::kGlNoError, "GL_NO_ERROR");
static_assert(GL_INVALID_ENUM == tgles::kGlInvalidEnum, "GL_INVALID_ENUM");
static_assert(GL_INVALID_VALUE == tgles::kGlInvalidValue, "GL_INVALID_VALUE");
static_assert(GL_INVALID_OPERATION == tgles::kGlInvalidOperation,
              "GL_INVALID_OPERATION");
static_assert(GL_OUT_OF_MEMORY == tgles::kGlOutOfMemory, "GL_OUT_OF_MEMORY");
static_assert(GL_VENDOR == tgles::kGlVendor, "GL_VENDOR");
static_assert(GL_RENDERER == tgles::kGlRenderer, "GL_RENDERER");
static_assert(GL_VERSION == tgles::kGlVersion, "GL_VERSION");
static_assert(GL_EXTENSIONS == tgles::kGlExtensions, "GL_EXTENSIONS");
static_assert(GL_SHADING_LANGUAGE_VERSION == tgles::kGlShadingLanguageVersion,
              "GL_SHADING_LANGUAGE_VERSION");
static_assert(EGL_SUCCESS == tgles::kEglSuccess, "EGL_SUCCESS");
static_assert(EGL_NOT_INITIALIZED == tgles::kEglNotInitialized,
              "EGL_NOT_INITIALIZED");
static_assert(EGL_BAD_DISPLAY == tgles::kEglBadDisplay, "EGL_BAD_DISPLAY");
static_assert(EGL_BAD_ATTRIBUTE == tgles::kEglBadAttribute, "EGL_BAD_ATTRIBUTE");
static_assert(EGL_BAD_CONFIG == tgles::kEglBadConfig, "EGL_BAD_CONFIG");
static_assert(EGL_NONE == tgles::kEglNone, "EGL_NONE");
static_assert(EGL_RED_SIZE == tgles::kEglRedSize, "EGL_RED_SIZE");
static_assert(EGL_ALPHA_SIZE == tgles::kEglAlphaSize, "EGL_ALPHA_SIZE");
static_assert(EGL_DEPTH_SIZE == tgles::kEglDepthSize, "EGL_DEPTH_SIZE");
static_assert(EGL_STENCIL_SIZE == tgles::kEglStencilSize, "EGL_STENCIL_SIZE");
static_assert(EGL_SAMPLES == tgles::kEglSamples, "EGL_SAMPLES");
static_assert(EGL_SURFACE_TYPE == tgles::kEglSurfaceType, "EGL_SURFACE_TYPE");
static_assert(EGL_RENDERABLE_TYPE == tgles::kEglRenderableType,
              "EGL_RENDERABLE_TYPE");
static_assert(EGL_TIMEOUT_EXPIRED == tgles::kEglTimeoutExpired,
              "EGL_TIMEOUT_EXPIRED");

namespace {

// The library under test. CMake passes its absolute path so the test cannot
// accidentally validate a stale system EGL.
#ifndef TGLES_HOST_LIBRARY_PATH
#define TGLES_HOST_LIBRARY_PATH "./libtgles.dylib"
#endif
#ifndef TGLES_MOBILEGL_HOST_CONTRACT_JSON
#define TGLES_MOBILEGL_HOST_CONTRACT_JSON "tools/mobilegl/host_contract.json"
#endif
constexpr const char* kLibraryPath = TGLES_HOST_LIBRARY_PATH;
constexpr const char* kContractPath = TGLES_MOBILEGL_HOST_CONTRACT_JSON;

struct Contract {
  std::vector<std::string> egl;
  std::vector<std::string> gles;
  std::vector<std::string> optional_egl;
};

// Budget for entry points that exist but are not yet served by the Metal path.
// This number is a *debt ceiling*: it is asserted by
// tests/host/test_abi_contract.cpp and documented in docs/host-abi.md. Lower it
// whenever work closes gaps; never raise it without saying so in that doc.
static_assert(EGL_TIMEOUT_EXPIRED == tgles::kEglTimeoutExpired,
              "EGL_TIMEOUT_EXPIRED");

// ---------------------------------------------------------------------------
// The loader probe, modelled on MobileGL's Loader.cpp:
//   GL_FUNC_TYPEDEF(type, name, ...) -> typedef type(*name##_PTR)(...);
//   GL_FUNC_DECL(name)               -> name##_PTR name;
//   INIT_GLES_FUNC(type, name, ...)  -> dlsym check, "Failed to load <name>"
// Every name is resolved against the *host* library. A miss here is a miss for
// MobileGL, so the miss list is part of the assertion.
// ---------------------------------------------------------------------------

namespace {

// kContractPath comes from CMake (TGLES_MOBILEGL_HOST_CONTRACT_JSON); the
// literal fallback keeps the test runnable when compiled by hand.
std::vector<std::string> ContractNames(const char* key) {
  std::ifstream in(kContractPath);
  if (!in) {
    std::printf("  cannot open %s (run tests from the repo root)\n",
                kContractPath);
    return {};
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  const std::string needle = std::string("\"") + key + "\": [";
  const std::size_t open = text.find(needle);
  if (open == std::string::npos) return {};
  const std::size_t close = text.find(']', open);
  if (close == std::string::npos) return {};

  std::vector<std::string> names;
  const std::string body = text.substr(open + needle.size(), close - open);
  std::size_t pos = 0;
  while ((pos = body.find('"', pos)) != std::string::npos) {
    const std::size_t end = body.find('"', pos + 1);
    if (end == std::string::npos) break;
    names.push_back(body.substr(pos + 1, end - pos - 1));
    pos = end + 1;
  }
  return names;
}

// Path of the freshly built host library. kLibraryPath comes from CMake
// (TGLES_HOST_LIBRARY_PATH, absolute); the fallback keeps hand-built runs sane.
const char* LibraryPath() { return kLibraryPath; }

void* HostLibrary() {
  static void* handle = dlopen(LibraryPath(), RTLD_NOW | RTLD_LOCAL);
  return handle;
}

using GetProcAddressFn = void* (*)(const char*);
using GapCountFn = unsigned int (*)();
using GapNameFn = const char* (*)(unsigned int);
using GapCallsFn = unsigned long long (*)(const char*);
using ResetLedgerFn = void (*)();

// Every dlopen'd name, resolved once and shared between cases.
struct Resolved {
  const char* name;
  void* addr;
};

}  // namespace

TEST(HostAbi, LibraryLoadsAndExportsContractEglNames) {
  void* lib = HostLibrary();
  EXPECT_TRUE(lib != nullptr);
  if (lib == nullptr) {
    std::printf("  dlopen(%s) failed: %s\n", LibraryPath(), dlerror());
    return;
  }

  const std::vector<std::string> egl_names = ContractNames("required_egl");
  EXPECT_TRUE(egl_names.size() >= 46);
  for (const std::string& name : egl_names) {
    void* addr = dlsym(lib, name.c_str());
    if (addr == nullptr) {
      std::printf("  EGL name not exported: %s\n", name.c_str());
      EXPECT_TRUE(false);
    }
  }
}

TEST(HostAbi, LibraryExportsEveryContractGlesName) {
  void* lib = HostLibrary();
  EXPECT_TRUE(lib != nullptr);
  if (lib == nullptr) return;

  const std::vector<std::string> gl_names = ContractNames("required_gles");
  // MobileGL's loader resolves this many names with dlsym at load time.
  EXPECT_TRUE(gl_names.size() >= 371);

  std::vector<std::string> missing;
  for (const std::string& name : gl_names) {
    if (dlsym(lib, name.c_str()) == nullptr) missing.push_back(name);
  }
  for (const std::string& name : missing) {
    std::printf("  GLES name not exported: %s\n", name.c_str());
  }
  EXPECT_EQ(missing.size(), std::size_t{0});
}

TEST(HostAbi, GetProcAddressAgreesWithDlsymAndHasNoSentinels) {
  void* lib = HostLibrary();
  EXPECT_TRUE(lib != nullptr);
  if (lib == nullptr) return;

  auto get_proc =
      reinterpret_cast<GetProcAddressFn>(dlsym(lib, "eglGetProcAddress"));
  EXPECT_TRUE(get_proc != nullptr);
  if (get_proc == nullptr) return;

  // Sentinels: (void*)1 / (void*)2 / (void*)-1 must never be returned.
  const void* kSentinels[] = {reinterpret_cast<void*>(1),
                              reinterpret_cast<void*>(2),
                              reinterpret_cast<void*>(~0ull)};

  std::vector<std::string> names = ContractNames("required_egl");
  const std::vector<std::string> gl_names = ContractNames("required_gles");
  names.insert(names.end(), gl_names.begin(), gl_names.end());
  EXPECT_TRUE(names.size() >= 417);

  std::size_t mismatches = 0, sentinels = 0, nulls = 0;
  for (const std::string& name : names) {
    void* via_proc = get_proc(name.c_str());
    void* via_dlsym = dlsym(lib, name.c_str());
    for (const void* sentinel : kSentinels) {
      if (via_proc == sentinel) {
        std::printf("  sentinel address for %s\n", name.c_str());
        ++sentinels;
      }
    }
    if (via_proc == nullptr) {
      std::printf("  eglGetProcAddress returned NULL for %s\n", name.c_str());
      ++nulls;
    } else if (via_proc != via_dlsym) {
      std::printf("  dispatch mismatch for %s (proc=%p dlsym=%p)\n",
                  name.c_str(), via_proc, via_dlsym);
      ++mismatches;
    }
  }
  EXPECT_EQ(sentinels, std::size_t{0});
  EXPECT_EQ(nulls, std::size_t{0});
  EXPECT_EQ(mismatches, std::size_t{0});
}

TEST(HostAbi, DeclaredGapsAreExportedAndRecordThemselves) {
  void* lib = HostLibrary();
  EXPECT_TRUE(lib != nullptr);
  if (lib == nullptr) return;

  auto gap_count =
      reinterpret_cast<GapCountFn>(dlsym(lib, "tglesAbiGapCount"));
  auto gap_name = reinterpret_cast<GapNameFn>(dlsym(lib, "tglesAbiGapName"));
  auto gap_calls = reinterpret_cast<GapCallsFn>(dlsym(lib, "tglesAbiGapCalls"));
  EXPECT_TRUE(gap_count != nullptr);
  EXPECT_TRUE(gap_name != nullptr);
  EXPECT_TRUE(gap_calls != nullptr);
  if (gap_count == nullptr || gap_name == nullptr || gap_calls == nullptr) return;

  const unsigned int count = gap_count();
  if (count == 0) return;  // Nothing declared as a gap: nothing to check.

  // Each declared gap must name itself, be exported, and expose a counter.
  for (unsigned int i = 0; i < count; ++i) {
    const char* name = gap_name(i);
    EXPECT_TRUE(name != nullptr);
    if (name == nullptr) continue;
    EXPECT_TRUE(std::strlen(name) > 2);
    EXPECT_TRUE(dlsym(lib, name) != nullptr);
  }

  // The ledger must be real: the counter for a declared gap is queryable and
  // monotonic. Hosts poll it to decide whether a feature is safely usable.
  const char* first = gap_name(0);
  if (first == nullptr) return;
  const unsigned long long calls = gap_calls(first);
  EXPECT_TRUE(gap_calls("definitely_not_a_gap_name") == 0);
  EXPECT_TRUE(calls == gap_calls(first));
}

TEST(HostAbi, UnknownGetProcAddressReturnsNullAndIsAccepted) {
  void* lib = HostLibrary();
  if (lib == nullptr) return;
  auto get_proc =
      reinterpret_cast<GetProcAddressFn>(dlsym(lib, "eglGetProcAddress"));
  if (get_proc == nullptr) return;

  // Spec: unknown names yield NULL. Callers must tolerate it (glad checks for
  // NULL; CTS's ExtLoader records the miss), so NULL is the correct answer for
  // a name TGL genuinely has no entry point for.
  EXPECT_TRUE(get_proc("glTotallyMadeUpName") == nullptr);
  EXPECT_TRUE(get_proc("eglNotAThing") == nullptr);
}

// Debt ceiling: 27 trial-subset bodies in src/host/host_c_api.cpp + 344 in
// src/host/abi/gl_real.cpp (previous 321 + final23: compressed/copy tex,
// integer clears, readback, indexed enables, raster misc, FBO texture/
// invalidate, multisample storage, shader binary) = 371 real; 0 declared
// gaps. The ledger remains for honesty (future regressions would re-appear
// here). Never raise this number.
constexpr unsigned int TGlesExpectedGapBudget = 0;

std::string ReadFile(const char* path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

// Pulls a JSON array of strings out of the contract without a JSON dependency:
// the contract is generated, so its shape is stable (one name per line in an
// array). A missing file is a test failure, never a silent skip.
std::vector<std::string> ExtractArray(const std::string& json,
                                     const std::string& key) {
  std::vector<std::string> names;
  std::size_t key_pos = json.find("\"" + key + "\"");
  if (key_pos == std::string::npos) return names;
  std::size_t open = json.find('[', key_pos);
  std::size_t close = json.find(']', open);
  if (open == std::string::npos || close == std::string::npos) return names;
  std::string body = json.substr(open + 1, close - open - 1);
  std::size_t pos = 0;
  while ((pos = body.find('"', pos)) != std::string::npos) {
    std::size_t end = body.find('"', pos + 1);
    if (end == std::string::npos) break;
    names.push_back(body.substr(pos + 1, end - pos - 1));
    pos = end + 1;
  }
  return names;
}

Contract LoadContract() {
  Contract contract;
  const std::string json = ReadFile(kContractPath);
  contract.egl = ExtractArray(json, "required_egl");
  contract.gles = ExtractArray(json, "required_gles");
  contract.optional_egl = ExtractArray(json, "optional_egl");
  return contract;
}

void* OpenLibrary() {
  void* lib = dlopen(kLibraryPath, RTLD_NOW | RTLD_LOCAL);
  if (lib == nullptr) {
    std::printf("  dlopen(%s) failed: %s\n", kLibraryPath, dlerror());
  }
  return lib;
}

bool IsSentinel(void* address) {
  // (void*)0x1 and friends: "not supported" markers that a loader will call.
  return address != nullptr && reinterpret_cast<std::uintptr_t>(address) < 0x1000;
}

}  // namespace

// --- Contract completeness -------------------------------------------------

TEST(HostAbiContract, ContractListsAreNotEmpty) {
  Contract contract = LoadContract();
  EXPECT_TRUE(contract.egl.size() >= 40);
  EXPECT_TRUE(contract.gles.size() >= 300);
}

TEST(HostAbiContract, EveryEglNameIsExported) {
  Contract contract = LoadContract();
  void* lib = OpenLibrary();
  EXPECT_NOT_NULL(lib);
  if (lib == nullptr) return;
  int missing = 0;
  for (const std::string& name : contract.egl) {
    if (dlsym(lib, name.c_str()) == nullptr) {
      std::printf("  missing EGL symbol: %s\n", name.c_str());
      ++missing;
    }
  }
  EXPECT_EQ(missing, 0);
  dlclose(lib);
}

TEST(HostAbiContract, EveryGlesNameIsExported) {
  Contract contract = LoadContract();
  void* lib = OpenLibrary();
  EXPECT_NOT_NULL(lib);
  if (lib == nullptr) return;
  int missing = 0;
  for (const std::string& name : contract.gles) {
    if (dlsym(lib, name.c_str()) == nullptr) {
      if (missing < 12) std::printf("  missing GLES symbol: %s\n", name.c_str());
      ++missing;
    }
  }
  std::printf("  GLES contract names: %zu, missing: %d\n", contract.gles.size(),
              missing);
  EXPECT_EQ(missing, 0);
  dlclose(lib);
}

static_assert(EGL_WIDTH == tgles::kEglWidth, "EGL_WIDTH");
static_assert(EGL_HEIGHT == tgles::kEglHeight, "EGL_HEIGHT");
static_assert(EGL_PBUFFER_BIT == tgles::kEglPbufferBit, "EGL_PBUFFER_BIT");
static_assert(EGL_WINDOW_BIT == tgles::kEglWindowBit, "EGL_WINDOW_BIT");
static_assert(EGL_OPENGL_ES3_BIT == tgles::kEglOpenGlEs3Bit,
              "EGL_OPENGL_ES3_BIT");
static_assert(EGL_CONTEXT_CLIENT_VERSION == tgles::kEglContextClientVersion,
              "EGL_CONTEXT_CLIENT_VERSION");
static_assert(EGL_VENDOR == tgles::kEglVendor, "EGL_VENDOR");
static_assert(EGL_VERSION == tgles::kEglVersion, "EGL_VERSION");
static_assert(EGL_EXTENSIONS == tgles::kEglExtensions, "EGL_EXTENSIONS");
static_assert(EGL_CLIENT_APIS == tgles::kEglClientApis, "EGL_CLIENT_APIS");
static_assert(EGL_SYNC_FENCE == tgles::kEglSyncFence, "EGL_SYNC_FENCE");
static_assert(EGL_SYNC_STATUS == tgles::kEglSyncStatus, "EGL_SYNC_STATUS");
static_assert(EGL_CONDITION_SATISFIED == tgles::kEglConditionSatisfied,
              "EGL_CONDITION_SATISFIED");
static_assert(EGL_TIMEOUT_EXPIRED == tgles::kEglTimeoutExpired,
              "EGL_TIMEOUT_EXPIRED");

namespace {

// Loads the library once per test process and resolves a name, failing loudly.
class Lib {
 public:
  explicit Lib(const char* path) : handle_(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {}
  ~Lib() { if (handle_ != nullptr) dlclose(handle_); }

  bool ok() const { return handle_ != nullptr; }
  void* handle() const { return handle_; }

  template <typename Fn>
  Fn Sym(const char* name) const {
    void* address = handle_ == nullptr ? nullptr : dlsym(handle_, name);
    if (address == nullptr) std::printf("  dlsym(%s) failed\n", name);
    return reinterpret_cast<Fn>(address);
  }

 private:
  void* handle_;
};

}  // namespace

// --- Sentinel and dispatch behaviour ---------------------------------------

TEST(HostAbiContract, NoSentinelAddresses) {
  // A magic small integer is worse than NULL: it survives every "is it
  // non-null?" check in a loader and crashes at the first call. TGL used to
  // hand out 0x1 for unsupported names; this test makes that unrepresentable.
  const Contract contract = LoadContract();
  Lib lib(kLibraryPath);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  auto proc = lib.Sym<void* (*)(const char*)>("eglGetProcAddress");
  EXPECT_NOT_NULL(proc);
  if (proc == nullptr) return;
  int sentinels = 0;
  for (const std::string& name : contract.gles) {
    if (IsSentinel(proc(name.c_str()))) {
      std::printf("  sentinel for %s\n", name.c_str());
      ++sentinels;
    }
  }
  for (const std::string& name : contract.egl) {
    if (IsSentinel(proc(name.c_str()))) {
      std::printf("  sentinel for %s\n", name.c_str());
      ++sentinels;
    }
  }
  EXPECT_EQ(sentinels, 0);
}

TEST(HostAbiContract, DlsymAndGetProcAddressAgree) {
  // CTS resolves through libEGL symbols, MobileGL through eglGetProcAddress.
  // If the two views differ, one host gets a different driver than the other.
  const Contract contract = LoadContract();
  Lib lib(kLibraryPath);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  auto proc = lib.Sym<void* (*)(const char*)>("eglGetProcAddress");
  if (proc == nullptr) return;
  int mismatches = 0;
  for (const std::string& name : contract.gles) {
    void* direct = dlsym(lib.handle(), name.c_str());
    void* via_proc = proc(name.c_str());
    if (direct != via_proc) {
      if (mismatches < 10) {
        std::printf("  %s: dlsym=%p proc=%p\n", name.c_str(), direct, via_proc);
      }
      ++mismatches;
    }
  }
  EXPECT_EQ(mismatches, 0);
}

TEST(HostAbiContract, UnknownNameReturnsNull) {
  Lib lib(kLibraryPath);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  auto proc = lib.Sym<void* (*)(const char*)>("eglGetProcAddress");
  if (proc == nullptr) return;
  EXPECT_TRUE(proc("glTotallyMadeUpFunction") == nullptr);
  EXPECT_TRUE(proc("") == nullptr);
  EXPECT_TRUE(proc(nullptr) == nullptr);
  // Desktop GL names are not part of a GLES host: resolving them would let a
  // desktop-GL app believe it can use features TGL never implements.
  EXPECT_TRUE(proc("glPolygonMode") == nullptr);
}



// --- The gap ledger is the honesty mechanism -------------------------------

TEST(HostAbiContract, GapsAreEnumeratedAndCallable) {
  // A "gap" is an entry point that exists (so the ABI is complete and no host
  // resolves a null) but whose GLES behaviour is not served by the Metal path
  // yet. Each gap records itself when called. The test calls every declared gap
  // and proves the counter moved: a gap that lies about being called would be
  // an invisible failure, which is exactly what this project must not have.
  Lib lib(kLibraryPath);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;

  auto gap_count = lib.Sym<unsigned int (*)(void)>("tglesAbiGapCount");
  auto gap_name = lib.Sym<const char* (*)(unsigned int)>("tglesAbiGapName");
  auto gap_calls =
      lib.Sym<unsigned long long (*)(const char*)>("tglesAbiGapCalls");
  auto reset = lib.Sym<void (*)(void)>("tglesAbiResetGapLedger");
  EXPECT_NOT_NULL(gap_count);
  EXPECT_NOT_NULL(gap_name);
  EXPECT_NOT_NULL(gap_calls);
  EXPECT_NOT_NULL(reset);
  if (gap_count == nullptr || gap_name == nullptr || gap_calls == nullptr ||
      reset == nullptr) {
    return;
  }

  const unsigned int total = gap_count();
  std::printf("  declared gaps: %u (budget %d)\n", total, TGlesExpectedGapBudget);
  // Gaps are allowed; unbounded growth is not. The budget is signed off in
  // docs/host-abi.md and must be lowered (never raised) as work lands.
  EXPECT_TRUE(total <= TGlesExpectedGapBudget);

  reset();
  for (unsigned int i = 0; i < total; ++i) {
    const char* name = gap_name(i);
    EXPECT_NOT_NULL(name);
    if (name == nullptr) continue;
    void* address = dlsym(lib.handle(), name);
    if (address == nullptr) {
      std::printf("  declared gap %s is not exported\n", name);
      EXPECT_TRUE(false);
      continue;
    }
    auto call = reinterpret_cast<void (*)(void)>(address);
    call();
    call();
    EXPECT_EQ(gap_calls(name), 2ull);
  }
  reset();
}

TEST(HostAbiContract, VersionIdentifiesTheLibrary) {
  Lib lib(kLibraryPath);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  auto version = lib.Sym<const char* (*)(void)>("tglesAbiVersion");
  EXPECT_NOT_NULL(version);
  if (version == nullptr) return;
  const std::string text = version();
  EXPECT_TRUE(text.find("TGL") != std::string::npos);
  EXPECT_TRUE(text.find("3.2") != std::string::npos);
}

// --- A real ES 3.2 conversation over the ABI -------------------------------
//
// Types come from the generated ABI header (decltype of each declaration)
// rather than being retyped here: if a prototype in abi_gl.h drifts from what
// a host expects, this file stops compiling.

namespace {

// One list, three uses (declare / resolve), so the EGL and GLES sets can never
// fall out of sync with each other.
#define TGL_EGL_FUNCS(X)                                                \
  X(GetDisplay, decltype(&eglGetDisplay), eglGetDisplay)                \
  X(Initialize, decltype(&eglInitialize), eglInitialize)                \
  X(Terminate, decltype(&eglTerminate), eglTerminate)                   \
  X(QueryString, decltype(&eglQueryString), eglQueryString)             \
  X(ChooseConfig, decltype(&eglChooseConfig), eglChooseConfig)          \
  X(GetConfigAttrib, decltype(&eglGetConfigAttrib), eglGetConfigAttrib) \
  X(CreatePbufferSurface, decltype(&eglCreatePbufferSurface),           \
    eglCreatePbufferSurface)                                            \
  X(CreateContext, decltype(&eglCreateContext), eglCreateContext)       \
  X(MakeCurrent, decltype(&eglMakeCurrent), eglMakeCurrent)             \
  X(DestroySurface, decltype(&eglDestroySurface), eglDestroySurface)    \
  X(DestroyContext, decltype(&eglDestroyContext), eglDestroyContext)    \
  X(BindApi, decltype(&eglBindAPI), eglBindAPI)                         \
  X(QueryApi, decltype(&eglQueryAPI), eglQueryAPI)                      \
  X(EglGetError, decltype(&eglGetError), eglGetError)

#define TGL_GL_FUNCS(X)                                                   \
  X(GetString, decltype(&glGetString), glGetString)                       \
  X(GetIntegerv, decltype(&glGetIntegerv), glGetIntegerv)                 \
  X(GlGetError, decltype(&glGetError), glGetError)                        \
  X(GenBuffers, decltype(&glGenBuffers), glGenBuffers)                    \
  X(DeleteBuffers, decltype(&glDeleteBuffers), glDeleteBuffers)           \
  X(BindBuffer, decltype(&glBindBuffer), glBindBuffer)                    \
  X(BufferData, decltype(&glBufferData), glBufferData)                    \
  X(CreateShader, decltype(&glCreateShader), glCreateShader)              \
  X(DeleteShader, decltype(&glDeleteShader), glDeleteShader)              \
  X(ShaderSource, decltype(&glShaderSource), glShaderSource)              \
  X(CompileShader, decltype(&glCompileShader), glCompileShader)           \
  X(GetShaderiv, decltype(&glGetShaderiv), glGetShaderiv)                 \
  X(CreateProgram, decltype(&glCreateProgram), glCreateProgram)           \
  X(DeleteProgram, decltype(&glDeleteProgram), glDeleteProgram)           \
  X(AttachShader, decltype(&glAttachShader), glAttachShader)              \
  X(BindAttribLocation, decltype(&glBindAttribLocation), glBindAttribLocation) \
  X(LinkProgram, decltype(&glLinkProgram), glLinkProgram)                 \
  X(GetProgramiv, decltype(&glGetProgramiv), glGetProgramiv)              \
  X(GetAttribLocation, decltype(&glGetAttribLocation), glGetAttribLocation) \
  X(UseProgram, decltype(&glUseProgram), glUseProgram)                    \
  X(GenVertexArrays, decltype(&glGenVertexArrays), glGenVertexArrays)     \
  X(DeleteVertexArrays, decltype(&glDeleteVertexArrays), glDeleteVertexArrays) \
  X(BindVertexArray, decltype(&glBindVertexArray), glBindVertexArray)     \
  X(EnableVertexAttribArray, decltype(&glEnableVertexAttribArray),        \
    glEnableVertexAttribArray)                                            \
  X(VertexAttribPointer, decltype(&glVertexAttribPointer),               \
    glVertexAttribPointer)                                                \
  X(DrawArrays, decltype(&glDrawArrays), glDrawArrays)                    \
  X(Finish, decltype(&glFinish), glFinish)                                \
  X(Flush, decltype(&glFlush), glFlush)

struct Api {
#define TGL_DECLARE(field, type, name) type field = nullptr;
  TGL_EGL_FUNCS(TGL_DECLARE)
  TGL_GL_FUNCS(TGL_DECLARE)
#undef TGL_DECLARE

  explicit Api(Lib* library) : lib(library) {
#define TGL_RESOLVE(field, type, name)          \
    field = lib->Sym<type>(#name);              \
    if (field == nullptr) {                     \
      std::printf("  unresolved: %s\n", #name); \
      complete = false;                         \
    }
    TGL_EGL_FUNCS(TGL_RESOLVE)
    TGL_GL_FUNCS(TGL_RESOLVE)
#undef TGL_RESOLVE
  }

  Lib* lib;
  bool complete = true;
};

}  // namespace

TEST(HostAbiContract, DrivesEs32ContextSetup) {
  // The bootstrap an EGL host performs: display, config (the pbuffer shape CTS
  // requests), config attribute read-back CTS validates, API binding, surface,
  // context, make-current, then server strings. A GLES app can do nothing
  // before all of this succeeds, so it is the first thing to pin.
  Lib lib(kLibraryPath);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  Api api(&lib);
  EXPECT_TRUE(api.complete);
  if (!api.complete) return;

  EGLDisplay display = api.GetDisplay((EGLNativeDisplayType)0);
  EXPECT_TRUE(display != EGL_NO_DISPLAY);
  if (display == EGL_NO_DISPLAY) return;

  EGLint major = 0, minor = 0;
  EXPECT_TRUE(api.Initialize(display, &major, &minor) == EGL_TRUE);
  EXPECT_TRUE(major >= 1);
  EXPECT_NOT_NULL(api.QueryString(display, EGL_VENDOR));
  EXPECT_NOT_NULL(api.QueryString(display, EGL_VERSION));
  const char* extensions = api.QueryString(display, EGL_EXTENSIONS);
  EXPECT_NOT_NULL(extensions);
  // Grepping this string is how launchers and users diagnose support, so an
  // empty list is a real defect even though the spec allows it.
  EXPECT_TRUE(extensions != nullptr && extensions[0] != '\0');
  EXPECT_EQ(api.EglGetError(), EGL_SUCCESS);

  const EGLint config_attribs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
      EGL_RED_SIZE,        8,
      EGL_GREEN_SIZE,      8,
      EGL_BLUE_SIZE,       8,
      EGL_ALPHA_SIZE,      8,
      EGL_DEPTH_SIZE,      24,
      EGL_STENCIL_SIZE,    8,
      EGL_SAMPLES,         0,
      EGL_NONE};
  EGLint num_config = 0;
  EXPECT_TRUE(api.ChooseConfig(display, config_attribs, nullptr, 0, &num_config) ==
              EGL_TRUE);
  EXPECT_TRUE(num_config >= 1);
  EGLConfig config = nullptr;
  EXPECT_TRUE(api.ChooseConfig(display, config_attribs, &config, 1, &num_config) ==
              EGL_TRUE);
  EXPECT_NOT_NULL(config);

  EGLint red = 0, depth = 0, stencil = 0, surface_type = 0, renderable = 0;
  EXPECT_TRUE(api.GetConfigAttrib(display, config, EGL_RED_SIZE, &red) ==
              EGL_TRUE);
  EXPECT_TRUE(api.GetConfigAttrib(display, config, EGL_DEPTH_SIZE, &depth) ==
              EGL_TRUE);
  EXPECT_TRUE(api.GetConfigAttrib(display, config, EGL_STENCIL_SIZE, &stencil) ==
              EGL_TRUE);
  EXPECT_TRUE(api.GetConfigAttrib(display, config, EGL_SURFACE_TYPE,
                                  &surface_type) == EGL_TRUE);
  EXPECT_TRUE(api.GetConfigAttrib(display, config, EGL_RENDERABLE_TYPE,
                                  &renderable) == EGL_TRUE);
  EXPECT_TRUE(red >= 8);
  EXPECT_TRUE(depth >= 24);
  EXPECT_TRUE(stencil >= 8);
  EXPECT_TRUE((surface_type & EGL_PBUFFER_BIT) != 0);
  EXPECT_TRUE((renderable & EGL_OPENGL_ES3_BIT) != 0);

  EXPECT_TRUE(api.BindApi(EGL_OPENGL_ES_API) == EGL_TRUE);
  EXPECT_EQ(api.QueryApi(), (EGLenum)EGL_OPENGL_ES_API);

  const EGLint pbuffer_attribs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  EGLSurface surface = api.CreatePbufferSurface(display, config, pbuffer_attribs);
  EXPECT_TRUE(surface != EGL_NO_SURFACE);

  const EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                                    EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
  EGLContext context =
      api.CreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
  EXPECT_TRUE(context != EGL_NO_CONTEXT);
  EXPECT_TRUE(api.MakeCurrent(display, surface, surface, context) == EGL_TRUE);

  // Server state is only reachable after make-current: that is the contract a
  // GLES host depends on, and the string it reports is what launchers show.
  const char* gl_version = (const char*)api.GetString(GL_VERSION);
  EXPECT_NOT_NULL(gl_version);
  if (gl_version != nullptr) {
    EXPECT_TRUE(std::strstr(gl_version, "OpenGL ES 3.2") != nullptr);
  }
  EXPECT_NOT_NULL(api.GetString(GL_VENDOR));
  EXPECT_NOT_NULL(api.GetString(GL_RENDERER));
  EXPECT_NOT_NULL(api.GetString(GL_SHADING_LANGUAGE_VERSION));
  GLint gl_major = 0, gl_minor = 0;
  api.GetIntegerv(GL_MAJOR_VERSION, &gl_major);
  api.GetIntegerv(GL_MINOR_VERSION, &gl_minor);
  EXPECT_EQ(gl_major, 3);
  EXPECT_EQ(gl_minor, 2);
  EXPECT_EQ(api.GlGetError(), GL_NO_ERROR);

  EXPECT_TRUE(api.DestroyContext(display, context) == EGL_TRUE);
  EXPECT_TRUE(api.DestroySurface(display, surface) == EGL_TRUE);
  EXPECT_TRUE(api.Terminate(display) == EGL_TRUE);
}


TEST(HostAbiContract, DrivesEs32ObjectAndDrawPath) {
  // Everything a renderer touches in its first second: buffer upload, shader
  // compile, program link, VAO setup, draw, teardown. It must run without a
  // single GL error and without a Metal drawable (validation-only path), which
  // is what makes the headless configuration of CTS and CI meaningful.
  Lib lib(kLibraryPath);
  EXPECT_TRUE(lib.ok());
  if (!lib.ok()) return;
  Api api(&lib);
  EXPECT_TRUE(api.complete);
  if (!api.complete) return;

  EGLDisplay display = api.GetDisplay((EGLNativeDisplayType)0);
  if (display == EGL_NO_DISPLAY) return;
  EGLint major = 0, minor = 0;
  if (api.Initialize(display, &major, &minor) != EGL_TRUE) return;
  const EGLint config_attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                   EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint num_config = 0;
  if (api.ChooseConfig(display, config_attribs, &config, 1, &num_config) !=
      EGL_TRUE) {
    return;
  }
  const EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                                    EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
  EGLContext context =
      api.CreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
  if (context == EGL_NO_CONTEXT) return;
  const EGLint pbuffer_attribs[] = {EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE};
  EGLSurface surface = api.CreatePbufferSurface(display, config, pbuffer_attribs);
  if (api.MakeCurrent(display, surface, surface, context) != EGL_TRUE) return;

  GLuint buffer = 0;
  api.GenBuffers(1, &buffer);
  EXPECT_TRUE(buffer != 0u);
  api.BindBuffer(GL_ARRAY_BUFFER, buffer);
  const GLfloat vertices[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  api.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices), vertices,
                 GL_STATIC_DRAW);
  EXPECT_EQ(api.GlGetError(), GL_NO_ERROR);

  const GLchar* vertex_source =
      (const GLchar*)"#version 320 es\n"
                     "layout(location = 0) in vec3 a_pos;\n"
                     "void main() { gl_Position = vec4(a_pos, 1.0); }\n";
  const GLchar* fragment_source =
      (const GLchar*)"#version 320 es\n"
                     "precision mediump float;\n"
                     "layout(location = 0) out vec4 o_color;\n"
                     "void main() { o_color = vec4(1.0); }\n";
  GLuint vs = api.CreateShader(GL_VERTEX_SHADER);
  GLuint fs = api.CreateShader(GL_FRAGMENT_SHADER);
  EXPECT_TRUE(vs != 0u && fs != 0u);
  api.ShaderSource(vs, 1, &vertex_source, nullptr);
  api.ShaderSource(fs, 1, &fragment_source, nullptr);
  api.CompileShader(vs);
  api.CompileShader(fs);
  GLint compiled = 0;
  api.GetShaderiv(vs, GL_COMPILE_STATUS, &compiled);
  EXPECT_TRUE(compiled == GL_TRUE);
  api.GetShaderiv(fs, GL_COMPILE_STATUS, &compiled);
  EXPECT_TRUE(compiled == GL_TRUE);

  GLuint program = api.CreateProgram();
  api.AttachShader(program, vs);
  api.AttachShader(program, fs);
  api.BindAttribLocation(program, 0, "a_pos");
  api.LinkProgram(program);
  GLint linked = 0;
  api.GetProgramiv(program, GL_LINK_STATUS, &linked);
  EXPECT_TRUE(linked == GL_TRUE);
  api.UseProgram(program);
  EXPECT_EQ(api.GetAttribLocation(program, "a_pos"), 0);

  GLuint vao = 0;
  api.GenVertexArrays(1, &vao);
  api.BindVertexArray(vao);
  api.BindBuffer(GL_ARRAY_BUFFER, buffer);
  api.EnableVertexAttribArray(0);
  api.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  api.DrawArrays(GL_TRIANGLES, 0, 3);
  EXPECT_EQ(api.GlGetError(), GL_NO_ERROR);
  api.Finish();
  api.Flush();
  EXPECT_EQ(api.GlGetError(), GL_NO_ERROR);

  api.DeleteShader(vs);
  api.DeleteShader(fs);
  api.DeleteProgram(program);
  api.DeleteVertexArrays(1, &vao);
  api.DeleteBuffers(1, &buffer);
  EXPECT_EQ(api.GlGetError(), GL_NO_ERROR);

  EXPECT_TRUE(api.DestroyContext(display, context) == EGL_TRUE);
  EXPECT_TRUE(api.DestroySurface(display, surface) == EGL_TRUE);
  EXPECT_TRUE(api.Terminate(display) == EGL_TRUE);
}


