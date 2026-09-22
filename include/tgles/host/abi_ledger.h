#ifndef TGLES_HOST_ABI_LEDGER_H
#define TGLES_HOST_ABI_LEDGER_H

// ABI gap ledger.
//
// A GLES host that silently fails is the worst outcome for a translator like
// MobileGL: the app sees NO_ERROR and renders nothing, and nobody knows which
// call was dropped. TGL therefore never fakes a call. Either an entry point is
// served by a manager (gl_real.cpp) or it is a declared gap that
//
//   1. records the fact (name + call count) in this ledger, and
//   2. returns the spec default for its call class (0 / GL_FALSE / void),
//      which is what "the command had no effect" looks like to a caller.
//
// Tests assert the ledger is empty for the paths they claim to cover, so
// "we support X" is a checkable statement and the remaining work is a number.

#include <cstddef>

namespace tgles {
namespace host {

// Record one call of the named gap (thread-safe).
void LedgerRecord(const char* name);

// Distinct gap names recorded since the last reset.
std::size_t LedgerSize();

// Name of the gap at [index] (index < LedgerSize()), else nullptr. Order is
// the order the gaps were first hit, which makes failures reproducible.
const char* LedgerNameAt(std::size_t index);

// Calls recorded for the named gap (0 when the name was never hit).
unsigned long long LedgerCalls(const char* name);

// Drop all records (tests, so each case measures only its own calls).
void LedgerReset();

}  // namespace host
}  // namespace tgles

#endif  // TGLES_HOST_ABI_LEDGER_H
