#ifndef TGLES_MOBILEGL_SUPPORT_H
#define TGLES_MOBILEGL_SUPPORT_H

// MobileGL-DirectGLES host support matrix: every gate a GLES 3.2 host must
// satisfy for MobileGL (desktop GL -> DirectGLES -> host GLES -> Metal).
// Evidence per gate: MobileGL source (dev branch, Sep 2026), MobileGL-docs,
// Khronos EGL 1.5 / GLES 3.2 specs, docs/reference headers.
//
// Statuses: kSupported (implemented + live-probed in tests), kPartial
// (works with documented limits), kMissing (pinned gap, blocks that path),
// kNotRequired (MobileGL handles it above the host: shader lowering,
// frontend emulation, or capability-gated opt-out).

#include <cstddef>

namespace tgles {
namespace mobilegl {

enum class GateStatus {
  kSupported,
  kPartial,
  kMissing,
  kNotRequired,
};

struct SupportGate {
  const char* id;        // Stable id, e.g. "host.gles32-state".
  const char* category;  // "contract", "version", "query", "draw", "resource",
                         // "sync", "egl", "execution", "platform".
  const char* evidence;  // Where the requirement comes from.
  GateStatus status;
  const char* notes;  // Limits, fallbacks, or the exact gap.
};

const SupportGate* SupportMatrix(std::size_t* out_count);
const SupportGate* FindGate(const char* id);

struct SupportSummary {
  std::size_t supported = 0;
  std::size_t partial = 0;
  std::size_t missing = 0;
  std::size_t not_required = 0;
};
SupportSummary Summarize();

// True when every gate that MobileGL REQUIRES from the host (Supported or
// NotRequired paths) holds. Missing/Partial gates are listed in out_gaps.
bool HostReadyForMobileGl(const char** out_gaps, std::size_t capacity,
                          std::size_t* out_count);

}  // namespace mobilegl
}  // namespace tgles

#endif  // TGLES_MOBILEGL_SUPPORT_H
