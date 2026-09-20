## Resident and transient storage

`VkBufferManager` gives a frontend buffer one of two main execution representations:

- a resident `VkBufferObject` containing device-visible storage, suitable for repeated draws;
- a per-frame `BufferArena` slice containing a streamed upload of the current semantic shadow.

Persistent mapped buffers use a resident host-visible/coherent allocation and can be written directly by the application. Other streaming paths upload into a frame-slot arena and cache the slice by the frontend change serial, so repeated draws in one frame do not re-upload unchanged data.

## Buffer arenas

Each `BufferArena` has one frame resource and write cursor per frames-in-flight slot. `Upload` aligns the cursor, grows the slot buffer when necessary, copies bytes into the mapped range, and returns a `BufferSlice` containing the native buffer and offset. When a buffer grows, the old allocation is placed on a deferred-release list for that slot.

The frame fence, rather than the C++ owner, determines when a superseded arena buffer may be destroyed. `BeginFrame` resets only the slot whose previous GPU work has completed.

## Immediate updates

Frontend buffer operations such as respecification, sub-data updates and flushed mapped ranges are forwarded to `VkBufferManager`. If the update can be applied directly to a resident allocation, the manager does so. If the resource is busy or the operation needs a staging copy, it records the copy outside a render pass and tracks a pending/full-upload flag. The next draw acquires a slice that is known to contain the current shadow.

## Usage flags and upgrades

The manager creates resident buffers with the union of usages that may be required by the semantic target: vertex, index, uniform, indirect, texel, storage or transfer. This avoids invalidating a buffer merely because a later GL operation uses a new binding target. Persistent mapping is similarly sticky because recreating storage every time the mapping state changes would be more expensive and could violate in-flight usage.

## Resource lifetime fields

`VkBufferResource::lastUseSerial` records the most recent GPU submission that can reference the buffer. `pendingFullUpload` forces a complete shadow upload after an interrupted update. `transientFrameSerial` and `transientChangeSerial` distinguish a cached slice that is valid for the current frame from one that belongs to an older recording.
