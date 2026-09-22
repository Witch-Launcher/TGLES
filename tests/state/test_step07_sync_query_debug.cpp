// Step 7 tests: fences (spec 4.1), async queries (spec 4.2) and debug
// output (spec ch.18).

#include "test_framework.h"

#include "tgles/state/debug.h"
#include "tgles/state/query.h"
#include "tgles/state/sync.h"

TEST(Step07, FenceLifecycle) {
  tgles::SyncManager sync;
  tgles::GLuint s = sync.FenceSync(tgles::kGlSyncGpuCommandsComplete, 0);
  EXPECT_NE(s, 0u);
  EXPECT_EQ(sync.IsSync(s), tgles::kGlTrue);
  EXPECT_EQ(sync.IsSync(0), tgles::kGlFalse);
  EXPECT_EQ(sync.FenceSync(0x1234u, 0), 0u);  // Bad condition.
  EXPECT_EQ(sync.GetError(), tgles::kGlInvalidEnum);
  EXPECT_EQ(sync.FenceSync(tgles::kGlSyncGpuCommandsComplete, 0xFFu), 0u);
  EXPECT_EQ(sync.GetError(), tgles::kGlInvalidValue);  // Flags must be 0.
  sync.DeleteSync(s);
  EXPECT_EQ(sync.IsSync(s), tgles::kGlFalse);
  sync.DeleteSync(0);  // Zero silently ignored.
  EXPECT_EQ(sync.GetError(), tgles::kGlNoError);
  sync.DeleteSync(s);  // Already gone.
  EXPECT_EQ(sync.GetError(), tgles::kGlInvalidValue);
}

TEST(Step07, ClientWaitSemantics) {
  tgles::SyncManager sync;
  tgles::GLuint s = sync.FenceSync(tgles::kGlSyncGpuCommandsComplete, 0);
  // Unsignaled + zero timeout -> TIMEOUT_EXPIRED (spec 4.1.1).
  EXPECT_EQ(sync.ClientWaitSync(s, 0, 0), tgles::kGlTimeoutExpired);
  sync.SignalSync(s);  // Backend reports GPU completion.
  EXPECT_EQ(sync.ClientWaitSync(s, 0, 0), tgles::kGlAlreadySignaled);
  EXPECT_EQ(sync.ClientWaitSync(s, tgles::kGlSyncFlushCommandsBit, 0),
            tgles::kGlAlreadySignaled);
  EXPECT_EQ(sync.ClientWaitSync(s, 0xFFu, 0), tgles::kGlWaitFailed);
  EXPECT_EQ(sync.GetError(), tgles::kGlInvalidValue);
  // WAIT_FAILED carries INVALID_VALUE for unknown syncs (spec-verified).
  EXPECT_EQ(sync.ClientWaitSync(424242u, 0, 0), tgles::kGlWaitFailed);
  EXPECT_EQ(sync.GetError(), tgles::kGlInvalidValue);
  sync.WaitSync(s, 0, tgles::kGlTimeoutIgnored);
  EXPECT_EQ(sync.GetError(), tgles::kGlNoError);
  sync.WaitSync(s, 1, tgles::kGlTimeoutIgnored);  // Flags must be 0.
  EXPECT_EQ(sync.GetError(), tgles::kGlInvalidValue);
  tgles::GLint status = 0;
  sync.GetSynciv(s, tgles::kGlSyncStatus, 1, nullptr, &status);
  EXPECT_EQ(status, static_cast<tgles::GLint>(tgles::kGlSignaled));
  tgles::GLint64 timeout = 0;
  sync.GetInteger64v(tgles::kGlMaxServerWaitTimeout, &timeout);
  EXPECT_TRUE(timeout > 0);
  EXPECT_EQ(sync.GetError(), tgles::kGlNoError);
}

TEST(Step07, QueryBeginEnd) {
  tgles::QueryManager q;
  tgles::GLuint id = 0;
  q.GenQueries(1, &id);
  // Generated but never begun: not yet an object (uniform object model).
  EXPECT_EQ(q.IsQuery(id), tgles::kGlFalse);
  q.BeginQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed),
               id);
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  EXPECT_EQ(q.IsQuery(id), tgles::kGlTrue);
  tgles::GLint current = -1;
  q.GetQueryiv(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed),
               tgles::kGlCurrentQuery, &current);
  EXPECT_EQ(current, static_cast<tgles::GLint>(id));
  q.BeginQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed),
               id);  // Same type still active.
  EXPECT_EQ(q.GetError(), tgles::kGlInvalidOperation);
  q.EndQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed));
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
  q.EndQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed));
  EXPECT_EQ(q.GetError(), tgles::kGlInvalidOperation);  // Nothing active.
  q.BeginQuery(0x1234u, id);
  EXPECT_EQ(q.GetError(), tgles::kGlInvalidEnum);
}

TEST(Step07, QueryResults) {
  tgles::QueryManager q;
  tgles::GLuint id = 0;
  q.GenQueries(1, &id);
  q.BeginQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kPrimitivesGenerated),
               id);
  q.EndQuery(tgles::QueryTargetEnum(tgles::QueryTarget::kPrimitivesGenerated));
  tgles::GLuint avail = 99;
  q.GetQueryObjectuiv(id, tgles::kGlQueryResultAvailable, &avail);
  EXPECT_EQ(avail, 0u);
  q.CompleteQuery(id, 42);
  q.GetQueryObjectuiv(id, tgles::kGlQueryResultAvailable, &avail);
  EXPECT_EQ(avail, 1u);
  tgles::GLuint result = 0;
  q.GetQueryObjectuiv(id, tgles::kGlQueryResult, &result);
  EXPECT_EQ(result, 42u);
  q.GetQueryObjectuiv(424242u, tgles::kGlQueryResult, &result);
  EXPECT_EQ(q.GetError(), tgles::kGlInvalidOperation);
  EXPECT_EQ(q.GetError(), tgles::kGlNoError);
}

TEST(Step07, DebugMessages) {
  tgles::DebugManager dbg;
  dbg.DebugMessageInsert(tgles::kGlDebugSourceApplication,
                         tgles::kGlDebugTypeMarker, 7,
                         tgles::kGlDebugSeverityNotification, -1, "hello");
  EXPECT_EQ(dbg.PendingMessages(), 1u);
  EXPECT_EQ(dbg.GetError(), tgles::kGlNoError);
  dbg.DebugMessageInsert(tgles::kGlDebugSourceApi, tgles::kGlDebugTypeMarker, 7,
                         tgles::kGlDebugSeverityNotification, -1, "nope");
  EXPECT_EQ(dbg.GetError(), tgles::kGlInvalidOperation);  // API not allowed.
  tgles::GLenum src = 0;
  tgles::GLuint msg_id = 0;
  char text[32] = {};
  tgles::GLuint n = dbg.GetDebugMessageLog(1, sizeof(text), &src, nullptr,
                                           &msg_id, nullptr, nullptr, text);
  EXPECT_EQ(n, 1u);
  EXPECT_EQ(src, tgles::kGlDebugSourceApplication);
  EXPECT_EQ(msg_id, 7u);
  EXPECT_EQ(dbg.PendingMessages(), 0u);
}

TEST(Step07, LabelsAndGroups) {
  tgles::DebugManager dbg;
  dbg.ObjectLabel(tgles::kGlTextureObject, 5, -1, "albedo");
  EXPECT_EQ(dbg.GetObjectLabel(tgles::kGlTextureObject, 5), "albedo");
  EXPECT_EQ(dbg.GetError(), tgles::kGlNoError);
  dbg.ObjectLabel(0x1234u, 5, -1, "x");
  EXPECT_EQ(dbg.GetError(), tgles::kGlInvalidEnum);
  dbg.PushDebugGroup(tgles::kGlDebugSourceApplication, 1, -1, "frame");
  EXPECT_EQ(dbg.GroupDepth(), 1u);
  dbg.PopDebugGroup();
  EXPECT_EQ(dbg.GroupDepth(), 0u);
  dbg.PopDebugGroup();  // Empty stack.
  EXPECT_EQ(dbg.GetError(), tgles::kGlInvalidOperation);
  dbg.DebugMessageControl(tgles::kGlDebugSourceApi, tgles::kGlDebugTypeError,
                          tgles::kGlDebugSeverityHigh, 0, nullptr,
                          tgles::kGlTrue);
  EXPECT_EQ(dbg.GetError(), tgles::kGlNoError);
  dbg.DebugMessageControl(0x1234u, tgles::kGlDebugTypeError,
                          tgles::kGlDebugSeverityHigh, 0, nullptr,
                          tgles::kGlTrue);
  EXPECT_EQ(dbg.GetError(), tgles::kGlInvalidEnum);
}
