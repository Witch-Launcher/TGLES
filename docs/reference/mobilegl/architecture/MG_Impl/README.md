
`MG_Impl` is the API-facing layer. It exposes ABI symbols, converts host handles, validates desktop OpenGL/EGL rules, mutates `MG_State`, and forwards only validated operations to `MG_Backend`.

## Directory layout

```text
MG_Impl/
├── EGLImpl/
│   ├── EGLImpl.cpp                   implementation functions
│   └── Exporting/Definitions.cpp     exported egl* symbols
├── GLImpl/
│   ├── Exporting/Definitions.cpp     exported gl* symbols
│   ├── Drawing/                       draw/dispatch commands
│   ├── Buffer/, Texture/, Program/    object API families
│   ├── Framebuffer/, VertexArray/     attachment and input APIs
│   └── Getter/, Query/, Sync/         queries and synchronization
├── GLXImpl/                           Linux GLX lookup and bridge
├── WGLImpl/                           Windows WGL bridge
├── CGLImpl/, NSOpenGLImpl/            macOS bridges
└── GetProcAddress.*                   core/extension lookup
```

## The exported-function pattern

The `Definitions.cpp` files are intentionally thin. They expose the symbol expected by an application and call the implementation namespace. The implementation then obtains `MG_State::pGLContext` or `pEGLContext`, validates arguments, and either mutates state or calls the backend function table.

For a draw:

```text
application glDrawElements
  -> GL exported definition
  -> MG_Impl::GLImpl::DrawElements
  -> ValidateCurrentProgramForExecution
  -> ValidatePrimitiveModeForBackend
  -> gBackendFunctionsTable.GL.DrawElements
```

`GL_Drawing.cpp` rejects an absent/unlinked current program and rejects a core-profile VAO 0 draw unless relaxed semantics are active. It also contains backend-sensitive checks such as DirectVulkan's current `GL_LINE_LOOP` limitation. Native APIs are not used as the source of GL error behavior.

## API family responsibilities

`GLImpl` is split by GL object family so validation and state mutation remain close to the API. `GL_Texture.cpp` validates target/format/level/pixel-store relationships and updates `TextureState`; `GL_Buffer.cpp` updates `BufferObject` storage/mapping and invokes buffer backend operations; `GL_Program.cpp` manages shader attachment/link/reflection metadata; `GL_Framebuffer.cpp` checks attachment completeness inputs; `GL_Drawing.cpp` builds draw payloads; getter modules read state or backend dynamic parameters.

The implementation layer can query the active backend for dynamic limits and format capabilities, but it does not decide how a `VkImage`, GLES scratch FBO or pipeline is built. That is the `MG_Backend` boundary.

## Host frontends

See [Host frontends](host-frontends.md) for the platform-specific context paths. All of them converge on the EGL state/backend path so context currentness and surface lifetime have one semantic owner.
