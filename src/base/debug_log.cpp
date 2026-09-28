#include "tgles/base/debug_log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

namespace tgles {

namespace {

// Bounded ring: oldest text drops past the cap instead of growing without
// bound on a long game session (a black-screen run can log per draw).
constexpr std::size_t kRingByteCap = 64u * 1024u;

std::mutex& LogMutex() {
  static std::mutex m;
  return m;
}

int& LogMode() {
  static int mode = kTglDebugLogStderr | kTglDebugLogRing;
  return mode;
}

std::deque<std::string>& LogRing() {
  static std::deque<std::string> ring;
  return ring;
}

std::size_t& RingBytes() {
  static std::size_t bytes = 0;
  return bytes;
}

}  // namespace

void TglSetDebugLogMode(int mode) {
  std::lock_guard<std::mutex> lock(LogMutex());
  LogMode() = mode;
}

int TglDebugLogMode() {
  std::lock_guard<std::mutex> lock(LogMutex());
  return LogMode();
}

long long TglNowMs() {
  // Epoch captured at first use so every diag line in the session shares one
  // origin; steady_clock keeps it immune to wall-clock jumps.
  static const std::chrono::steady_clock::time_point epoch =
      std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - epoch)
      .count();
}

void TglDebugf(const char* fmt, ...) {
  if (fmt == nullptr) return;
  char body[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  body[sizeof(body) - 1] = '\0';
  // Call sites historically end messages with '\n'; the sinks add their own
  // line break, so strip one trailing newline to avoid blank lines.
  std::string msg = body;
  if (!msg.empty() && msg.back() == '\n') msg.pop_back();
  std::string line = "[TGL-DEBUG] ";
  line += msg;
  std::lock_guard<std::mutex> lock(LogMutex());
  const int mode = LogMode();
  if (mode == 0) return;
  if ((mode & kTglDebugLogStderr) != 0) {
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
  }
  if ((mode & kTglDebugLogRing) != 0) {
    line += '\n';
    LogRing().push_back(line);
    RingBytes() += line.size();
    while (RingBytes() > kRingByteCap && !LogRing().empty()) {
      RingBytes() -= LogRing().front().size();
      LogRing().pop_front();
    }
  }
}

int TglDebugLogDrain(char* out, int capacity) {
  std::lock_guard<std::mutex> lock(LogMutex());
  std::size_t pending = RingBytes();
  if (out == nullptr || capacity <= 0) {
    return static_cast<int>(pending);
  }
  int written = 0;
  while (!LogRing().empty() && written < capacity - 1) {
    const std::string& front = LogRing().front();
    for (char c : front) {
      if (written >= capacity - 1) break;
      out[written++] = c;
    }
    RingBytes() -= front.size();
    LogRing().pop_front();
  }
  out[written] = '\0';
  return written;
}

}  // namespace tgles
