// TGL debug-log channel tests (pure C++, no GPU): toggle granularity,
// ring buffering, drain semantics and the C ABI surface the launcher polls.
// Ground truth: include/tgles/base/debug_log.h contract.

#include "test_framework.h"

#include <cstring>
#include <string>

#include "tgles/base/debug_log.h"

namespace {

// Save/restore the process-wide mode so this file never leaks a test-only
// setting into later suites (the default is stderr|ring).
struct ModeGuard {
  explicit ModeGuard(int mode) : saved_(tgles::TglDebugLogMode()) {
    tgles::TglSetDebugLogMode(mode);
  }
  ~ModeGuard() { tgles::TglSetDebugLogMode(saved_); }
  int saved_;
};

void DrainAll() {
  char buf[4096];
  while (tgles::TglDebugLogDrain(buf, sizeof(buf)) > 0) {
  }
}

}  // namespace

TEST(DebugLog, FullySilentWhenModeIsZero) {
  ModeGuard guard(0);
  DrainAll();
  tgles::TglDebugf("this must go nowhere %d", 42);
  EXPECT_EQ(tgles::TglDebugLogMode(), 0);
  // Size query reports nothing pending (ring sink is off).
  EXPECT_EQ(tgles::TglDebugLogDrain(nullptr, 0), 0);
  char buf[64] = {0};
  EXPECT_EQ(tgles::TglDebugLogDrain(buf, sizeof(buf)), 0);
}

TEST(DebugLog, RingBuffersAndDrains) {
  ModeGuard guard(tgles::kTglDebugLogRing);  // stderr off: quiet test output.
  DrainAll();
  tgles::TglDebugf("hello %s", "ring");
  tgles::TglDebugf("second line");
  // Size query does not drain.
  const int pending = tgles::TglDebugLogDrain(nullptr, 0);
  EXPECT_TRUE(pending > 0);
  EXPECT_EQ(tgles::TglDebugLogDrain(nullptr, -1), pending);
  char buf[1024];
  const int n = tgles::TglDebugLogDrain(buf, sizeof(buf));
  EXPECT_EQ(n, pending);
  const std::string text(buf, static_cast<std::size_t>(n));
  EXPECT_TRUE(text.find("[TGL-DEBUG] hello ring") != std::string::npos);
  EXPECT_TRUE(text.find("[TGL-DEBUG] second line") != std::string::npos);
  // Drained: second read is empty.
  EXPECT_EQ(tgles::TglDebugLogDrain(buf, sizeof(buf)), 0);
}

TEST(DebugLog, SmallBufferTruncatesSafely) {
  ModeGuard guard(tgles::kTglDebugLogRing);
  DrainAll();
  tgles::TglDebugf("0123456789abcdef");
  char buf[8];
  const int n = tgles::TglDebugLogDrain(buf, sizeof(buf));
  EXPECT_TRUE(n > 0 && n < 8);
  EXPECT_TRUE(buf[n] == '\0');
  // Remainder was dropped with the drained entry (documented drain).
  EXPECT_EQ(tgles::TglDebugLogDrain(buf, sizeof(buf)), 0);
}

TEST(DebugLog, StderrOnlyLeavesRingEmpty) {
  ModeGuard guard(tgles::kTglDebugLogStderr);
  DrainAll();
  tgles::TglDebugf("stderr sink only");
  EXPECT_EQ(tgles::TglDebugLogDrain(nullptr, 0), 0);
}
