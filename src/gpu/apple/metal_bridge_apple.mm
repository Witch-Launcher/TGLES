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

#include <cstdio>
#include <cstring>
#include <map>

#include "tgles/gpu/metal_bridge_apple.h"
#include "tgles/gpu/metal_mapping.h"
#include "tgles/gpu/metal_translate.h"

namespace tgles {
namespace metal_bridge {
namespace {

#if TGLES_HAS_METAL

// MSL twin of msl::CorrectedTriangleVertexShader(): varyings travel in a
// struct (never "thread T& ... [[user]]" parameters). Vertex attribute
// storage lives at buffer index 1 so index 0 stays free for Uniforms.
// GL depth semantics: clip z is remapped z*0.5+w*0.5 (GL NDC [-1,1] onto
// Metal NDC [0,1], the same 1:1 translators like ANGLE apply), so apps
// project exactly like on a GL driver; order-preserving, LESS still wins.
//
// Textured pair (ES 3.2 §8 honest v1): vert_tex_main passes uv@2 through,
// frag_tex_main samples texture(0)/sampler(0) and multiplies by vertex
// color (v_col * texture). Compute kernel add_one backs the honest compute
// subset (SSBO0 uint32 += 1).
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
    @"  Varyings out; float4 clip = uni.u_modelViewProj * in.position; "
    @"clip.z = clip.z * 0.5 + clip.w * 0.5; out.position = clip; "
    @"out.color = in.color; return out; }\n"
    @"fragment float4 frag_main(Varyings in [[stage_in]]) { return in.color; "
    @"}\n"
    @"struct VertexInTex { float4 position [[attribute(0)]]; float4 color "
    @"[[attribute(1)]]; float2 uv [[attribute(2)]]; };\n"
    @"struct VaryingsTex { float4 position [[position]]; float4 color "
    @"[[user(locn0)]]; float2 uv [[user(locn1)]]; };\n"
    @"vertex VaryingsTex vert_tex_main(VertexInTex in [[stage_in]], constant "
    @"Uniforms& uni [[buffer(0)]]) {\n"
    @"  VaryingsTex out; float4 clip = uni.u_modelViewProj * in.position; "
    @"clip.z = clip.z * 0.5 + clip.w * 0.5; out.position = clip; "
    @"out.color = in.color; out.uv = in.uv; return out; }\n"
    @"fragment float4 frag_tex_main(VaryingsTex in [[stage_in]], texture2d<float> "
    @"tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    @"  float4 t = tex.sample(smp, in.uv); return in.color * t; }\n"
    @"kernel void compute_add_one(device const uint* vin [[buffer(0)]], device "
    @"uint* vout [[buffer(1)]], uint gid [[thread_position_in_grid]]) {\n"
    @"  vout[gid] = vin[gid] + 1u; }\n";

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
    // Type-1 path: the descriptor bakes the key's blend state (factors,
    // equations, write mask) via metal_translate plus the depth/stencil
    // pixel formats matching this frame (see CompileKeyedPipeline), so
    // depth+blend keys compile to real blended-depth PSOs.
    // Stencil-enabled draws need a stencil-format pipeline: they live in a
    // separate cache keyed by blend key + stencil signature, because the
    // pipeline depth/stencil pixel formats must match the render pass.
    if (stencil_cfg_.enabled) {
      const std::string sig = StencilSignature(key);
      auto st = stencil_pipelines_.find(sig);
      if (st != stencil_pipelines_.end()) return st->second.handle;
      id<MTLRenderPipelineState> pso = CompileKeyedPipeline(key, true);
      if (pso == nil) return 0;  // Error already recorded.
      const std::uint32_t handle = next_handle_++;
      stencil_pipelines_[sig] = Entry{handle, pso, key};
      stencil_key_textured_[sig] = key.textured;
      stencil_key_sampler_[sig] = key.textured && !key.translated;
      stencil_keys_[sig] = key;
      return handle;
    }
    id<MTLRenderPipelineState> pso = CompileKeyedPipeline(key, false);
    if (pso == nil) return 0;  // Error already recorded.
    const std::uint32_t handle = next_handle_++;
    pipelines_[key] = Entry{handle, pso, key};
    handle_uses_sampler_[handle] = key.textured && !key.translated;
    return handle;
  }

  void BindRenderPipeline(std::uint32_t handle) override {
    if (!initialized_ || handle == 0) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    for (const auto& kv : pipelines_) {
      if (kv.second.handle == handle) {
        bound_pso_ = kv.second.state;
        bound_textured_ = kv.second.key.textured;
        // Sampler need: translated pipelines record it per handle (layout
        // and sampling decouple there); legacy textured keys imply it.
        auto sit = handle_uses_sampler_.find(handle);
        bound_needs_texture_ = (sit != handle_uses_sampler_.end())
                                   ? sit->second
                                   : kv.second.key.textured;
        bound_mrt_count_ = kv.second.key.mrt_count;
        bound_samples_ = kv.second.key.sample_count;
        return;
      }
    }
    for (const auto& kv : stencil_pipelines_) {
      if (kv.second.handle == handle) {
        bound_pso_ = kv.second.state;
        // Stencil entries store the full key alongside (see creation).
        auto kit = stencil_keys_.find(kv.first);
        const backend::PsoKey& bk =
            (kit != stencil_keys_.end()) ? kit->second : kv.second.key;
        bound_textured_ = bk.textured;
        auto sit = stencil_key_sampler_.find(kv.first);
        bound_needs_texture_ = (sit != stencil_key_sampler_.end())
                                   ? sit->second
                                   : bound_textured_;
        bound_mrt_count_ = bk.mrt_count;
        bound_samples_ = bk.sample_count;
        return;
      }
    }
    errors_.Record(kGlInvalidOperation);  // Unknown handle: fail closed.
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
    // The bridge owns RGBA8 Shared offscreen targets per frame size, one
    // per MRT attachment (count from the bound key, explicit SetMrtCount
    // fallback for bare-bridge use). Attachment 0 aliases target_ so all
    // single-target code (readback, blit, present) is untouched.
    const GLsizei want_mrt =
        (bound_pso_ != nil) ? bound_mrt_count_ : mrt_count_;
    const GLsizei mrt_n =
        (want_mrt >= 1 && want_mrt <= 8) ? want_mrt : 1;
    if (target_ == nil || target_width_ != width ||
        target_height_ != height ||
        (GLsizei)mrt_targets_.size() != mrt_n) {
      mrt_targets_.clear();
      for (GLsizei i = 0; i < mrt_n; ++i) {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:(NSUInteger)width
                                        height:(NSUInteger)height
                                     mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> t = [device_ newTextureWithDescriptor:td];
        if (t == nil) {
          errors_.Record(kGlInvalidOperation);
          return false;
        }
        mrt_targets_.push_back(t);
      }
      target_ = mrt_targets_[0];
      target_width_ = width;
      target_height_ = height;
    }
    // MSAA (spec 14.x) + MRT+MSAA (Phase 4 item 4): when the bound key
    // carries samples>1, EACH MRT attachment renders into its own
    // multisample texture that resolves into its target at pass end
    // (StoreAndMultisampleResolve per attachment). Depth rides a matching
    // multisample depth texture (Metal requires equal sample counts).
    const GLsizei want_samples =
        (bound_pso_ != nil) ? bound_samples_ : 0;
    const NSUInteger msaa_n =
        (want_samples <= 1) ? 0 : (want_samples <= 2 ? 2 : 4);
    if (msaa_n > 0) {
      if (msaa_targets_.size() != (std::size_t)mrt_n || msaa_target_ == nil ||
          msaa_width_ != width || msaa_height_ != height) {
        msaa_targets_.clear();
        for (GLsizei i = 0; i < mrt_n; ++i) {
          MTLTextureDescriptor* md = [MTLTextureDescriptor
              texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                           width:(NSUInteger)width
                                          height:(NSUInteger)height
                                       mipmapped:NO];
          md.textureType = MTLTextureType2DMultisample;
          md.sampleCount = msaa_n;
          md.usage = MTLTextureUsageRenderTarget;
          md.storageMode = MTLStorageModePrivate;
          id<MTLTexture> mt = [device_ newTextureWithDescriptor:md];
          if (mt == nil) {
            errors_.Record(kGlInvalidOperation);
            fprintf(stderr,
                    "[TGL-DEBUG] MRT+MSAA: msaa target %d alloc failed\n", i);
            return false;
          }
          msaa_targets_.push_back(mt);
        }
        msaa_target_ = msaa_targets_[0];
        msaa_width_ = width;
        msaa_height_ = height;
        // Size changed under the depth companions: drop them so the blocks
        // below rebuild at the new size instead of reusing stale textures.
        msaa_depth_ = nil;
        msaa_ds_ = nil;
      }
    } else if (!msaa_targets_.empty() || msaa_target_ != nil) {
      msaa_targets_.clear();
      msaa_target_ = nil;
      msaa_depth_ = nil;
      msaa_ds_ = nil;
      msaa_width_ = 0;
      msaa_height_ = 0;
    }
    cmd_ = [queue_ commandBuffer];
    if (cmd_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    // Depth target lives and dies with the color target size. Private
    // storage: the GPU is its only reader/writer (never presented, never
    // read back to the CPU). With stencil on, a combined Depth32Float_Stencil8
    // texture serves both attachments (ds_target_); depth-only keeps the
    // Depth32Float texture the depth tests were verified against.
    const bool want_stencil = stencil_cfg_.enabled;
    const bool want_msaa_depth =
        (msaa_target_ != nil) && (depth_cfg_.enabled || want_stencil);
    if (want_stencil) {
      if (ds_target_ == nil || ds_width_ != width || ds_height_ != height) {
        MTLTextureDescriptor* dd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                           width:(NSUInteger)width
                                          height:(NSUInteger)height
                                       mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        ds_target_ = [device_ newTextureWithDescriptor:dd];
        if (ds_target_ == nil) {
          errors_.Record(kGlInvalidOperation);
          return false;
        }
        ds_width_ = width;
        ds_height_ = height;
      }
      if (want_msaa_depth &&
          (msaa_ds_ == nil || msaa_width_ != width ||
           msaa_height_ != height)) {
        MTLTextureDescriptor* dd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                           width:(NSUInteger)width
                                          height:(NSUInteger)height
                                       mipmapped:NO];
        dd.textureType = MTLTextureType2DMultisample;
        dd.sampleCount = msaa_target_.sampleCount;
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        msaa_ds_ = [device_ newTextureWithDescriptor:dd];
        if (msaa_ds_ == nil) {
          errors_.Record(kGlInvalidOperation);
          return false;
        }
      }
    } else if (ds_target_ != nil) {
      ds_target_ = nil;
      ds_width_ = 0;
      ds_height_ = 0;
      msaa_ds_ = nil;
    }
    if (depth_cfg_.enabled && !want_stencil) {
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
      if (want_msaa_depth &&
          (msaa_depth_ == nil || msaa_width_ != width ||
           msaa_height_ != height)) {
        MTLTextureDescriptor* dd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                           width:(NSUInteger)width
                                          height:(NSUInteger)height
                                       mipmapped:NO];
        dd.textureType = MTLTextureType2DMultisample;
        dd.sampleCount = msaa_target_.sampleCount;
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        msaa_depth_ = [device_ newTextureWithDescriptor:dd];
        if (msaa_depth_ == nil) {
          errors_.Record(kGlInvalidOperation);
          return false;
        }
      }
    } else if (depth_target_ != nil) {
      depth_target_ = nil;
      depth_width_ = 0;
      depth_height_ = 0;
      msaa_depth_ = nil;
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
    const bool want_stencil = stencil_cfg_.enabled;
    id<MTLTexture> ds_tex = want_stencil ? ds_target_ : nil;
    if (want_depth && !want_stencil && depth_target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (want_stencil && ds_tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
    // MRT: attachments 0..N-1 from the sized targets (N from the bound key).
    // MSAA: EACH attachment renders into its own multisample texture and
    // resolves into its target (Phase 4 item 4 per-attachment resolve).
    const bool use_msaa = (!msaa_targets_.empty());
    const GLsizei pass_n =
        (bound_mrt_count_ > 1) ? bound_mrt_count_ : 1;
    for (GLsizei i = 0;
         i < pass_n && (std::size_t)i < mrt_targets_.size() && i < 8; ++i) {
      const bool resolve_this =
          use_msaa && (std::size_t)i < msaa_targets_.size();
      id<MTLTexture> msaa_tex =
          resolve_this ? msaa_targets_[i] : nil;
      rp.colorAttachments[i].texture =
          resolve_this ? msaa_tex : mrt_targets_[i];
      rp.colorAttachments[i].loadAction = MTLLoadActionClear;
      rp.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 0, 1);
      if (resolve_this) {
        rp.colorAttachments[i].resolveTexture = mrt_targets_[i];
        rp.colorAttachments[i].storeAction =
            MTLStoreActionStoreAndMultisampleResolve;
      } else {
        rp.colorAttachments[i].storeAction = MTLStoreActionStore;
      }
    }
    id<MTLDepthStencilState> dss = nil;
    if (want_depth) {
      // Depth is cleared at pass start (loadAction Clear): the bridge has
      // no standalone glClear path, so every depth frame starts from the
      // configured clear value. Out-of-range clears clamp into [0, 1]
      // (NaN becomes 1.0); Metal leaves them undefined otherwise.
      // With stencil on, depth rides the combined texture; with MSAA, the
      // multisample companion (contents never resolved: depth is GPU-only).
      id<MTLTexture> depth_tex = want_stencil ? ds_tex : depth_target_;
      if (use_msaa) {
        id<MTLTexture> msaa_d = want_stencil ? msaa_ds_ : msaa_depth_;
        if (msaa_d != nil) depth_tex = msaa_d;
      }
      rp.depthAttachment.texture = depth_tex;
      rp.depthAttachment.loadAction = MTLLoadActionClear;
      double cd = (double)depth_cfg_.clear_depth;
      if (!(cd >= 0.0 && cd <= 1.0)) cd = (cd < 0.0) ? 0.0 : 1.0;
      rp.depthAttachment.clearDepth = cd;
      rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    }
    if (want_stencil) {
      // Stencil clears at pass start like depth (same one-draw-per-frame
      // model); ref values ride the encoder, masks/ops ride the state.
      // MSAA uses the multisample companion, same DontCare reasoning.
      id<MTLTexture> sten_tex = ds_tex;
      if (use_msaa && msaa_ds_ != nil) sten_tex = msaa_ds_;
      rp.stencilAttachment.texture = sten_tex;
      rp.stencilAttachment.loadAction = MTLLoadActionClear;
      rp.stencilAttachment.clearStencil = (std::uint32_t)(
          stencil_cfg_.clear_stencil < 0 ? 0
          : stencil_cfg_.clear_stencil > 255 ? 255
                                              : stencil_cfg_.clear_stencil);
      rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
    }
    if (want_depth || want_stencil) {
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
    // Facade draws always Bind a keyed PSO first (compiled with live blend +
    // depth/stencil formats, textured variant included), so prefer it: that
    // is what makes depth+blend execute instead of being refused. The
    // depth_pso_/stencil_depth_pso_ fallbacks below serve bare-bridge use
    // (tests driving BeginFrame/BeginRenderPass/Draw with no Bind).
    const bool use_textured = bound_textured_;
    id<MTLRenderPipelineState> use_pso = nil;
    if (bound_pso_ != nil) {
      use_pso = bound_pso_;
    } else if (want_depth) {
      if (want_stencil) {
        // Combined depth+stencil needs its own PSO (stencil pixel format is
        // baked in); bare-bridge fallback carries no blend state.
        if (use_textured) {
          if (stencil_depth_tex_pso_ == nil) {
            stencil_depth_tex_pso_ = CompileStencilDepthPipeline(true);
            if (stencil_depth_tex_pso_ == nil) return;
          }
          use_pso = stencil_depth_tex_pso_;
        } else {
          if (stencil_depth_pso_ == nil) {
            stencil_depth_pso_ = CompileStencilDepthPipeline(false);
            if (stencil_depth_pso_ == nil) return;  // Error already recorded.
          }
          use_pso = stencil_depth_pso_;
        }
      } else {
        if (use_textured) {
          if (depth_tex_pso_ == nil) {
            depth_tex_pso_ = CompileDefaultPipeline(true, true);
            if (depth_tex_pso_ == nil) return;
          }
          use_pso = depth_tex_pso_;
        } else {
          if (depth_pso_ == nil) {
            depth_pso_ = CompileDefaultPipeline(true, false);
            if (depth_pso_ == nil) return;  // Error already recorded.
          }
          use_pso = depth_pso_;
        }
      }
    } else {
      if (bound_pso_ == nil) {
        const std::uint32_t h = CreateRenderPipeline(backend::PsoKey());
        if (h == 0) return;  // Error already recorded.
        // CreateRenderPipeline cached under the matching table (stencil keys
        // live in stencil_pipelines_); resolve the fresh handle to a state.
        for (const auto& kv : pipelines_) {
          if (kv.second.handle == h) bound_pso_ = kv.second.state;
        }
        for (const auto& kv : stencil_pipelines_) {
          if (kv.second.handle == h) bound_pso_ = kv.second.state;
        }
        if (bound_pso_ == nil) {
          errors_.Record(kGlInvalidOperation);
          return;
        }
      }
      use_pso = bound_pso_;
    }
    [encoder_ setRenderPipelineState:use_pso];
    // CONSTANT_* blend factors read this color; uploading the facade's
    // glBlendColor every pass keeps them exact instead of default-black.
    [encoder_ setBlendColorRed:blend_color_[0]
                        green:blend_color_[1]
                         blue:blend_color_[2]
                        alpha:blend_color_[3]];
    if (dss != nil) [encoder_ setDepthStencilState:dss];
    if (want_stencil) {
      [encoder_ setStencilReferenceValue:(std::uint32_t)(
          stencil_cfg_.ref_front < 0 ? 0
          : stencil_cfg_.ref_front > 255 ? 255
                                          : stencil_cfg_.ref_front)];
    }
    ApplyRasterEncoderState();
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

  void SetFragmentTexture(GLuint unit, GLsizei width, GLsizei height,
                          const void* rgba8) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (rgba8 == nullptr || width <= 0 || height <= 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:(NSUInteger)width
                                    height:(NSUInteger)height
                                 mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLRegion region =
        MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height);
    [tex replaceRegion:region
           mipmapLevel:0
             withBytes:rgba8
           bytesPerRow:(NSUInteger)(width * 4)];
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  void SetFragmentSampler(GLuint unit, bool linear, bool repeat) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = linear ? MTLSamplerMinMagFilterLinear
                          : MTLSamplerMinMagFilterNearest;
    sd.magFilter = linear ? MTLSamplerMinMagFilterLinear
                          : MTLSamplerMinMagFilterNearest;
    sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = repeat ? MTLSamplerAddressModeRepeat
                             : MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = repeat ? MTLSamplerAddressModeRepeat
                             : MTLSamplerAddressModeClampToEdge;
    sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> smp = [device_ newSamplerStateWithDescriptor:sd];
    if (smp == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    frag_smps_[unit] = smp;
    active_frag_unit_ = unit;
  }

  bool HasFragmentTexture(GLuint unit) const override {
    if (!initialized_) return false;
    auto it = frag_texs_.find(unit);
    return it != frag_texs_.end() && it->second != nil;
  }

  // Pixel format for sampling uploads: hardware sRGB decode for
  // SRGB8_ALPHA8 views/storage, plain RGBA8 otherwise (verified value 71 in
  // MTLPixelFormat.h of the macOS 26.2 SDK).
  MTLPixelFormat SamplingFormat(bool srgb) {
    return srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
  }

  void SetFragmentTextureCube(GLuint unit, GLsizei size,
                              const void* const* faces_rgba8,
                              bool srgb) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (faces_rgba8 == nullptr || size <= 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    for (int f = 0; f < 6; ++f) {
      if (faces_rgba8[f] == nullptr) {
        errors_.Record(kGlInvalidValue);
        return;
      }
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        textureCubeDescriptorWithPixelFormat:SamplingFormat(srgb)
                                        size:(NSUInteger)size
                                   mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    for (NSUInteger slice = 0; slice < 6; ++slice) {
      MTLRegion region =
          MTLRegionMake2D(0, 0, (NSUInteger)size, (NSUInteger)size);
      [tex replaceRegion:region
             mipmapLevel:0
                   slice:slice
                 withBytes:faces_rgba8[slice]
               bytesPerRow:(NSUInteger)(size * 4)
             bytesPerImage:(NSUInteger)(size * size * 4)];
    }
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  void SetFragmentTexture3D(GLuint unit, GLsizei width, GLsizei height,
                            GLsizei depth, const void* rgba8,
                            bool srgb) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (rgba8 == nullptr || width <= 0 || height <= 0 || depth <= 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    // NOTE: MTLTextureDescriptor has no 3D convenience constructor (verified
    // against MTLTexture.h in the macOS 26.2 SDK: only 2D/cube/buffer
    // factories exist); 3D goes through alloc/init + explicit textureType.
    MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
    td.textureType = MTLTextureType3D;
    td.pixelFormat = SamplingFormat(srgb);
    td.width = (NSUInteger)width;
    td.height = (NSUInteger)height;
    td.depth = (NSUInteger)depth;
    td.mipmapLevelCount = 1;
    td.arrayLength = 1;
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLRegion region = MTLRegionMake3D(0, 0, 0, (NSUInteger)width,
                                       (NSUInteger)height, (NSUInteger)depth);
    [tex replaceRegion:region
           mipmapLevel:0
                 slice:0
               withBytes:rgba8
             bytesPerRow:(NSUInteger)(width * 4)
           bytesPerImage:(NSUInteger)(width * height * 4)];
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  void SetFragmentTextureArray(GLuint unit, GLsizei width, GLsizei height,
                               GLsizei layers, const void* rgba8,
                               bool srgb) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (rgba8 == nullptr || width <= 0 || height <= 0 || layers <= 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:SamplingFormat(srgb)
                                     width:(NSUInteger)width
                                    height:(NSUInteger)height
                                 mipmapped:NO];
    td.textureType = MTLTextureType2DArray;
    td.arrayLength = (NSUInteger)layers;
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    const std::size_t slice_bytes =
        static_cast<std::size_t>(width) * height * 4;
    for (NSUInteger slice = 0; slice < (NSUInteger)layers; ++slice) {
      MTLRegion region =
          MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height);
      [tex replaceRegion:region
             mipmapLevel:0
                   slice:slice
                 withBytes:static_cast<const std::uint8_t*>(rgba8) +
                           slice * slice_bytes
               bytesPerRow:(NSUInteger)(width * 4)
             bytesPerImage:slice_bytes];
    }
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  void SetFragmentTextureMips(GLuint unit, GLsizei base_width,
                              GLsizei base_height, int levels,
                              const void* const* level_rgba8,
                              bool srgb) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (level_rgba8 == nullptr || base_width <= 0 || base_height <= 0 ||
        levels < 1) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    for (int l = 0; l < levels; ++l) {
      if (level_rgba8[l] == nullptr) {
        errors_.Record(kGlInvalidValue);
        return;
      }
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:SamplingFormat(srgb)
                                     width:(NSUInteger)base_width
                                    height:(NSUInteger)base_height
                                 mipmapped:(levels > 1)];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    for (int l = 0; l < levels; ++l) {
      const GLsizei w = std::max<GLsizei>(1, base_width >> l);
      const GLsizei h = std::max<GLsizei>(1, base_height >> l);
      MTLRegion region =
          MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h);
      [tex replaceRegion:region
             mipmapLevel:(NSUInteger)l
                 withBytes:level_rgba8[l]
               bytesPerRow:(NSUInteger)(w * 4)];
    }
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  // Phase 4 item 6: cube/3D/array mip chains. Layout: cube faces_per_level =
  // [L0*6, L1*6, ...]; 3D/array level_rgba8[l] contiguous (w*h*d*4, depth
  // halved). Sampler mipFilter comes from SetFragmentSamplerDetail.
  void SetFragmentTextureCubeMips(GLuint unit, GLsizei base_size, int levels,
                                  const void* const* faces_per_level,
                                  bool srgb) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (faces_per_level == nullptr || base_size <= 0 || levels < 1) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    for (int l = 0; l < levels * 6; ++l) {
      if (faces_per_level[l] == nullptr) {
        errors_.Record(kGlInvalidValue);
        return;
      }
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        textureCubeDescriptorWithPixelFormat:SamplingFormat(srgb)
                                        size:(NSUInteger)base_size
                                   mipmapped:(levels > 1)];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      fprintf(stderr, "[TGL-DEBUG] cube-mips alloc failed size %d levels %d\n",
              (int)base_size, levels);
      return;
    }
    for (int l = 0; l < levels; ++l) {
      const GLsizei s = std::max<GLsizei>(1, base_size >> l);
      MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)s, (NSUInteger)s);
      for (NSUInteger slice = 0; slice < 6; ++slice) {
        [tex replaceRegion:region
               mipmapLevel:(NSUInteger)l
                     slice:slice
                   withBytes:faces_per_level[l * 6 + slice]
                 bytesPerRow:(NSUInteger)(s * 4)
               bytesPerImage:(NSUInteger)(s * s * 4)];
      }
    }
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  void SetFragmentTexture3DMips(GLuint unit, GLsizei base_w, GLsizei base_h,
                                GLsizei base_d, int levels,
                                const void* const* level_rgba8,
                                bool srgb) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (level_rgba8 == nullptr || base_w <= 0 || base_h <= 0 || base_d <= 0 ||
        levels < 1) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    for (int l = 0; l < levels; ++l) {
      if (level_rgba8[l] == nullptr) {
        errors_.Record(kGlInvalidValue);
        return;
      }
    }
    MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
    td.textureType = MTLTextureType3D;
    td.pixelFormat = SamplingFormat(srgb);
    td.width = (NSUInteger)base_w;
    td.height = (NSUInteger)base_h;
    td.depth = (NSUInteger)base_d;
    td.mipmapLevelCount = (NSUInteger)levels;
    td.arrayLength = 1;
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      fprintf(stderr, "[TGL-DEBUG] 3D-mips alloc failed\n");
      return;
    }
    for (int l = 0; l < levels; ++l) {
      const GLsizei w = std::max<GLsizei>(1, base_w >> l);
      const GLsizei h = std::max<GLsizei>(1, base_h >> l);
      const GLsizei d = std::max<GLsizei>(1, base_d >> l);
      MTLRegion region =
          MTLRegionMake3D(0, 0, 0, (NSUInteger)w, (NSUInteger)h, (NSUInteger)d);
      [tex replaceRegion:region
             mipmapLevel:(NSUInteger)l
                   slice:0
                 withBytes:level_rgba8[l]
               bytesPerRow:(NSUInteger)(w * 4)
             bytesPerImage:(NSUInteger)(w * h * 4)];
    }
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  void SetFragmentTextureArrayMips(GLuint unit, GLsizei base_w, GLsizei base_h,
                                   GLsizei layers, int levels,
                                   const void* const* level_rgba8,
                                   bool srgb) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (level_rgba8 == nullptr || base_w <= 0 || base_h <= 0 || layers <= 0 ||
        levels < 1) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    for (int l = 0; l < levels; ++l) {
      if (level_rgba8[l] == nullptr) {
        errors_.Record(kGlInvalidValue);
        return;
      }
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:SamplingFormat(srgb)
                                     width:(NSUInteger)base_w
                                    height:(NSUInteger)base_h
                                 mipmapped:(levels > 1)];
    td.textureType = MTLTextureType2DArray;
    td.arrayLength = (NSUInteger)layers;
    td.mipmapLevelCount = (NSUInteger)levels;
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (tex == nil) {
      errors_.Record(kGlInvalidOperation);
      fprintf(stderr, "[TGL-DEBUG] array-mips alloc failed\n");
      return;
    }
    for (int l = 0; l < levels; ++l) {
      const GLsizei w = std::max<GLsizei>(1, base_w >> l);
      const GLsizei h = std::max<GLsizei>(1, base_h >> l);
      const std::size_t slice_bytes =
          static_cast<std::size_t>(w) * h * 4;
      for (NSUInteger slice = 0; slice < (NSUInteger)layers; ++slice) {
        MTLRegion region =
            MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h);
        [tex replaceRegion:region
               mipmapLevel:(NSUInteger)l
                     slice:slice
                   withBytes:static_cast<const std::uint8_t*>(level_rgba8[l]) +
                             slice * slice_bytes
                 bytesPerRow:(NSUInteger)(w * 4)
               bytesPerImage:slice_bytes];
      }
    }
    frag_texs_[unit] = tex;
    active_frag_unit_ = unit;
  }

  // Full GL sampler enums (spec 8.2, verified against docs/reference/gl32.h
  // values): 0x2600 NEAREST, 0x2601 LINEAR, 0x2700..0x2703 mipmap modes,
  // 0x812F CLAMP_TO_EDGE, 0x2901 REPEAT, 0x8370 MIRRORED_REPEAT.
  // CLAMP_TO_BORDER has no Metal sampler mode (no border color): rejected.
  static int SamplerMinMag(GLenum f, bool* mip_linear, bool* mip_nearest) {
    switch (f) {
      case 0x2600u:
        return 0;  // Nearest.
      case 0x2601u:
        return 1;  // Linear.
      case 0x2700u:  // NEAREST_MIPMAP_NEAREST.
        if (mip_nearest) *mip_nearest = true;
        return 0;
      case 0x2701u:  // LINEAR_MIPMAP_NEAREST.
        if (mip_nearest) *mip_nearest = true;
        return 1;
      case 0x2702u:  // NEAREST_MIPMAP_LINEAR.
        if (mip_linear) *mip_linear = true;
        return 0;
      case 0x2703u:  // LINEAR_MIPMAP_LINEAR.
        if (mip_linear) *mip_linear = true;
        return 1;
      default:
        return -1;
    }
  }

  void BuildSamplerForUnit(GLuint unit) {
    auto it = sampler_gl_.find(unit);
    if (it == sampler_gl_.end() || !it->second.has_detail) return;
    const SamplerGL& g = it->second;
    bool mip_linear = false, mip_nearest = false;
    const int minf = SamplerMinMag(g.min_filter, &mip_linear, &mip_nearest);
    const int magf = SamplerMinMag(g.mag_filter, nullptr, nullptr);
    if (minf < 0 || magf < 0) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
    auto addr_mode = [](GLenum w, int* out) {
      switch (w) {
        case 0x812Fu:
          *out = 0;
          return true;
        case 0x2901u:
          *out = 1;
          return true;
        case 0x8370u:
          *out = 2;
          return true;
        default:
          return false;
      }
    };
    int s = 0, t = 0;
    if (!addr_mode(g.wrap_s, &s) || !addr_mode(g.wrap_t, &t)) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter =
        (minf == 1) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.magFilter =
        (magf == 1) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.mipFilter = mip_linear      ? MTLSamplerMipFilterLinear
                   : mip_nearest   ? MTLSamplerMipFilterNearest
                                   : MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = (s == 1) ? MTLSamplerAddressModeRepeat
                      : (s == 2) ? MTLSamplerAddressModeMirrorRepeat
                                 : MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = (t == 1) ? MTLSamplerAddressModeRepeat
                      : (t == 2) ? MTLSamplerAddressModeMirrorRepeat
                                 : MTLSamplerAddressModeClampToEdge;
    sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
    // Phase 4 item 10: MIN/MAX_LOD -> lodMin/MaxClamp (Metal has them).
    sd.lodMinClamp = g.min_lod;
    sd.lodMaxClamp = g.max_lod;
    id<MTLSamplerState> smp = [device_ newSamplerStateWithDescriptor:sd];
    if (smp == nil) {
      errors_.Record(kGlInvalidOperation);
      fprintf(stderr, "[TGL-DEBUG] sampler lod %.2f/%.2f rejected\n",
              g.min_lod, g.max_lod);
      return;
    }
    frag_smps_[unit] = smp;
    active_frag_unit_ = unit;
  }

  void SetFragmentSamplerDetail(GLuint unit, GLenum min_filter,
                                GLenum mag_filter, GLenum wrap_s,
                                GLenum wrap_t) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    // Validate eagerly (same errors as before) before storing.
    bool mip_linear = false, mip_nearest = false;
    const int minf = SamplerMinMag(min_filter, &mip_linear, &mip_nearest);
    const int magf = SamplerMinMag(mag_filter, nullptr, nullptr);
    if (minf < 0 || magf < 0) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
    if (wrap_s != 0x812Fu && wrap_s != 0x2901u && wrap_s != 0x8370u) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (wrap_t != 0x812Fu && wrap_t != 0x2901u && wrap_t != 0x8370u) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    SamplerGL& g = sampler_gl_[unit];
    // Preserve LOD set earlier via SetSamplerLod (facade calls Detail then
    // Lod; either order works because both rebuild).
    const float keep_min =
        sampler_gl_.find(unit) != sampler_gl_.end() ? g.min_lod : -1000.0f;
    const float keep_max =
        sampler_gl_.find(unit) != sampler_gl_.end() ? g.max_lod : 1000.0f;
    g.min_filter = min_filter;
    g.mag_filter = mag_filter;
    g.wrap_s = wrap_s;
    g.wrap_t = wrap_t;
    if (!g.has_detail) {
      g.min_lod = keep_min;
      g.max_lod = keep_max;
    }
    g.has_detail = true;
    BuildSamplerForUnit(unit);
  }

  void SetSamplerLod(GLuint unit, float min_lod, float max_lod) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (min_lod > max_lod) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    SamplerGL& g = sampler_gl_[unit];
    g.min_lod = min_lod;
    g.max_lod = max_lod;
    if (g.has_detail) BuildSamplerForUnit(unit);
  }

  void SetSamplerSlots(const GLuint* gl_units, std::size_t count) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (gl_units == nullptr && count != 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    sampler_slots_.assign(gl_units, gl_units + count);
  }

  void SetMrtCount(GLsizei n) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (n < 1 || n > 8) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    mrt_count_ = n;
  }

  bool ReadbackAttachment(GLuint index, GLint x, GLint y,
                          std::uint8_t out_rgba[4]) override {
    if (out_rgba == nullptr) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    if (index >= mrt_targets_.size() || mrt_targets_[index] == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    id<MTLTexture> tex = mrt_targets_[index];
    if (x < 0 || y < 0 || (NSUInteger)x >= tex.width ||
        (NSUInteger)y >= tex.height) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    WaitForCompletion(frame_serial_);
    uint8_t bgra[4] = {0};
    [tex getBytes:bgra
      bytesPerRow:(NSUInteger)(tex.width * 4)
       fromRegion:MTLRegionMake2D((NSUInteger)x, (NSUInteger)y, 1, 1)
      mipmapLevel:0];
    out_rgba[0] = bgra[2];
    out_rgba[1] = bgra[1];
    out_rgba[2] = bgra[0];
    out_rgba[3] = bgra[3];
    return true;
  }

  void BeginRenderPassNoClear() override {
    // Two-pass split-stencil emulation: same attachments as BeginRenderPass
    // but everything Loads (tone: pass 1 cleared, pass 2 preserves).
    if (!frame_open_ || render_open_ || committed_ || cmd_ == nil ||
        target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    const bool want_depth = depth_cfg_.enabled;
    const bool want_stencil = stencil_cfg_.enabled;
    id<MTLTexture> ds_tex = want_stencil ? ds_target_ : nil;
    if (want_depth && !want_stencil && depth_target_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (want_stencil && ds_tex == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLRenderPassDescriptor* rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
    const bool use_msaa = (!msaa_targets_.empty());
    const GLsizei n =
        (bound_mrt_count_ > 1) ? bound_mrt_count_ : 1;
    for (GLsizei i = 0; i < n && (std::size_t)i < mrt_targets_.size(); ++i) {
      const bool resolve_this =
          use_msaa && (std::size_t)i < msaa_targets_.size();
      id<MTLTexture> msaa_tex = resolve_this ? msaa_targets_[i] : nil;
      rp.colorAttachments[i].texture =
          resolve_this ? msaa_tex : mrt_targets_[i];
      rp.colorAttachments[i].loadAction = MTLLoadActionLoad;
      if (resolve_this) {
        rp.colorAttachments[i].resolveTexture = mrt_targets_[i];
        rp.colorAttachments[i].storeAction =
            MTLStoreActionStoreAndMultisampleResolve;
      } else {
        rp.colorAttachments[i].storeAction = MTLStoreActionStore;
      }
    }
    id<MTLDepthStencilState> dss = nil;
    if (want_depth) {
      id<MTLTexture> depth_tex = want_stencil ? ds_tex : depth_target_;
      if (use_msaa) {
        id<MTLTexture> msaa_d = want_stencil ? msaa_ds_ : msaa_depth_;
        if (msaa_d != nil) depth_tex = msaa_d;
      }
      rp.depthAttachment.texture = depth_tex;
      rp.depthAttachment.loadAction = MTLLoadActionLoad;
      rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    }
    if (want_stencil) {
      id<MTLTexture> sten_tex = ds_tex;
      if (use_msaa && msaa_ds_ != nil) sten_tex = msaa_ds_;
      rp.stencilAttachment.texture = sten_tex;
      rp.stencilAttachment.loadAction = MTLLoadActionLoad;
      rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
    }
    if (want_depth || want_stencil) {
      dss = EnsureDepthStencilState();
      if (dss == nil) return;  // Error already recorded.
    }
    encoder_ = [cmd_ renderCommandEncoderWithDescriptor:rp];
    if (encoder_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    // Same PSO the first pass used (bound before the frame started).
    if (bound_pso_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    [encoder_ setRenderPipelineState:bound_pso_];
    [encoder_ setBlendColorRed:blend_color_[0]
                        green:blend_color_[1]
                         blue:blend_color_[2]
                        alpha:blend_color_[3]];
    if (dss != nil) [encoder_ setDepthStencilState:dss];
    if (want_stencil) {
      [encoder_ setStencilReferenceValue:(std::uint32_t)(
          stencil_cfg_.ref_front < 0 ? 0
          : stencil_cfg_.ref_front > 255 ? 255
                                           : stencil_cfg_.ref_front)];
    }
    ApplyRasterEncoderState();
    render_open_ = true;
  }

  void SetUboBytes(GLuint block_index, const void* data,
                   std::size_t bytes) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if ((data == nullptr && bytes != 0) || (bytes % 16) != 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    ubo_bytes_[block_index].assign(static_cast<const std::uint8_t*>(data),
                                   static_cast<const std::uint8_t*>(data) +
                                       bytes);
  }

  void ConfigureCull(const CullConfig& config) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (config.mode != kGlFront && config.mode != kGlBackFace &&
        config.mode != kGlFrontAndBack) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
    cull_cfg_ = config;
  }

  void ConfigureViewport(const ViewportConfig& config) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if (config.width < 0 || config.height < 0 || config.scissor_width < 0 ||
        config.scissor_height < 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    viewport_cfg_ = config;
  }

  // Applies cull + winding + viewport + scissor + depth bias to an open
  // render encoder (both BeginRenderPass variants call this; encoder state
  // is per-pass, unlike the persisted configs).
  void ApplyRasterEncoderState() {
    if (encoder_ == nil) return;
    if (cull_cfg_.enabled) {
      // FRONT_AND_BACK culls everything: the facade skips such draws before
      // reaching the bridge (early-out), so only FRONT/BACK arrive here.
      [encoder_ setCullMode:(cull_cfg_.mode == kGlFront)
                                ? MTLCullModeFront
                                : MTLCullModeBack];
    } else {
      [encoder_ setCullMode:MTLCullModeNone];
    }
    [encoder_ setFrontFacingWinding:(cull_cfg_.front_face == kGlCw)
                                        ? MTLWindingClockwise
                                        : MTLWindingCounterClockwise];
    // Viewport maps NDC onto the target rect (GL origin bottom-left matches
    // Metal's viewport origin convention here).
    MTLViewport vp;
    vp.originX = viewport_cfg_.x;
    vp.originY = viewport_cfg_.y;
    vp.width = viewport_cfg_.width > 0 ? (double)viewport_cfg_.width
                                       : (double)target_width_;
    vp.height = viewport_cfg_.height > 0 ? (double)viewport_cfg_.height
                                         : (double)target_height_;
    vp.znear = 0.0;
    vp.zfar = 1.0;
    [encoder_ setViewport:vp];
    // Metal always applies the scissor rect: full target unless the test is
    // on (spec 14.1.2: scissor only clips while enabled).
    MTLScissorRect sr;
    if (viewport_cfg_.scissor_enabled && viewport_cfg_.scissor_width > 0 &&
        viewport_cfg_.scissor_height > 0) {
      sr.x = viewport_cfg_.scissor_x >= 0 ? (NSUInteger)viewport_cfg_.scissor_x
                                          : 0;
      sr.y = viewport_cfg_.scissor_y >= 0 ? (NSUInteger)viewport_cfg_.scissor_y
                                          : 0;
      sr.width = (NSUInteger)viewport_cfg_.scissor_width;
      sr.height = (NSUInteger)viewport_cfg_.scissor_height;
    } else {
      sr.x = 0;
      sr.y = 0;
      sr.width =
          target_width_ > 0 ? (NSUInteger)target_width_ : (NSUInteger)vp.width;
      sr.height = target_height_ > 0 ? (NSUInteger)target_height_
                                     : (NSUInteger)vp.height;
    }
    [encoder_ setScissorRect:sr];
    if (depth_cfg_.bias_enabled) {
      [encoder_ setDepthBias:depth_cfg_.bias_units
                 slopeScale:depth_cfg_.bias_factor
                      clamp:0.0f];
    }
  }

  void SetBlendColor(float r, float g, float b, float a) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    blend_color_[0] = r;
    blend_color_[1] = g;
    blend_color_[2] = b;
    blend_color_[3] = a;
  }

  void SetUniformBytes(const void* data, std::size_t bytes) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    if ((data == nullptr && bytes != 0) || (bytes % 16) != 0) {
      errors_.Record(kGlInvalidValue);
      return;
    }
    uniform_bytes_.assign(static_cast<const std::uint8_t*>(data),
                          static_cast<const std::uint8_t*>(data) + bytes);
  }

  bool ComputeIncrementUint32(const std::uint32_t* input,
                              std::uint32_t* output,
                              std::size_t count) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    if (count == 0) return true;
    if (input == nullptr || output == nullptr) {
      errors_.Record(kGlInvalidValue);
      return false;
    }
    if (!EnsureLibrary()) return false;
    NSError* err = nil;
    id<MTLFunction> fn =
        [library_ newFunctionWithName:@"compute_add_one"];
    if (fn == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    id<MTLComputePipelineState> pso =
        [device_ newComputePipelineStateWithFunction:fn error:&err];
    if (pso == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    id<MTLBuffer> vin =
        [device_ newBufferWithBytes:input
                             length:count * sizeof(std::uint32_t)
                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> vout =
        [device_ newBufferWithLength:count * sizeof(std::uint32_t)
                             options:MTLResourceStorageModeShared];
    if (vin == nil || vout == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    if (cb == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    if (enc == nil) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    [enc setComputePipelineState:pso];
    [enc setBuffer:vin offset:0 atIndex:0];
    [enc setBuffer:vout offset:0 atIndex:1];
    MTLSize grid = MTLSizeMake(count, 1, 1);
    NSUInteger w = pso.maxTotalThreadsPerThreadgroup;
    if (w == 0) w = 1;
    if (w > count) w = count;
    MTLSize tg = MTLSizeMake(w, 1, 1);
    [enc dispatchThreads:grid threadsPerThreadgroup:tg];
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if ([cb status] != MTLCommandBufferStatusCompleted) {
      errors_.Record(kGlInvalidOperation);
      return false;
    }
    std::memcpy(output, [vout contents], count * sizeof(std::uint32_t));
    return true;
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

  void ConfigureStencil(const StencilConfig& config) override {
    if (config.func_front < kGlNever || config.func_front > kGlAlways ||
        config.func_back < kGlNever || config.func_back > kGlAlways) {
      errors_.Record(kGlInvalidEnum);
      return;
    }
    const GLenum ops[] = {
        config.sfail_front,  config.sfail_back,  config.dpfail_front,
        config.dpfail_back,  config.dppass_front, config.dppass_back};
    for (GLenum op : ops) {
      if (StencilOperation(op) < 0) {
        errors_.Record(kGlInvalidOperation);  // No MTLStencilOperation.
        return;
      }
    }
    if (config.enabled && config.ref_front != config.ref_back) {
      // MTLRenderCommandEncoder (macOS 26.2 SDK, MTLRenderCommandEncoder.h:462)
      // exposes a single setStencilReferenceValue: for both faces. Separate
      // front/back refs cannot execute; fail closed instead of silently using
      // one of them.
      errors_.Record(kGlInvalidOperation);
      return;
    }
    stencil_cfg_ = config;
  }

  bool StencilEnabled() const override {
    return initialized_ && stencil_cfg_.enabled;
  }

  // GL stencil op -> MTLStencilOperation (Keep=0 .. DecrementWrap=7).
  // GL has no direct ZERO constant in raster.h (kGlZero==0 doubles as the
  // blend factor), so 0x0000 is matched numerically here.
  static int StencilOperation(GLenum op) {
    switch (op) {
      case 0x1E00u:  // KEEP
        return 0;
      case 0x0000u:  // ZERO
        return 1;
      case 0x1E01u:  // REPLACE
        return 2;
      case 0x1E02u:  // INCR
        return 3;
      case 0x1E03u:  // DECR
        return 4;
      case 0x150Au:  // INVERT
        return 5;
      case 0x8507u:  // INCR_WRAP
        return 6;
      case 0x8508u:  // DECR_WRAP
        return 7;
      default:
        return -1;
    }
  }

  // Cache key for stencil-aware keyed pipelines: blend key fields that reach
  // the descriptor (per-buffer for MRT) + every stencil field that reaches
  // the descriptor (ref values are encoder state, set per draw, so they are
  // excluded) + the textured bit.
  static std::string StencilSignature(const backend::PsoKey& key,
                                      const StencilConfig& cfg) {
    char buf[1024];
    int n = std::snprintf(buf, sizeof(buf),
                  "%u-%u-%u-%u-%u-%u-%u-%u-%u-%u-%u-%x-%x-%x-%x-%x-%x-%x-%x-%x-"
                  "%x-%x-%x-%x-%u-%u-%u-%u-%u|",
                  (unsigned)key.vertex_program, (unsigned)key.fragment_program,
                  (unsigned)key.blend_eq_rgb, (unsigned)key.blend_eq_alpha,
                  (unsigned)key.blend_src_rgb, (unsigned)key.blend_dst_rgb,
                  (unsigned)key.blend_src_alpha, (unsigned)key.blend_dst_alpha,
                  (unsigned)key.blend_enabled, (unsigned)key.color_write_mask,
                  (unsigned)key.depth_enabled, (unsigned)cfg.func_front,
                  (unsigned)cfg.func_back, (unsigned)cfg.value_mask_front,
                  (unsigned)cfg.value_mask_back,
                  (unsigned)cfg.write_mask_front,
                  (unsigned)cfg.write_mask_back, (unsigned)cfg.sfail_front,
                  (unsigned)cfg.sfail_back, (unsigned)cfg.dpfail_front,
                  (unsigned)cfg.dpfail_back, (unsigned)cfg.dppass_front,
                  (unsigned)cfg.dppass_back,
                  (unsigned)key.sample_count, (unsigned)key.textured,
                  (unsigned)key.translated, (unsigned)key.program_id,
                  (unsigned)key.slot2, (unsigned)key.mrt_count);
    std::string s(buf, n > 0 ? static_cast<std::size_t>(n) : 0);
    for (int i = 0; i < 8; ++i) {
      char pb[128];
      std::snprintf(pb, sizeof(pb), "%u/%u/%u/%u/%u/%u/%d/%u;",
                    (unsigned)key.blend_eq_rgb_per[i],
                    (unsigned)key.blend_eq_alpha_per[i],
                    (unsigned)key.blend_src_rgb_per[i],
                    (unsigned)key.blend_dst_rgb_per[i],
                    (unsigned)key.blend_src_alpha_per[i],
                    (unsigned)key.blend_dst_alpha_per[i],
                    key.blend_enabled_per[i] ? 1 : 0,
                    (unsigned)key.color_write_mask_per[i]);
      s += pb;
    }
    return s;
  }

  std::string StencilSignature(const backend::PsoKey& key) const {
    return StencilSignature(key, stencil_cfg_);
  }

  // Fills one face of a depth-stencil descriptor from GL stencil state.
  static void FillStencilFace(MTLStencilDescriptor* face, GLenum func,
                              GLuint read_mask, GLuint write_mask, GLenum sfail,
                              GLenum dpfail, GLenum dppass) {
    face.stencilCompareFunction = (MTLCompareFunction)(func - kGlNever);
    face.readMask = read_mask;
    face.writeMask = write_mask;
    face.stencilFailureOperation =
        (MTLStencilOperation)StencilOperation(sfail);
    face.depthFailureOperation =
        (MTLStencilOperation)StencilOperation(dpfail);
    face.depthStencilPassOperation =
        (MTLStencilOperation)StencilOperation(dppass);
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

  // Compiles one PSO from a PsoKey (type-1 translation): blend enable,
  // per-channel equations/factors and the write mask come from live GL
  // state via metal_translate (pure, unit-tested). Untranslatable factors
  // (dual-source) fail closed here instead of compiling a wrong PSO.
  // Depth/stencil pixel formats are declared from the live configs + key
  // (see the format block below), so blended-depth keys execute instead of
  // being refused.
  id<MTLRenderPipelineState> CompileKeyedPipeline(
      const backend::PsoKey& key, bool with_stencil) {
    const int rgb_op = metal_translate::BlendOperation(key.blend_eq_rgb);
    const int alpha_op = metal_translate::BlendOperation(key.blend_eq_alpha);
    const int src_rgb = metal_translate::BlendFactor(key.blend_src_rgb);
    const int dst_rgb = metal_translate::BlendFactor(key.blend_dst_rgb);
    const int src_alpha = metal_translate::BlendFactor(key.blend_src_alpha);
    const int dst_alpha = metal_translate::BlendFactor(key.blend_dst_alpha);
    if (key.blend_enabled &&
        (rgb_op < 0 || alpha_op < 0 || src_rgb < 0 || dst_rgb < 0 ||
         src_alpha < 0 || dst_alpha < 0)) {
      errors_.Record(kGlInvalidOperation);  // Needs encoder state (tracked).
      return nil;
    }
    if (key.blend_enabled &&
        (src_rgb >= 15 || dst_rgb >= 15 || src_alpha >= 15 ||
         dst_alpha >= 15)) {
      // Dual-source factors need the translator's DualOut fragment
      // ([[color(0), index(1)]]); the fixed pair has no second source.
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    if (!EnsureLibrary()) return nil;
    NSString* vname = key.textured ? @"vert_tex_main" : @"vert_main";
    NSString* fname = key.textured ? @"frag_tex_main" : @"frag_main";
    id<MTLFunction> vf = [library_ newFunctionWithName:vname];
    id<MTLFunction> ff = [library_ newFunctionWithName:fname];
    if (vf == nil || ff == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    MTLRenderPipelineDescriptor* desc =
        [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vf;
    desc.fragmentFunction = ff;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (key.blend_enabled) {
      desc.colorAttachments[0].blendingEnabled = YES;
      desc.colorAttachments[0].rgbBlendOperation =
          (MTLBlendOperation)rgb_op;
      desc.colorAttachments[0].alphaBlendOperation =
          (MTLBlendOperation)alpha_op;
      desc.colorAttachments[0].sourceRGBBlendFactor =
          (MTLBlendFactor)src_rgb;
      desc.colorAttachments[0].destinationRGBBlendFactor =
          (MTLBlendFactor)dst_rgb;
      desc.colorAttachments[0].sourceAlphaBlendFactor =
          (MTLBlendFactor)src_alpha;
      desc.colorAttachments[0].destinationAlphaBlendFactor =
          (MTLBlendFactor)dst_alpha;
    }
    desc.colorAttachments[0].writeMask = key.color_write_mask;
    // MSAA rasterization needs the pipeline sample count to match the
    // multisample attachment: without this Metal rasterizes a single sample
    // (probed: resolve then reads 255/N). Single-sample draws keep 1.
    desc.sampleCount =
        (key.sample_count > 1) ? (NSUInteger)key.sample_count : 1;
    ApplyDepthStencilFormats(desc, key, with_stencil);
    desc.vertexDescriptor = BuildKeyedVertexDescriptor(key);
    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [device_ newRenderPipelineStateWithDescriptor:desc error:&err];
    if (pso == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    return pso;
  }

  // Depth/stencil formats must EXACTLY match the render pass attachments, or
  // Metal drops the draw. The key carries depth_enabled from live GL state
  // and with_stencil marks the stencil cache; the live configs confirm an
  // attachment is actually bound (test-without-buffer renders color-only by
  // facade contract, so no format is declared then). Shared by keyed and
  // translated pipelines so both execute depth+blend instead of refusing it.
  void ApplyDepthStencilFormats(MTLRenderPipelineDescriptor* desc,
                                const backend::PsoKey& key, bool with_stencil) {
    const bool want_depth_fmt = depth_cfg_.enabled && key.depth_enabled;
    const bool want_stencil_fmt = with_stencil && stencil_cfg_.enabled;
    if (want_depth_fmt && want_stencil_fmt) {
      desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
      desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    } else if (want_depth_fmt) {
      desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    } else if (want_stencil_fmt) {
      desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    }
  }

  // Vertex layout for keyed pipelines: pos@0+col@1 (stride 32), plus uv@2
  // (stride 40) when key.textured. Fixed strides (not the mutable
  // vertex_stride_): the PSO must match the facade's interleave regardless
  // of call order (SetVertexBytes may follow creation).
  MTLVertexDescriptor* BuildKeyedVertexDescriptor(
      const backend::PsoKey& key) {
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat4;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 1;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 16;
    vd.attributes[1].bufferIndex = 1;
    if (key.slot2 == 0) {
      vd.layouts[1].stride = 32;
    } else {
      // Slot 2 width follows the translated program (uv=float2,
      // cube dir=float3) or the legacy textured layout (float2).
      vd.attributes[2].format = (key.slot2 >= 4)   ? MTLVertexFormatFloat4
                                : (key.slot2 == 3) ? MTLVertexFormatFloat3
                                                   : MTLVertexFormatFloat2;
      vd.attributes[2].offset = 32;
      vd.attributes[2].bufferIndex = 1;
      vd.layouts[1].stride = 32 + key.slot2 * 4;
    }
    return vd;
  }

  // Translated graphics pipeline (GLSL->MSL v1): compiles the app-derived
  // library (cached by content) and builds the keyed PSO around its entry
  // points. Caching mirrors CreateRenderPipeline (plain vs stencil tables)
  // with the program identity inside the key (translated+program_id), so two
  // programs can never share a PSO.
  std::uint32_t CreateTranslatedPipeline(
      const glsl::TranslatedProgram& prog,
      const backend::PsoKey& key) override {
    if (!initialized_) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    if (!prog.ok || prog.library_source.empty() || prog.vertex_fn.empty() ||
        prog.fragment_fn.empty() || !key.translated) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    if ((key.mrt_count > 1) != prog.is_mrt) {
      errors_.Record(kGlInvalidOperation);
      return 0;
    }
    if (stencil_cfg_.enabled) {
      const std::string sig = StencilSignature(key);
      auto st = stencil_pipelines_.find(sig);
      if (st != stencil_pipelines_.end()) return st->second.handle;
      id<MTLRenderPipelineState> pso =
          CompileTranslated(prog, key, true);
      if (pso == nil) return 0;  // Error already recorded.
      const std::uint32_t handle = next_handle_++;
      stencil_pipelines_[sig] = Entry{handle, pso, key};
      stencil_key_textured_[sig] = key.textured;
      stencil_key_sampler_[sig] = prog.uses_sampler;
      stencil_keys_[sig] = key;
      return handle;
    }
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) return it->second.handle;
    id<MTLRenderPipelineState> pso = CompileTranslated(prog, key, false);
    if (pso == nil) return 0;  // Error already recorded.
    const std::uint32_t handle = next_handle_++;
    pipelines_[key] = Entry{handle, pso, key};
    handle_uses_sampler_[handle] = prog.uses_sampler;
    return handle;
  }

  id<MTLRenderPipelineState> CompileTranslated(
      const glsl::TranslatedProgram& prog, const backend::PsoKey& key,
      bool with_stencil) {
    // Per-buffer blend (Phase 4 item 5): validate every enabled attachment;
    // MRT+MSAA (item 4) and blended MRT now execute via per-attachment
    // descriptors + resolves (no more tracked rejects).
    const GLsizei mrt_n0 =
        (key.mrt_count >= 1 && key.mrt_count <= 8) ? key.mrt_count : 1;
    for (GLsizei i = 0; i < mrt_n0; ++i) {
      const std::size_t u = static_cast<std::size_t>(i);
      const bool en = key.blend_enabled_per[u];
      if (!en) continue;
      const int rop =
          metal_translate::BlendOperation(key.blend_eq_rgb_per[u]);
      const int aop =
          metal_translate::BlendOperation(key.blend_eq_alpha_per[u]);
      const int srgb = metal_translate::BlendFactor(key.blend_src_rgb_per[u]);
      const int drgb = metal_translate::BlendFactor(key.blend_dst_rgb_per[u]);
      const int salpha =
          metal_translate::BlendFactor(key.blend_src_alpha_per[u]);
      const int dalpha =
          metal_translate::BlendFactor(key.blend_dst_alpha_per[u]);
      if (rop < 0 || aop < 0 || srgb < 0 || drgb < 0 || salpha < 0 ||
          dalpha < 0) {
        fprintf(stderr,
                "[TGL-DEBUG] translated per-buffer blend %d untranslatable\n",
                i);
        errors_.Record(kGlInvalidOperation);
        return nil;
      }
      if ((srgb >= 15 || drgb >= 15 || salpha >= 15 || dalpha >= 15) &&
          !prog.is_dual_source) {
        fprintf(stderr,
                "[TGL-DEBUG] translated dual-source factor on single-out %d\n",
                i);
        errors_.Record(kGlInvalidOperation);
        return nil;
      }
    }
    id<MTLLibrary> lib = nullptr;
    auto lit = trans_libs_.find(prog.library_source);
    if (lit != trans_libs_.end()) {
      lib = lit->second;
    } else {
      NSError* err = nil;
      NSString* src = [NSString
          stringWithUTF8String:prog.library_source.c_str()];
      lib = [device_ newLibraryWithSource:src options:nil error:&err];
      if (lib == nil) {
        if (err != nil) {
          fprintf(stderr, "[TGL-DEBUG] translated MSL rejected: %s\nSRC:\n%s\n",
                  [[err localizedDescription] UTF8String],
                  prog.library_source.c_str());
        }
        errors_.Record(kGlInvalidOperation);  // MSL rejected by Metal.
        return nil;
      }
      trans_libs_[prog.library_source] = lib;
    }
    NSString* vname =
        [NSString stringWithUTF8String:prog.vertex_fn.c_str()];
    NSString* fname =
        [NSString stringWithUTF8String:prog.fragment_fn.c_str()];
    id<MTLFunction> vf = [lib newFunctionWithName:vname];
    id<MTLFunction> ff = [lib newFunctionWithName:fname];
    if (vf == nil || ff == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    MTLRenderPipelineDescriptor* desc =
        [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vf;
    desc.fragmentFunction = ff;
    const GLsizei mrt_n =
        (key.mrt_count >= 1 && key.mrt_count <= 8) ? key.mrt_count : 1;
    for (GLsizei i = 0; i < mrt_n; ++i) {
      const std::size_t u = static_cast<std::size_t>(i);
      desc.colorAttachments[i].pixelFormat = MTLPixelFormatBGRA8Unorm;
      if (key.blend_enabled_per[u]) {
        desc.colorAttachments[i].blendingEnabled = YES;
        desc.colorAttachments[i].rgbBlendOperation = (MTLBlendOperation)
            metal_translate::BlendOperation(key.blend_eq_rgb_per[u]);
        desc.colorAttachments[i].alphaBlendOperation = (MTLBlendOperation)
            metal_translate::BlendOperation(key.blend_eq_alpha_per[u]);
        desc.colorAttachments[i].sourceRGBBlendFactor = (MTLBlendFactor)
            metal_translate::BlendFactor(key.blend_src_rgb_per[u]);
        desc.colorAttachments[i].destinationRGBBlendFactor = (MTLBlendFactor)
            metal_translate::BlendFactor(key.blend_dst_rgb_per[u]);
        desc.colorAttachments[i].sourceAlphaBlendFactor = (MTLBlendFactor)
            metal_translate::BlendFactor(key.blend_src_alpha_per[u]);
        desc.colorAttachments[i].destinationAlphaBlendFactor = (MTLBlendFactor)
            metal_translate::BlendFactor(key.blend_dst_alpha_per[u]);
      }
      desc.colorAttachments[i].writeMask =
          key.color_write_mask_per[u];
    }
    // Same MSAA rule as keyed pipelines (see above): match the attachment.
    desc.sampleCount =
        (key.sample_count > 1) ? (NSUInteger)key.sample_count : 1;
    ApplyDepthStencilFormats(desc, key, with_stencil);
    desc.vertexDescriptor = BuildKeyedVertexDescriptor(key);
    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [device_ newRenderPipelineStateWithDescriptor:desc error:&err];
    if (pso == nil) {
      if (err != nil) {
        fprintf(stderr, "[TGL-DEBUG] translated PSO rejected: %s\n",
                [[err localizedDescription] UTF8String]);
      }
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    return pso;
  }

  // Compiles the default color-only (with_depth=false) or depth-attached
  // pipeline. The vertex layout matches the bridge MSL VertexIn
  // (float4 position @0 + float4 color @1, stride 32) or VertexInTex
  // (+ float2 uv @2, stride 40) when textured.
  id<MTLRenderPipelineState> CompileDefaultPipeline(bool with_depth,
                                                     bool textured = false) {
    if (!EnsureLibrary()) return nil;
    NSString* vname = textured ? @"vert_tex_main" : @"vert_main";
    NSString* fname = textured ? @"frag_tex_main" : @"frag_main";
    id<MTLFunction> vf = [library_ newFunctionWithName:vname];
    id<MTLFunction> ff = [library_ newFunctionWithName:fname];
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
    if (textured) {
      vd.attributes[2].format = MTLVertexFormatFloat2;
      vd.attributes[2].offset = 32;
      vd.attributes[2].bufferIndex = 1;
      vd.layouts[1].stride = 40;
    } else {
      vd.layouts[1].stride = 32;
    }
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

  // Combined depth+stencil fallback pipeline (bare-bridge use only; facade
  // draws bind keyed PSOs that carry their own blend state).
  id<MTLRenderPipelineState> CompileStencilDepthPipeline(
      bool textured = false) {
    if (!EnsureLibrary()) return nil;
    NSString* vname = textured ? @"vert_tex_main" : @"vert_main";
    NSString* fname = textured ? @"frag_tex_main" : @"frag_main";
    id<MTLFunction> vf = [library_ newFunctionWithName:vname];
    id<MTLFunction> ff = [library_ newFunctionWithName:fname];
    if (vf == nil || ff == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    MTLRenderPipelineDescriptor* desc =
        [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vf;
    desc.fragmentFunction = ff;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat4;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 1;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 16;
    vd.attributes[1].bufferIndex = 1;
    if (textured) {
      vd.attributes[2].format = MTLVertexFormatFloat2;
      vd.attributes[2].offset = 32;
      vd.attributes[2].bufferIndex = 1;
      vd.layouts[1].stride = 40;
    } else {
      vd.layouts[1].stride = 32;
    }
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
  // the pair is unchanged (creating one per frame would churn). With stencil
  // on, both faces are baked from the StencilConfig and the cache key covers
  // every baked field (ref values ride the encoder, not the state object).
  id<MTLDepthStencilState> EnsureDepthStencilState() {
    const std::string sig = StencilStateSignature();
    if (depth_state_ != nil && depth_state_func_ == depth_cfg_.func &&
        depth_state_write_ == depth_cfg_.write_mask &&
        stencil_state_sig_ == sig) {
      return depth_state_;
    }
    MTLDepthStencilDescriptor* dd =
        [[MTLDepthStencilDescriptor alloc] init];
    if (depth_cfg_.enabled) {
      dd.depthCompareFunction =
          (MTLCompareFunction)(depth_cfg_.func - kGlNever);
      dd.depthWriteEnabled = depth_cfg_.write_mask ? YES : NO;
    } else {
      // No depth attachment in the pass: neutralize depth so a stale func
      // can never discard (Always + no write is the identity depth test).
      dd.depthCompareFunction = MTLCompareFunctionAlways;
      dd.depthWriteEnabled = NO;
    }
    if (stencil_cfg_.enabled) {
      MTLStencilDescriptor* front = [[MTLStencilDescriptor alloc] init];
      FillStencilFace(front, stencil_cfg_.func_front,
                      stencil_cfg_.value_mask_front,
                      stencil_cfg_.write_mask_front, stencil_cfg_.sfail_front,
                      stencil_cfg_.dpfail_front, stencil_cfg_.dppass_front);
      dd.frontFaceStencil = front;
      MTLStencilDescriptor* back = [[MTLStencilDescriptor alloc] init];
      FillStencilFace(back, stencil_cfg_.func_back,
                      stencil_cfg_.value_mask_back,
                      stencil_cfg_.write_mask_back, stencil_cfg_.sfail_back,
                      stencil_cfg_.dpfail_back, stencil_cfg_.dppass_back);
      dd.backFaceStencil = back;
    }
    depth_state_ = [device_ newDepthStencilStateWithDescriptor:dd];
    if (depth_state_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return nil;
    }
    depth_state_func_ = depth_cfg_.func;
    depth_state_write_ = depth_cfg_.write_mask;
    stencil_state_sig_ = sig;
    return depth_state_;
  }

  // Signature of every stencil field baked into the depth-stencil state
  // object (empty when stencil is off).
  std::string StencilStateSignature() const {
    if (!stencil_cfg_.enabled) return std::string();
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x-%x",
                  (unsigned)stencil_cfg_.func_front,
                  (unsigned)stencil_cfg_.func_back,
                  (unsigned)stencil_cfg_.value_mask_front,
                  (unsigned)stencil_cfg_.value_mask_back,
                  (unsigned)stencil_cfg_.write_mask_front,
                  (unsigned)stencil_cfg_.write_mask_back,
                  (unsigned)stencil_cfg_.sfail_front,
                  (unsigned)stencil_cfg_.sfail_back,
                  (unsigned)stencil_cfg_.dpfail_front,
                  (unsigned)stencil_cfg_.dpfail_back,
                  (unsigned)stencil_cfg_.dppass_front,
                  (unsigned)stencil_cfg_.dppass_back);
    return std::string(buf);
  }

  void Draw(GLenum mode) override {
    if (!frame_open_ || !render_open_ || committed_ || encoder_ == nil) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
    switch (mode) {
      case kGlPoints:
        primitive = MTLPrimitiveTypePoint;
        break;
      case kGlLines:
        primitive = MTLPrimitiveTypeLine;
        break;
      case kGlLineStrip:
        primitive = MTLPrimitiveTypeLineStrip;
        break;
      case kGlTriangles:
        primitive = MTLPrimitiveTypeTriangle;
        break;
      case kGlTriangleStrip:
        primitive = MTLPrimitiveTypeTriangleStrip;
        break;
      default:
        // LINE_LOOP/TRIANGLE_FAN are CPU-expanded by the facade and never
        // arrive here; adjacency/PATCHES have no Metal equivalent.
        errors_.Record((mode == kGlLineLoop || mode == kGlTriangleFan ||
                        (mode >= 0x000Au && mode <= 0x000Eu))
                           ? kGlInvalidOperation
                           : kGlInvalidEnum);
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
    if (!uniform_bytes_.empty()) {
      // Translated program: full uniform block (MVP + vec4s) to BOTH stages
      // (fragment lighting reads it too); legacy path keeps MVP vertex-only.
      [encoder_ setVertexBytes:uniform_bytes_.data()
                        length:uniform_bytes_.size()
                       atIndex:0];
      [encoder_ setFragmentBytes:uniform_bytes_.data()
                          length:uniform_bytes_.size()
                         atIndex:0];
    } else {
      [encoder_ setVertexBytes:mvp_
                        length:sizeof(mvp_)
                       atIndex:0];
    }
    if (bound_needs_texture_) {
      // Sampler wiring: MSL slot i reads the GL unit in sampler_slots_[i]
      // (translator emits slots in sampler-declaration order). Without an
      // explicit slot map (legacy single-sampler path), slot 0 reads the
      // last-uploaded unit. Missing texture fails closed (never samples
      // garbage); missing sampler uses a NEAREST/CLAMP default.
      std::vector<std::uint32_t> slots;
      if (!sampler_slots_.empty()) {
        slots = sampler_slots_;
      } else {
        auto tit0 = frag_texs_.find(active_frag_unit_);
        if (tit0 == frag_texs_.end()) tit0 = frag_texs_.find(0);
        if (tit0 == frag_texs_.end()) {
          errors_.Record(kGlInvalidOperation);
          return;
        }
        slots.push_back(tit0->first);
      }
      NSUInteger slot_idx = 0;
      for (std::uint32_t unit : slots) {
        auto tit = frag_texs_.find(unit);
        if (tit == frag_texs_.end() || tit->second == nil) {
          errors_.Record(kGlInvalidOperation);
          return;
        }
        id<MTLTexture> ftex = tit->second;
        id<MTLSamplerState> fsmp = nil;
        auto sit = frag_smps_.find(unit);
        if (sit != frag_smps_.end()) fsmp = sit->second;
        if (fsmp == nil) {
          MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
          sd.minFilter = MTLSamplerMinMagFilterNearest;
          sd.magFilter = MTLSamplerMinMagFilterNearest;
          sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
          sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
          sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
          fsmp = [device_ newSamplerStateWithDescriptor:sd];
          if (fsmp == nil) {
            errors_.Record(kGlInvalidOperation);
            return;
          }
        }
        [encoder_ setFragmentTexture:ftex atIndex:slot_idx];
        [encoder_ setFragmentSamplerState:fsmp atIndex:slot_idx];
        ++slot_idx;
      }
    }
    for (const auto& kv : ubo_bytes_) {
      // UBO blocks ride constant buffers 2+i (translator emits matching
      // indices); needed by both stages, like the uniform block.
      id<MTLBuffer> ubuf =
          [device_ newBufferWithBytes:kv.second.data()
                               length:kv.second.size()
                              options:MTLResourceStorageModeShared];
      if (ubuf == nil) {
        errors_.Record(kGlInvalidOperation);
        return;
      }
      const NSUInteger bi = 2 + kv.first;
      [encoder_ setVertexBuffer:ubuf offset:0 atIndex:bi];
      [encoder_ setFragmentBuffer:ubuf offset:0 atIndex:bi];
    }
    const NSUInteger vertexCount =
        (vertex_stride_ == 0)
            ? 0
            : (NSUInteger)(vertex_bytes_.size() / vertex_stride_);
    if (vertexCount == 0) {
      errors_.Record(kGlInvalidOperation);
      return;
    }
    [encoder_ drawPrimitives:primitive
                vertexStart:0
                vertexCount:vertexCount];
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
     backend::PsoKey key;
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
  CullConfig cull_cfg_;
  ViewportConfig viewport_cfg_;
  DepthConfig depth_cfg_;
  id<MTLTexture> depth_target_ = nil;
  GLsizei depth_width_ = 0;
  GLsizei depth_height_ = 0;
  id<MTLDepthStencilState> depth_state_ = nil;
  GLenum depth_state_func_ = 0;  // 0 caches nothing (NEVER is 0x0200).
  bool depth_state_write_ = false;
  // Stencil path (spec 13-15): combined depth-stencil texture shared by both
  // attachments when stencil is on; dedicated stencil-aware PSOs because the
  // pipeline depth/stencil pixel formats must match the render pass.
  StencilConfig stencil_cfg_;
  id<MTLTexture> ds_target_ = nil;
  GLsizei ds_width_ = 0;
  GLsizei ds_height_ = 0;
  std::string stencil_state_sig_;
  id<MTLRenderPipelineState> stencil_depth_pso_ = nil;
  id<MTLRenderPipelineState> stencil_depth_tex_pso_ = nil;
  std::map<std::string, Entry> stencil_pipelines_;
  std::map<std::string, bool> stencil_key_textured_;
  id<MTLRenderPipelineState> depth_pso_ = nil;
  id<MTLRenderPipelineState> depth_tex_pso_ = nil;
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
  // Textured sampling state (ES 3.2 §8 v1): per-GL-unit MTLTexture +
  // sampler, bound to Metal texture(0)/sampler(0) when bound_textured_.
  bool bound_textured_ = false;
  bool bound_needs_texture_ = false;
  GLsizei bound_mrt_count_ = 1;
  GLsizei bound_samples_ = 0;
  std::map<std::uint32_t, id<MTLTexture>> frag_texs_;
  std::map<std::uint32_t, id<MTLSamplerState>> frag_smps_;
  // LOD + filter memory so SetSamplerLod can rebuild the sampler (Phase 10).
  struct SamplerGL {
    GLenum min_filter = 0x2601u;
    GLenum mag_filter = 0x2601u;
    GLenum wrap_s = 0x812Fu;
    GLenum wrap_t = 0x812Fu;
    float min_lod = -1000.0f;
    float max_lod = 1000.0f;
    bool has_detail = false;
  };
  std::map<std::uint32_t, SamplerGL> sampler_gl_;
  std::uint32_t active_frag_unit_ = 0;
  std::vector<std::uint32_t> sampler_slots_;  // MSL slot -> GL unit.
  float blend_color_[4] = {0, 0, 0, 0};
  std::vector<std::uint8_t> uniform_bytes_;
  std::map<GLuint, std::vector<std::uint8_t>> ubo_bytes_;  // block -> bytes.
  std::map<std::uint32_t, bool> handle_uses_sampler_;
  std::map<std::string, bool> stencil_key_sampler_;
  std::map<std::string, backend::PsoKey> stencil_keys_;
  std::map<std::string, id<MTLLibrary>> trans_libs_;
  GLsizei mrt_count_ = 1;
  std::vector<id<MTLTexture>> mrt_targets_;  // [0] aliases target_.
  id<MTLTexture> msaa_target_ = nil;  // Aliases msaa_targets_[0] when present.
  std::vector<id<MTLTexture>> msaa_targets_;  // Per-attachment MSAA (Phase 4).
  id<MTLTexture> msaa_depth_ = nil;
  id<MTLTexture> msaa_ds_ = nil;
  GLsizei msaa_width_ = 0;
  GLsizei msaa_height_ = 0;
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
