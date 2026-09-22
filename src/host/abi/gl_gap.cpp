// GENERATED gap bodies by tools/host_abi/gen_gl_abi.py.
//
// These entry points exist so the ABI is complete - a host that resolves
// them gets a real function, never a sentinel address - but the Metal
// execution path does not serve them yet. Every call records itself in the
// ABI ledger, so the gap is measured instead of hidden. Move a function
// into gl_real.cpp and regenerate to close one.

#include "tgles/host/abi_gl.h"

#include <cstddef>

#include "tgles/host/abi_ledger.h"
