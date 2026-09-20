// Real Apple Metal bridge (OBJCXX, iOS + macOS SDK).
// Host unit tests use MockMetalBridge so CI without a GPU stays
// deterministic; the device test (tests/test_metal_device.mm) drives this
// class on a real MTLDevice and asserts real pixels. Every MTL call below
// uses signatures verified against the macOS 26.2 SDK headers
// (MTLDevice.h, MTLCommandQueue.h, MTLLibrary.h, CAMetalLayer.h) and probed
// live on host Metal (Intel KBL): MSL compiles, PSO needs a vertex
// descriptor when the vertex function takes attributes, offscreen
// render+blit round-trips exact pixels, headless nextDrawable succeeds.

#import <Foundation/Foundation.h>
#if __has_include(<Metal/Metal.h>)
#import <Metal/Metal.h>
#define TGLES_HAS_METAL 1
#else
#define TGLES_HAS_METAL 0
#endif
#if __has_include(<QuartzCore/QuartzCore.h>)
#import <QuartzCore/QuartzCore.h>
#define TGLES_HAS_QUARTZ 1
#else
#define TGLES_HAS_QUARTZ 0
#endif

#include <cstring>
#include <map>

#include "tgles/metal_bridge_apple.h"
#include "tgles/metal_mapping.h"

namespace tgles {
namespace metal_bridge {
namespace {

#if TGLES_HAS_METAL

// MSL twin of msl::CorrectedTriangleVertexShader(): varyings travel in a
// struct (never "thread T& ... [[user]]" parameters). Vertex attribute
// storage lives at buffer index 1 so index 0 stays free for Uniforms.
static NSString* kBridgeMsl =
    @"#include <metal_stdlib>\n"
    @"using namespace metal;\n"
    @"struct VertexIn { float4 position [[attribute(0)]]; float4 color "
    @"[[attribute(1)]]; };\n"
    @"struct Uniforms { float4x4 u_modelViewProj; };\n"
    @"struct Varyings { float4 position [[position]]; float4 color "
    @"[[user(locn0)]]; };\n"
    @"vertex Varyings vert_main(VertexIn in [[stage_in]], constant Uniforms& "
    @"uni [[buffer(0)]]) {\n"
    @"  Varyings out; out.position = uni.u_modelViewProj * in.position; "
    @"out.color = in.color; return out; }\n"
    @"fragment float4 frag_main(Varyings in [[stage_in]]) { return in.color; "
    @"}\n";

class AppleMetalBridge : public MetalBridge {
 public:
  AppleMetalBridge() {
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                        0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(mvp_, kIdentity, sizeof(mvp_));
  }

  bool Initialize(const char* family) override {
    if (family == nullptr || metal::FindGpuFamily(family) == nullptr) {
      errors_.Record(kGlInvalidEnum);
      return false;
    }
    if (initialized_) {
      family_ = family;
      return true;
    }
    device_ = MTLCreateSystemDefaultDevice();
    if (device_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    queue_ = [device_ newCommandQueue];
    if (queue_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    family_ = family;
    initialized_ = true;
    return true;
  }

  bool IsInitialized() const override { return initialized_; }

  std::uint32_t CreateRenderPipeline(const backend::PsoKey& key) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) return it->second.handle;
    id<MTLRenderPipelineState> pso = CompileDefaultPipeline(false);
    if (pso == nil) return 0;  // Error already recorded.
    const std::uint32_t handle = next_handle_++;
    pipelines_[key] = Entry{handle, pso};
    return handle;
  }

  std::uint32_t CreateBuffer(GLsizeiptr size,
                             backend::StorageMode mode) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    if (size <= 0) {
      errors_.Record(kGlInvalidValue);
      return 0;
    }
    MTLResourceOptions options =
        (mode == backend::StorageMode::kShared)
            ? MTLResourceStorageModeShared
            : MTLResourceStorageModePrivate;
    id<MTLBuffer> buf = [device_ newBufferWithLength:(NSUInteger)size
                                             options:options];
    if (buf == nil) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    const std::uint32_t handle = next_handle_++;
    buffers_[handle] = buf;
    return handle;
  }

  std::uint32_t CreateTexture(GLsizei width, GLsizei height,
                              GLenum internalformat,
                              const char* family) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    if (width <= 0 || height <= 0) {
      errors_.Record(kGlInvalidValue);
      return 0;
    }
    if (!backend::ResourcePlan::IsFormatSupported(internalformat, family)) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:(NSUInteger)width
                                    height:(NSUInteger)height
                                 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    const std::uint32_t handle = next_handle_++;
    textures_[handle] = tex;
    return handle;
  }

  bool BeginFrame(GLsizei width, GLsizei height) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    if (width <= 0 || height <= 0 || frame_open_) {
      errors_.Record(width <= 0 || height <= 0 ? kGlInvalidValue
                                               : kGlInvalidOperation);
      return false;
    }
    // The bridge owns one RGBA8 Shared offscreen target per frame size
    // (FBO-attachment mapping from app FBOs is the next milestone; the
    // target contract — size, format, Shared — already matches it).
    if (target_ == nil || target_width_ != width ||
        target_height_ != height) {
      MTLTextureDescriptor* td = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                       width:(NSUInteger)width
                                      height:(NSUInteger)height
                                   mipmapped:NO];
      td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      td.storageMode = MTLStorageModeShared;
      target_ = [device_ newTextureWithDescriptor:td];
      if (target_ == nil) {
        errors_.Record(kGlInvalidOperation);
        return false;
      }
      target_width_ = width;
      target_height_ = height;
    }
    cmd_ = [queue_ commandBuffer];
    if (cmd_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    // Depth target lives and dies with the color target size. Private
    // storage: the GPU is its only reader/writer (never presented, never
    // read back to the CPU).
    if (depth_cfg_.enabled) {
      if (depth_target_ == nil || depth_width_ != width ||
          depth_height_ != height) {
        MTLTextureDescriptor* dd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                         width:(NSUInteger)width
                                        height:(NSUInteger)height
                                     mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        depth_target_ = [device_ newTextureWithDescriptor:dd];
        if (depth_target_ == nil) {
          errors_.Record(kGlInvalidOperation);
          return false;
        }
        depth_width_ = width;
        depth_height_ = height;
      }
    } else if (depth_target_ != nil) {
      depth_target_ = nil;
      depth_width_ = 0;
      depth_height_ = 0;
    }
    frame_open_ = true;
    render_open_ = false;
    committed_ = false;
    draws_ = 0;
    // NOTE: vertex/MVP bindings persist across frames (GL VAO/uniform
    // semantics); only per-frame encoder progress resets here.
    return true;
  }

  void BeginRenderPass() override {
    if (!frame_open_ || render_open_ || committed_ || cmd_ == nil ||
        target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    const bool want_depth = depth_cfg_.enabled;
    if (want_depth && depth_target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target_;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLDepthStencilState> dss = nil;
    if (want_depth) {
      // Depth is cleared at pass start (loadAction Clear): the bridge has
      // no standalone glClear path, so every depth frame starts from the
      // configured clear value. Out-of-range clears clamp into [0, 1]
      // (NaN becomes 1.0); Metal leaves them undefined otherwise.
      rp.depthAttachment.texture = depth_target_;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      double cd = (double)depth_cfg_.clear_depth;
      if (!(cd >= 0.0 && cd <= 1.0)) cd = (cd < 0.0) ? 0.0 : 1.0;
      rp.depthAttachment.clearDepth = cd;
      rp.depthAttachment.storeAction = MTLStoreActionDontCare;
      dss = EnsureDepthStencilState();
      if (dss == nil) return;  // Error already recorded.
    }
    encoder_ = [cmd_ renderCommandEncoderWithDescriptor:rp];
    if (encoder_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    // A pipeline must exist before drawing; lazily compile the default plan
    // so a bare BeginFrame/BeginRenderPass/Draw sequence renders.
    id<MTLRenderPipelineState> use_pso = nil;
    if (want_depth) {
      if (depth_pso_ == nil) {
        depth_pso_ = CompileDefaultPipeline(true);
        if (depth_pso_ == nil) return;  // Error already recorded.
      }
      use_pso = depth_pso_;
    } else {
      if (bound_pso_ == nil) {
        const std::uint32_t h = CreateRenderPipeline(backend::PsoKey());
        if (h == 0) return;  // Error already recorded.
        bound_pso_ = pipelines_[backend::PsoKey()].state;
      }
      use_pso = bound_pso_;
    }
    [encoder_ setRenderPipelineState:use_pso];
    if (dss != nil) [encoder_ setDepthStencilState:dss];
    render_open_ = true;
  }

  void SetVertexBytes(const void* data, std::size_t bytes,
                      std::uint32_t stride) override {
    if (data == nullptr || bytes == 0 || stride == 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    vertex_bytes_.assign(static_cast<const std::uint8_t*>(data),
                         static_cast<const std::uint8_t*>(data) + bytes);
    vertex_stride_ = stride;
    has_vertex_ = true;
  }

  void SetMVP(const float mvp[16]) override {
    if (mvp == nullptr) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    std::memcpy(mvp_, mvp, sizeof(mvp_));
  }

  void ConfigureDepth(const DepthConfig& config) override {
    // GL compare funcs NEVER..ALWAYS (0x0200..0x0207) map 1:1 onto
    // MTLCompareFunctionNever..Always (0..7, verified in MTLDepthStencil.h
    // of the macOS 26.2 SDK).
    if (config.func < kGlNever || config.func > kGlAlways) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
    depth_cfg_ = config;
  }

  bool DepthEnabled() const override {
    return initialized_ && depth_cfg_.enabled;
  }

  // Shared MSL library for both pipeline variants.
  bool EnsureLibrary() {
    if (library_ != nil) return true;
    NSError* err = nil;
    library_ = [device_ newLibraryWithSource:kBridgeMsl
                                     options:nil
                                       error:&err];
    if (library_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    return true;
  }

  // Compiles the default color-only (with_depth=false) or depth-attached
  // pipeline. The vertex layout matches the bridge MSL VertexIn
  // (float4 position @0 + float4 color @1, stride bytes, buffer index 1).
  id<MTLRenderPipelineState> CompileDefaultPipeline(bool with_depth) {
    if (!EnsureLibrary()) return nil;
    id<MTLFunction> vf = [library_ newFunctionWithName:@"vert_main"];
    id<MTLFunction> ff = [library_ newFunctionWithName:@"frag_main"];
    if (vf == nil || ff == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    MTLRenderPipelineDescriptor* desc =
        [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vf;
    desc.fragmentFunction = ff;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (with_depth) {
      // A depth-attached draw needs its own PSO: the depth pixel format is
      // baked into the pipeline state, so the plain and depth pipelines
      // cannot share one object.
      desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    }
    // The vertex function takes attributes, so the driver requires a vertex
    // descriptor (probed failure message without it). Attribute storage is
    // bound at index 1, keeping index 0 for the Uniforms constant buffer.
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat4;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 1;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 16;
    vd.attributes[1].bufferIndex = 1;
    vd.layouts[1].stride = vertex_stride_;
    desc.vertexDescriptor = vd;
    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [device_ newRenderPipelineStateWithDescriptor:desc error:&err];
    if (pso == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    return pso;
  }

  // Depth-stencil state for the configured func + write mask, cached while
  // the pair is unchanged (creating one per frame would churn).
  id<MTLDepthStencilState> EnsureDepthStencilState() {
    if (depth_state_ != nil && depth_state_func_ == depth_cfg_.func &&
        depth_state_write_ == depth_cfg_.write_mask) {
      return depth_state_;
    }
    MTLDepthStencilDescriptor* dd =
        [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction =
        (MTLCompareFunction)(depth_cfg_.func - kGlNever);
    dd.depthWriteEnabled = depth_cfg_.write_mask ? YES : NO;
    depth_state_ = [device_ newDepthStencilStateWithDescriptor:dd];
    if (depth_state_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    depth_state_func_ = depth_cfg_.func;
    depth_state_write_ = depth_cfg_.write_mask;
    return depth_state_;
  }

  void Draw() override {
    if (!frame_open_ || !render_open_ || committed_ || encoder_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (!has_vertex_ || vertex_bytes_.empty()) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    id<MTLBuffer> vbuf =
        [device_ newBufferWithBytes:vertex_bytes_.data()
                             length:vertex_bytes_.size()
                            options:MTLResourceStorageModeShared];
    if (vbuf == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    [encoder_ setVertexBuffer:vbuf offset:0 atIndex:1];
    [encoder_ setVertexBytes:mvp_
                     length:sizeof(mvp_)
                    atIndex:0];
    [encoder_ drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:vertex_bytes_.size() / vertex_stride_];
    ++draws_;
  }

  void EndRenderPass() override {
    if (!frame_open_ || !render_open_ || encoder_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    [encoder_ endEncoding];
    encoder_ = nil;
    render_open_ = false;
  }

  void Blit() override {
    if (!frame_open_ || committed_ || render_open_ || cmd_ == nil ||
        target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
    if (blit == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    // No-op barrier blit proving encoder creation on the frame command
    // buffer (the ReadPixels-model copy lives in BlitToCpu).
    [blit endEncoding];
  }

  bool CommitFrame() override {
    // Commit is intentionally deferred to Present(): Metal requires
    // presentDrawable: before commit, mirroring MobileGL's fence-before-swap
    // ordering (DirectGLES.cpp Present). CommitFrame only seals the frame.
    if (!frame_open_ || render_open_ || committed_ || cmd_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    committed_ = true;
    frame_open_ = false;
    return true;
  }

  bool Committed() const override { return committed_; }
  GLuint DrawCount() const override { return draws_; }

  bool ReadbackPixel(GLint x, GLint y,
                     std::uint8_t out_rgba[4]) override {
    if (out_rgba == nullptr) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    if (target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    if (x < 0 || y < 0 || x >= target_width_ || y >= target_height_) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    // The target is Shared: wait for the presenting frame, then read.
    WaitForCompletion(frame_serial_);
    uint8_t bgra[4] = {0};
    [target_ getBytes:bgra
          bytesPerRow:(NSUInteger)(target_width_ * 4)
           fromRegion:MTLRegionMake2D((NSUInteger)x, (NSUInteger)y, 1, 1)
          mipmapLevel:0];
    out_rgba[0] = bgra[2];
    out_rgba[1] = bgra[1];
    out_rgba[2] = bgra[0];
    out_rgba[3] = bgra[3];
    return true;
  }

  bool BlitToCpu(void* dst, std::size_t bytes) override {
    if (dst == nullptr) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    if (target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    const std::size_t need =
        static_cast<std::size_t>(target_width_ * target_height_ * 4);
    if (bytes < need) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    id<MTLBuffer> staging = [device_
        newBufferWithLength:need
                    options:MTLResourceStorageModeShared];
    if (staging == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    if (blit == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    [blit copyFromTexture:target_
             sourceSlice:0
             sourceLevel:0
            sourceOrigin:MTLOriginMake(0, 0, 0)
              sourceSize:MTLSizeMake((NSUInteger)target_width_,
                                     (NSUInteger)target_height_, 1)
                toBuffer:staging
       destinationOffset:0
      destinationBytesPerRow:(NSUInteger)(target_width_ * 4)
    destinationBytesPerImage:need];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if ([cb status] != MTLCommandBufferStatusCompleted) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    std::memcpy(dst, [staging contents], need);
    // The bridge target is BGRA8Unorm but the CPU contract is RGBA:
    // swizzle in place (matches ReadbackPixel).
    auto* px = static_cast<std::uint8_t*>(dst);
    for (std::size_t i = 0; i < need; i += 4) {
      const std::uint8_t b = px[i];
      px[i] = px[i + 2];
      px[i + 2] = b;
    }
    return true;
  }

  bool WaitForCompletion(std::uint64_t serial) override {
    if (serial > frame_serial_) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    auto it = inflight_.find(serial);
    if (it != inflight_.end() && it->second != nil) {
      [it->second waitUntilCompleted];
      if ([it->second status] != MTLCommandBufferStatusCompleted) {
        errors_.Record(kGlInvalidOperation);
        return false;
      }
      inflight_.erase(it);
    }
    if (serial > completed_serial_) completed_serial_ = serial;
    return true;
  }

  void SetLayer(void* ca_metal_layer, GLsizei width,
                GLsizei height) override {
    layer_ = ca_metal_layer;
    layer_width_ = width;
    layer_height_ = height;
#if TGLES_HAS_QUARTZ
    if (layer_ != nullptr) {
      CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_;
      layer.device = device_;
      layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
      layer.drawableSize = CGSizeMake(width, height);
    }
#endif
  }

  void Resize(GLsizei width, GLsizei height) override {
    layer_width_ = width;
    layer_height_ = height;
#if TGLES_HAS_QUARTZ
    if (layer_ != nullptr) {
      CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_;
      layer.drawableSize = CGSizeMake(width, height);
    }
#endif
  }

  bool Present() override {
    if (layer_ == nullptr || layer_width_ <= 0 || layer_height_ <= 0) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    if (!committed_ || cmd_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
#if TGLES_HAS_QUARTZ
    CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_;
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (drawable == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    // Copy the offscreen render target into the drawable so a real window
    // shows the frame (the trial only reads back via BlitToCpu, but a
    // CAMetalLayer-backed NSView needs drawable pixels). Sizes match when
    // the app keeps SetLayer/Resize in sync with the FBO; on mismatch copy
    // the overlapping origin region instead of failing.
    if (target_ != nil && drawable.texture != nil) {
      id<MTLBlitCommandEncoder> copy = [cmd_ blitCommandEncoder];
      if (copy != nil) {
        const NSUInteger dw = drawable.texture.width;
        const NSUInteger dh = drawable.texture.height;
        const NSUInteger cw =
            (NSUInteger)target_width_ < dw ? (NSUInteger)target_width_ : dw;
        const NSUInteger ch =
            (NSUInteger)target_height_ < dh ? (NSUInteger)target_height_ : dh;
        if (cw > 0 && ch > 0) {
          [copy copyFromTexture:target_
                    sourceSlice:0
                    sourceLevel:0
                   sourceOrigin:MTLOriginMake(0, 0, 0)
                     sourceSize:MTLSizeMake(cw, ch, 1)
                      toTexture:drawable.texture
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        [copy endEncoding];
      }
    }
    // presentDrawable: must precede commit — the fence-before-swap analogue.
    [cmd_ presentDrawable:drawable];
#endif
    [cmd_ commit];
    ++frame_serial_;
    ++swaps_;
    // completed_serial_ advances only in WaitForCompletion (async ring, like
    // MobileGL polling prior fences AFTER the swap).
    inflight_[frame_serial_] = cmd_;
    cmd_ = nil;
    committed_ = false;
    return true;
  }

  std::uint64_t FrameSerial() const override { return frame_serial_; }
  std::uint64_t CompletedSerial() const override { return completed_serial_; }
  std::uint64_t SwapCount() const override { return swaps_; }
  GLenum GetError() override { return errors_.Get(); }

 private:
  struct Entry {
    std::uint32_t handle = 0;
    id<MTLRenderPipelineState> state = nil;
  };
  ErrorQueue errors_;
  bool initialized_ = false;
  std::string family_;
  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLLibrary> library_ = nil;
  id<MTLCommandBuffer> cmd_ = nil;
  id<MTLRenderCommandEncoder> encoder_ = nil;
  id<MTLRenderPipelineState> bound_pso_ = nil;
  id<MTLTexture> target_ = nil;
  GLsizei target_width_ = 0;
  GLsizei target_height_ = 0;
  DepthConfig depth_cfg_;
  id<MTLTexture> depth_target_ = nil;
  GLsizei depth_width_ = 0;
  GLsizei depth_height_ = 0;
  id<MTLDepthStencilState> depth_state_ = nil;
  GLenum depth_state_func_ = 0;  // 0 caches nothing (NEVER is 0x0200).
  bool depth_state_write_ = false;
  id<MTLRenderPipelineState> depth_pso_ = nil;
  std::map<backend::PsoKey, Entry> pipelines_;
  std::map<std::uint32_t, id<MTLBuffer>> buffers_;
  std::map<std::uint32_t, id<MTLTexture>> textures_;
  std::map<std::uint64_t, id<MTLCommandBuffer>> inflight_;
  std::uint32_t next_handle_ = 1;
  bool frame_open_ = false;
  bool render_open_ = false;
  bool committed_ = false;
  GLuint draws_ = 0;
  std::vector<std::uint8_t> vertex_bytes_;
  std::uint32_t vertex_stride_ = 32;
  float mvp_[16];
  bool has_vertex_ = false;
  void* layer_ = nullptr;
  GLsizei layer_width_ = 0;
  GLsizei layer_height_ = 0;
  std::uint64_t frame_serial_ = 0;
  std::uint64_t completed_serial_ = 0;
  std::uint64_t swaps_ = 0;
};
#endif  // TGLES_HAS_METAL

}  // namespace

std::unique_ptr<MetalBridge> CreateAppleBridge() {
#if TGLES_HAS_METAL
  return std::unique_ptr<MetalBridge>(new AppleMetalBridge());
#else
  return nullptr;
#endif
}

}  // namespace metal_bridge
}  // namespace tgles
