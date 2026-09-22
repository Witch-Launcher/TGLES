// KHR_debug message filtering (spec ch.18, TDD red-first).
//
// Why: DebugMessageControl used to validate its enums and drop the `enabled`
// flag on the floor, so "disable notifications" was silently ignored. These
// cases pin that control state actually gates what DebugMessageInsert stores.

#include "test_framework.h"

#include "tgles/state/debug.h"

namespace {

void InsertAppMarker(tgles::DebugManager& dbg, tgles::GLuint id) {
  dbg.DebugMessageInsert(tgles::kGlDebugSourceApplication,
                         tgles::kGlDebugTypeMarker, id,
                         tgles::kGlDebugSeverityNotification, -1, "m");
}

}  // namespace

TEST(DebugFilter, DisabledGroupIsNotStored) {
  tgles::DebugManager dbg;
  dbg.DebugMessageControl(tgles::kGlDebugSourceApplication,
                          tgles::kGlDebugTypeMarker,
                          tgles::kGlDebugSeverityNotification, 0, nullptr,
                          tgles::kGlFalse);
  EXPECT_EQ(dbg.GetError(), tgles::kGlNoError);
  InsertAppMarker(dbg, 7);
  EXPECT_EQ(dbg.GetError(), tgles::kGlNoError);
  EXPECT_EQ(dbg.PendingMessages(), 0u);
  // Re-enable: the same message is stored again.
  dbg.DebugMessageControl(tgles::kGlDebugSourceApplication,
                          tgles::kGlDebugTypeMarker,
                          tgles::kGlDebugSeverityNotification, 0, nullptr,
                          tgles::kGlTrue);
  InsertAppMarker(dbg, 7);
  EXPECT_EQ(dbg.PendingMessages(), 1u);
}

TEST(DebugFilter, PerIdRuleBeatsGroupRule) {
  tgles::DebugManager dbg;
  const tgles::GLuint muted = 7;
  dbg.DebugMessageControl(tgles::kGlDebugSourceApplication,
                          tgles::kGlDebugTypeMarker,
                          tgles::kGlDebugSeverityNotification, 1, &muted,
                          tgles::kGlFalse);
  EXPECT_EQ(dbg.GetError(), tgles::kGlNoError);
  InsertAppMarker(dbg, 7);
  EXPECT_EQ(dbg.PendingMessages(), 0u);
  InsertAppMarker(dbg, 8);
  EXPECT_EQ(dbg.PendingMessages(), 1u);
}

TEST(DebugFilter, DontCareWildcardMatchesAll) {
  tgles::DebugManager dbg;
  dbg.DebugMessageControl(tgles::kGlDontCare, tgles::kGlDontCare,
                          tgles::kGlDontCare, 0, nullptr, tgles::kGlFalse);
  EXPECT_EQ(dbg.GetError(), tgles::kGlNoError);
  InsertAppMarker(dbg, 7);
  EXPECT_EQ(dbg.PendingMessages(), 0u);
}
