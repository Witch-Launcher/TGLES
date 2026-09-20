#ifndef TGLES_METAL_BRIDGE_APPLE_H
#define TGLES_METAL_BRIDGE_APPLE_H

// Real Apple bridge (OBJCXX only). This header stays pure C++ so .cpp
// translation units can reference the factory without pulling <Metal/*>.
// The implementation lives in src/metal_bridge_apple.mm and links
// -framework Metal/QuartzCore/Foundation (iOS SDK, also macOS SDK).

#include <memory>

#include "tgles/metal_bridge.h"

namespace tgles {
namespace metal_bridge {

// Returns nullptr when Metal headers are unavailable (non-Apple host).
// On Apple platforms returns an Apple-backed bridge; Initialize() still
// validates the GPU family string before touching MTLCreateSystemDefaultDevice.
std::unique_ptr<MetalBridge> CreateAppleBridge();

}  // namespace metal_bridge
}  // namespace tgles

#endif  // TGLES_METAL_BRIDGE_APPLE_H
