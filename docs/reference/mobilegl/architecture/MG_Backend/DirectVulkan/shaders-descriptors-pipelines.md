## ProgramFactory

`ProgramFactory::GetOrCreateProgram` hashes the semantic program plus compile flags, then caches a reflected Vulkan program. A cache miss transforms each generated SPIR-V module before creating shader modules:

- surface pre-transform position fixups;
- explicit LOD 0 sampling variants;
- invariant position decoration for multi-pass depth equality;
- GL `gl_InstanceID` rebasing when shader draw parameters are available;
- storage-image format adjustments where the device permits unformatted float access;
- descriptor binding remapping into the backend binding domain.

Reflection extracts vertex inputs, fragment outputs, uniform blocks, sampled images, storage images, texel buffers and storage buffers. It then creates descriptor-set and pipeline layouts. Reflection is the contract used by `UniformManager`; it is not optional metadata.

## UniformManager

The manager translates GL-style bindings into Vulkan descriptor writes:

| GL-side resource | Vulkan representation |
| --- | --- |
| Global/regular UBO | Dynamic uniform-buffer descriptor plus offset |
| Sampler + texture | Combined image sampler |
| `samplerBuffer` | Uniform texel-buffer view |
| SSBO | Storage-buffer descriptor |
| Image unit | Storage-image view and layout |

Transient UBO data is uploaded into aligned frame-local storage. Descriptor set cursors are rewound only after the frame-slot fence proves old sets are idle. Descriptor signatures can reuse a set while dynamic offsets change independently.

## Pipeline key

`PipelineFactory` hashes the static inputs that Vulkan bakes into a graphics pipeline: program hash, vertex-input hash, render-pass hash, topology, primitive restart, rasterization, depth/stencil state, blend/write state, multisample state and relevant render-state versions. Viewport, scissor, blend constants, depth bias, line width and stencil references remain dynamic.

The renderer has a last-pipeline fast path for adjacent identical draws. The factory owns the durable content-addressed cache and ages entries at frame boundaries. If a render pass or program is evicted/recreated, dependent pipelines are invalidated; the renderer also drops its last-handle memo when eviction could make it stale.

## Why shader, descriptor and pipeline caches are coupled

A shader transform flag can change descriptor layout; a descriptor layout determines pipeline layout; a render pass determines attachment formats and blend state; vertex input determines attribute descriptions. A cache hit is valid only when all hashes describe the same Vulkan contract. Treating “same GL program” as sufficient would produce incompatible pipeline layouts or descriptors.
