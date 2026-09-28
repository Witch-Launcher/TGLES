#ifndef TGLES_DEBUG_LOG_H
#define TGLES_DEBUG_LOG_H

// Process-wide TGL debug log (the `[TGL-DEBUG]` channel).
//
// Why this exists: every fail-closed path in the facade/bridge names its
// reason via this channel. On macOS that text reaches the terminal through
// stderr, but a launcher on iOS only captures NSLog + game stdout — the
// dylib's stderr never reaches its log file, so a black screen stays
// unexplained. This module keeps the same text in a bounded in-memory ring
// the launcher polls through the C ABI (`tglHostSetDebugLog` /
// `tglHostGetDebugLog`), plus the historical stderr sink. Both sinks are
// independently toggleable; mode 0 silences the channel completely.
//
// Pure C++ (no platform headers) so every target (macOS host, iphoneos,
// iphonesimulator) shares it. Thread-safe; the ring drops the oldest text
// past its byte cap instead of growing.

namespace tgles {

// Debug-log sinks (bitmask for TglSetDebugLogMode / tglHostSetDebugLog).
inline constexpr int kTglDebugLogStderr = 1;  // fprintf(stderr, ...) sink.
inline constexpr int kTglDebugLogRing = 2;    // In-memory ring for polling.

// Sets the active sinks (any combination, 0 = fully silent). Defaults to
// stderr|ring, matching historical behavior plus the new polling surface.
void TglSetDebugLogMode(int mode);
int TglDebugLogMode();

// printf-style emit. The "[TGL-DEBUG] " prefix is added here, so call sites
// pass only the message body. No-op (except argument evaluation) when the
// mode is 0.
void TglDebugf(const char* fmt, ...);

// Milliseconds since the first call in this process (CLOCK_MONOTONIC-based).
// Diagnostic lines carry it as `t=` so ring text can be aligned with the
// game's own timestamped log (swap vs. progress vs. exit ordering).
long long TglNowMs();

// Copies drained ring text into `out` (NUL-terminated when capacity > 0)
// and clears the ring. Returns bytes written excluding the NUL. With
// `out == nullptr` or `capacity <= 0`, returns the pending byte count and
// drains nothing (size query).
int TglDebugLogDrain(char* out, int capacity);

}  // namespace tgles

#endif  // TGLES_DEBUG_LOG_H
