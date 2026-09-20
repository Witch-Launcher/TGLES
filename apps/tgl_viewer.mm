// TGL interactive viewer (macOS Intel, Darwin-only).
// OpenGL ES 3.2 -> TGL -> Metal, on-screen in its own window.
//
// What it proves (all through the real TGL path, no raw Metal drawing):
// - GLES managers own the scene: ARRAY_BUFFERs (xyz + rgba), VAO attribs
//   0/1, program with `u_modelViewProj` (GLSL ES 3.20 source below),
//   draw-FBO COLOR_ATTACHMENT0 sizing the target.
// - Per-frame CPU lighting bakes directional diffuse into the color stream
//   (apps/cube_render.h primitives: TriNormal/ShadeFace), then
//   GlesContext::RenderFrame uploads interleaved float4 pairs + MVP and the
//   Apple bridge draws with its MSL twin and presents the drawable, so the
//   window shows real Metal pixels (Present blits target -> drawable).
// - Scene: a row of tall vertical bars (pure RGB primaries) + one spinning
//   RGB cube + ground quad. A DEPTH_COMPONENT24 renderbuffer feeds the
//   bridge's real GPU depth test (LESS), so occlusion is resolved per pixel;
//   the CPU painter sort remains as a stable order underneath.
// - HUD (bilingual VI/EN, toggle in the panel header or the L key; default
//   follows the system locale): FPS, frame-time ms + scrolling bar graph,
//   verts/tris, bridge serials, camera values, GL/Metal errors, GLSL->MSL
//   mapping, controls.
// - Responsive layout (pure test-window UI): the Metal view takes all free
//   space, the 360pt HUD panel stays pinned right, and Auto Layout shares
//   extra height between the stats and help panes when the window is
//   enlarged (scroll + minimum heights + window min size when shrunk).
// - Camera: left-drag orbits (yaw/pitch), right-drag or Shift+drag pans,
//   wheel zooms, Space toggles spin, R resets, L flips VI/EN.
//
// Build: cmake --build build --target tgl_viewer && ./build/tgl_viewer
// (CWD anywhere; no PPM output, the window IS the output).

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cube_model.h"
#include "cube_render.h"
#include "tgles/gles.h"
#include "tgles/metal_bridge_apple.h"

namespace {

// ---------------------------------------------------------------- math ---

using tgles::trial::Mat4;

Mat4 MatScale(float sx, float sy, float sz) {
  Mat4 o = {};
  o.m[0] = sx;
  o.m[5] = sy;
  o.m[10] = sz;
  o.m[15] = 1.0f;
  return o;
}

Mat4 MatRotateX(float rad) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  Mat4 o = {};
  o.m[0] = 1.0f;
  o.m[5] = c;
  o.m[6] = s;
  o.m[9] = -s;
  o.m[10] = c;
  o.m[15] = 1.0f;
  return o;
}

inline void XformPoint(const Mat4& m, const float p[3], float out[3]) {
  out[0] = m.m[0] * p[0] + m.m[4] * p[1] + m.m[8] * p[2] + m.m[12];
  out[1] = m.m[1] * p[0] + m.m[5] * p[1] + m.m[9] * p[2] + m.m[13];
  out[2] = m.m[2] * p[0] + m.m[6] * p[1] + m.m[10] * p[2] + m.m[14];
}

inline void Normalize3(float v[3]) {
  const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 1e-8f) {
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
  }
}

// --------------------------------------------------------------- scene ---

struct Bar {
  float x, z;      // footprint center
  float w, d, h;   // size (h is vertical)
  float r, g, b;   // base color
};

// A street of tall vertical blocks, like a 3D bar chart / mini city.
// Every block uses only the pure RGB primaries, cycling Red/Green/Blue.
const Bar kBars[] = {
    {-6.0f, 0.0f, 1.1f, 1.1f, 1.6f, 1.0f, 0.0f, 0.0f},  // Red
    {-4.0f, 0.4f, 1.1f, 1.1f, 2.8f, 0.0f, 1.0f, 0.0f},  // Green
    {-2.0f, -0.3f, 1.2f, 1.2f, 4.2f, 0.0f, 0.0f, 1.0f},  // Blue
    {0.0f, 0.3f, 1.2f, 1.2f, 3.1f, 1.0f, 0.0f, 0.0f},   // Red
    {2.0f, -0.4f, 1.1f, 1.1f, 5.0f, 0.0f, 1.0f, 0.0f},  // Green
    {4.0f, 0.2f, 1.1f, 1.1f, 2.2f, 0.0f, 0.0f, 1.0f},   // Blue
    {6.0f, -0.1f, 1.1f, 1.1f, 3.6f, 1.0f, 0.0f, 0.0f},  // Red
};
constexpr float kGroundY = -1.6f;
constexpr float kGroundHalf = 11.0f;

struct Camera {
  float yaw = 0.55f;
  float pitch = 0.38f;
  float dist = 12.0f;
  float tx = 0.0f, ty = 1.1f, tz = 0.0f;
};

Mat4 ViewMatrix(const Camera& c) {
  // World -> view: T(0,0,-dist) * Rx(pitch) * Ry(yaw) * T(-target).
  const Mat4 t1 = Mat4::Translate(-c.tx, -c.ty, -c.tz);
  const Mat4 r = Mat4::Multiply(MatRotateX(c.pitch), Mat4::RotateY(c.yaw));
  // NOTE: ViewMatrix convention here is orbit-style: positive pitch looks
  // slightly down at the street. Sign verified visually on Intel Mac.
  const Mat4 t2 = Mat4::Translate(0.0f, 0.0f, -c.dist);
  return Mat4::Multiply(t2, Mat4::Multiply(r, t1));
}

struct RawTri {
  float w[3][3];
  float base[3];
  float z;  // average view-space z (ascending = far first)
};

// Builds one lit, depth-sorted frame into xyz + rgba streams.
// World-space lighting (normals from world tris, world light dir); depth
// keys from view-space z; emitted positions are WORLD (the MVP uniform
// applies P*V on the GPU). Count is always a multiple of 3.
void BuildSceneFrame(const Mat4& view, const float light_dir[3], float spin,
                     std::vector<float>& out_xyz,
                     std::vector<float>& out_rgba) {
  using tgles::trial::CubeModel;
  using tgles::trial::ShadeFace;
  using tgles::trial::TriNormal;
  std::vector<RawTri> tris;
  tris.reserve(110);

  auto pushBox = [&](float cx, float cy, float cz, float sx, float sy,
                     float sz, float r, float g, float b, float spin_a,
                     bool face_colors) {
    const Mat4 model = Mat4::Multiply(
        Mat4::Translate(cx, cy, cz),
        Mat4::Multiply(Mat4::RotateY(spin_a), MatScale(sx, sy, sz)));
    for (int t = 0; t < 12; ++t) {
      RawTri tri;
      float vz = 0.0f;
      for (int v = 0; v < 3; ++v) {
        const float* lp =
            CubeModel::kPositions + (static_cast<std::size_t>(t) * 3 + v) * 3;
        float p[3] = {lp[0], lp[1], lp[2]};
        XformPoint(model, p, tri.w[v]);
        float vp[3];
        XformPoint(view, tri.w[v], vp);
        vz += vp[2];
      }
      tri.z = vz / 3.0f;
      if (face_colors) {
        // Spinner: pure RGB primaries cycling per cube face (two tris per
        // face share one primary), so every block in the scene is Red,
        // Green or Blue only. Rotation stays readable via flat shading.
        static const float kRgb[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const float* c = kRgb[(t / 2) % 3];
        tri.base[0] = c[0];
        tri.base[1] = c[1];
        tri.base[2] = c[2];
      } else {
        tri.base[0] = r;
        tri.base[1] = g;
        tri.base[2] = b;
      }
      tris.push_back(tri);
    }
  };

  for (const Bar& b : kBars) {
    pushBox(b.x, kGroundY + b.h * 0.5f, b.z, b.w * 0.5f, b.h * 0.5f,
            b.d * 0.5f, b.r, b.g, b.b, 0.0f, false);
  }
  // Center spinner: RGB faces so rotation is obvious.
  pushBox(0.0f, kGroundY + 6.1f, -2.6f, 0.7f, 0.7f, 0.7f, 1, 1, 1, spin, true);

  // Ground quad (2 tris, CCW seen from +Y so the normal points up).
  {
    const float y = kGroundY;
    const float e = kGroundHalf;
    const float q[2][3][3] = {
        {{-e, y, -e}, {-e, y, e}, {e, y, e}},
        {{-e, y, -e}, {e, y, e}, {e, y, -e}},
    };
    for (int t = 0; t < 2; ++t) {
      RawTri tri;
      float vz = 0.0f;
      for (int v = 0; v < 3; ++v) {
        tri.w[v][0] = q[t][v][0];
        tri.w[v][1] = q[t][v][1];
        tri.w[v][2] = q[t][v][2];
        float vp[3];
        XformPoint(view, tri.w[v], vp);
        vz += vp[2];
      }
      tri.z = vz / 3.0f;
      tri.base[0] = 0.32f;
      tri.base[1] = 0.33f;
      tri.base[2] = 0.36f;
      tris.push_back(tri);
    }
  }

  // Stable insertion sort, ascending view z (far first, painter's order).
  for (std::size_t i = 1; i < tris.size(); ++i) {
    RawTri key = tris[i];
    std::size_t j = i;
    while (j > 0 && tris[j - 1].z > key.z) {
      tris[j] = tris[j - 1];
      --j;
    }
    tris[j] = key;
  }

  out_xyz.clear();
  out_rgba.clear();
  out_xyz.reserve(tris.size() * 9);
  out_rgba.reserve(tris.size() * 12);
  for (const RawTri& tri : tris) {
    float n[3];
    TriNormal(tri.w[0], tri.w[1], tri.w[2], n);
    float lit[3];
    ShadeFace(tri.base, n, light_dir, lit);
    for (int v = 0; v < 3; ++v) {
      out_xyz.push_back(tri.w[v][0]);
      out_xyz.push_back(tri.w[v][1]);
      out_xyz.push_back(tri.w[v][2]);
      out_rgba.push_back(lit[0]);
      out_rgba.push_back(lit[1]);
      out_rgba.push_back(lit[2]);
      out_rgba.push_back(1.0f);
    }
  }
}

const char* kVsSrc =
    "#version 320 es\n"
    "uniform mat4 u_modelViewProj;\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec4 a_col;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "  gl_Position = u_modelViewProj * vec4(a_pos, 1.0);\n"
    "  v_col = a_col;\n"
    "}\n";
const char* kFsSrc =
    "#version 320 es\n"
    "precision mediump float;\n"
    "in vec4 v_col;\n"
    "layout(location = 0) out vec4 o_col;\n"
    "void main() { o_col = v_col; }\n";

const char* kMslNote =
    "MSL (bridge, buffer(0)=MVP, idx1=verts):\n"
    "vertex Varyings vert_main(VertexIn in\n"
    "  [[stage_in]], constant Uniforms& uni\n"
    "  [[buffer(0)]]) {\n"
    "  out.position = uni.u_modelViewProj\n"
    "    * in.position; out.color = in.color; }";

}  // namespace

// ------------------------------------------------------- app state -------

static tgles::GlesContext* g_ctx = nullptr;
static std::unique_ptr<tgles::metal_bridge::MetalBridge> g_bridge;
static tgles::GLuint g_posBuf = 0, g_colBuf = 0, g_vao = 0, g_prog = 0;
static tgles::GLint g_mvpLoc = -1;
static tgles::GLuint g_tex = 0, g_fbo = 0, g_depthRb = 0;
static int g_fbW = 0, g_fbH = 0;
static Camera g_cam;
static float g_spin = 0.0f;
static bool g_autoSpin = true;
static float g_light[3] = {0.45f, 0.75f, 0.55f};

// Stats ring.
static const int kHistN = 180;
static float g_hist[kHistN] = {0};
static int g_histHead = 0, g_histCount = 0;
static double g_lastT = 0.0;
static float g_fpsEma = 60.0f;
static float g_msEma = 16.6f;
static float g_worstMs = 0.0f;
static int g_frame = 0;
static int g_lastVertCount = 0;
// Last-frame pipeline status (formatted per UI language in refreshStats).
static bool g_lastOk = true;
static unsigned g_lastCtxErr = 0;
static unsigned g_lastBrErr = 0;
static NSTextView* g_statsView = nil;

// Test-window UI state (pure UI strings/layout, unrelated to TGL core).
static bool g_english = false;  // false = Vietnamese, true = English.
static bool g_langDirty = true;
static NSWindow* g_win = nil;
static NSTextField* g_titleLabel = nil;
static NSTextView* g_helpView = nil;
static NSSegmentedControl* g_langSeg = nil;
static NSView* g_graphViewShared = nil;

static NSString* WindowTitle() {
  return g_english ? @"TGL — GLES 3.2 → Metal | Lit pillars (Intel Mac)"
                   : @"TGL — GLES 3.2 → Metal | Khối dọc có ánh sáng (Intel Mac)";
}

static NSString* BuildHelpText() {
  NSString* shaders = [NSString
      stringWithFormat:@"\n%s\n%s\n\n%s", kVsSrc, kFsSrc, kMslNote];
  if (g_english) {
    return [[@"CAMERA\n"
             @"• Left-drag: orbit around the blocks\n"
             @"• Right-drag / Shift+drag: pan the target\n"
             @"• Scroll wheel: zoom in / out\n"
             @"• Space: toggle auto-spin | R: reset cam | L: VI/EN\n\n"
             @"FRAME PATH\n"
             @"GLES 3.2 (buffers/VAO/program/FBO)\n"
             @" → GlesContext::RenderFrame\n"
             @" → MSL translated from GLSL ES 3.20\n"
             @" → Metal draw + present to window\n\n"
             @"ORIGINAL SHADERS (GLSL ES 3.20):" stringByAppendingString:shaders]
        stringByAppendingString:
            @"\n\nDepth is real here: GPU depth test (DEPTH24, LESS)\n"
             @"resolves occlusion per pixel; the CPU painter sort\n"
             @"remains as a stable order underneath."];
  }
  return [[@"ĐIỀU KHIỂN CAM\n"
           @"• Kéo trái: xoay quanh phố khối\n"
           @"• Kéo phải / Shift+kéo: di chuyển mục tiêu\n"
           @"• Cuộn chuột: phóng to / thu nhỏ\n"
           @"• Space: bật/tắt tự xoay | R: đặt lại cam | L: VI/EN\n\n"
           @"ĐƯỜNG ĐI KHUNG HÌNH\n"
           @"GLES 3.2 (buffers/VAO/program/FBO)\n"
           @" → GlesContext::RenderFrame\n"
           @" → MSL dịch từ GLSL ES 3.20\n"
           @" → Metal draw + present ra cửa sổ\n\n"
           @"SHADER GỐC (GLSL ES 3.20):" stringByAppendingString:shaders]
      stringByAppendingString:
          @"\n\nDepth ở đây là thật: GPU depth test (DEPTH24, LESS)\n"
           @"quyết định che khuất từng pixel; sort painter trên CPU\n"
           @"giữ lại như thứ tự ổn định bên dưới."];
}

// Applies language-dependent static UI (title, labels, help, graph).
// Called lazily from refreshStats when g_langDirty is set.
static void ApplyStaticLanguageUI() {
  if (g_win != nil) [g_win setTitle:WindowTitle()];
  if (g_titleLabel != nil) {
    [g_titleLabel setStringValue:(g_english ? @"Live metrics"
                                            : @"Thông số đo (trực tiếp)")];
  }
  if (g_helpView != nil) [g_helpView setString:BuildHelpText()];
  if (g_graphViewShared != nil) [g_graphViewShared setNeedsDisplay:YES];
}

static void EnsureGlTargets(int w, int h) {
  if (w <= 0 || h <= 0) return;
  if (w == g_fbW && h == g_fbH && g_tex != 0) return;
  if (w > 2048) w = 2048;
  if (h > 2048) h = 2048;
  g_ctx->textures().BindTexture(tgles::kGlTexture2d, g_tex);
  g_ctx->textures().TexImage2D(tgles::kGlTexture2d, 0, tgles::kGlRgba8, w, h,
                               0, tgles::kGlRgba, tgles::kGlUnsignedByte,
                               nullptr);
  // Attachment snapshots size at attach time -> must re-attach on resize.
  g_ctx->framebuffers().BindFramebuffer(tgles::kGlDrawFramebuffer, g_fbo);
  g_ctx->framebuffers().FramebufferTexture2D(
      tgles::kGlDrawFramebuffer, tgles::kGlColorAttachment0,
      tgles::kGlTexture2d, g_tex, 0);
  // DEPTH_COMPONENT24 renderbuffer, same size: the bridge runs a real GPU
  // depth test (LESS), which is what fixes painter-sort artifacts like the
  // ground slab covering blocks.
  g_ctx->renderbuffers().BindRenderbuffer(tgles::kGlRenderbuffer, g_depthRb);
  g_ctx->renderbuffers().RenderbufferStorage(
      tgles::kGlRenderbuffer, tgles::kGlDepthComponent24, w, h);
  g_ctx->framebuffers().FramebufferRenderbuffer(
      tgles::kGlDrawFramebuffer, tgles::kGlDepthAttachment,
      tgles::kGlRenderbuffer, g_depthRb);
  g_fbW = w;
  g_fbH = h;
  g_bridge->Resize(w, h);
}

// ---------------------------------------------------------------- views ---

@interface FrameGraphView : NSView
@end

@implementation FrameGraphView
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  NSRect b = [self bounds];
  [[NSColor colorWithWhite:0.10 alpha:1.0] setFill];
  NSRectFill(b);
  if (g_histCount < 2) return;
  const float maxMs = 40.0f;
  const int n = g_histCount < kHistN ? g_histCount : kHistN;
  const CGFloat bw = b.size.width / (CGFloat)kHistN;
  for (int i = 0; i < n; ++i) {
    int idx = (g_histHead - n + i + kHistN * 2) % kHistN;
    float ms = g_hist[idx];
    CGFloat h = (CGFloat)(ms / maxMs) * b.size.height;
    if (h > b.size.height) h = b.size.height;
    NSColor* c = [NSColor systemGreenColor];
    if (ms > 33.3f)
      c = [NSColor systemRedColor];
    else if (ms > 17.5f)
      c = [NSColor systemYellowColor];
    [c setFill];
    NSRectFill(NSMakeRect(b.size.width - (n - i) * bw, 0, bw - 0.5, h));
  }
  // 16.7ms + 33.3ms guides.
  [[NSColor colorWithWhite:1.0 alpha:0.35] setStroke];
  NSBezierPath* p = [NSBezierPath bezierPath];
  CGFloat y16 = (CGFloat)(16.7f / maxMs) * b.size.height;
  CGFloat y33 = (CGFloat)(33.3f / maxMs) * b.size.height;
  [p moveToPoint:NSMakePoint(0, y16)];
  [p lineToPoint:NSMakePoint(b.size.width, y16)];
  [p moveToPoint:NSMakePoint(0, y33)];
  [p lineToPoint:NSMakePoint(b.size.width, y33)];
  [p stroke];
  NSString* s = g_english
      ? [NSString stringWithFormat:@"%.1f ms — green <17.5, yellow <33.3, "
                                   @"red = drop",
                                   g_msEma]
      : [NSString stringWithFormat:@"%.1f ms — xanh <17.5, vàng <33.3, "
                                   @"đỏ = drop",
                                   g_msEma];
  NSDictionary* attrs = @{
    NSFontAttributeName : [NSFont systemFontOfSize:10],
    NSForegroundColorAttributeName : [NSColor whiteColor]
  };
  [s drawAtPoint:NSMakePoint(6, b.size.height - 18) withAttributes:attrs];
}
@end

@interface MetalCamView : NSView {
 @public
  NSPoint lastPt;
  BOOL panning;
}
@end

@implementation MetalCamView
- (instancetype)initWithFrame:(NSRect)f {
  if ((self = [super initWithFrame:f]) != nil) {
    self.wantsLayer = YES;
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    self.layer = layer;
  }
  return self;
}
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (void)mouseDown:(NSEvent*)e {
  lastPt = [self convertPoint:[e locationInWindow] fromView:nil];
  panning = (([e modifierFlags] & NSEventModifierFlagShift) != 0);
  [[self window] makeFirstResponder:self];
}
- (void)rightMouseDown:(NSEvent*)e {
  lastPt = [self convertPoint:[e locationInWindow] fromView:nil];
  panning = YES;
}
- (void)mouseDragged:(NSEvent*)e {
  NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
  float dx = (float)(p.x - lastPt.x);
  float dy = (float)(p.y - lastPt.y);
  lastPt = p;
  if (panning) {
    const float k = g_cam.dist / 600.0f;
    const float cy = std::cos(g_cam.yaw), sy = std::sin(g_cam.yaw);
    const float cp = std::cos(g_cam.pitch), sp = std::sin(g_cam.pitch);
    // Camera right + up in world space (orbit convention).
    const float rx = cy, rz = -sy;
    const float ux = -sy * sp, uy = cp, uz = -cy * sp;
    g_cam.tx -= (rx * dx + ux * (-dy)) * k;
    g_cam.ty -= uy * (-dy) * k;
    g_cam.tz -= (rz * dx + uz * (-dy)) * k;
    if (g_cam.ty < -1.0f) g_cam.ty = -1.0f;
    if (g_cam.ty > 8.0f) g_cam.ty = 8.0f;
  } else {
    g_cam.yaw -= dx * 0.006f;
    g_cam.pitch -= dy * 0.006f;
    if (g_cam.pitch < -1.35f) g_cam.pitch = -1.35f;
    if (g_cam.pitch > 1.35f) g_cam.pitch = 1.35f;
  }
}
- (void)rightMouseDragged:(NSEvent*)e {
  [self mouseDragged:e];
}
- (void)scrollWheel:(NSEvent*)e {
  float d = 0.0f;
  if ([e hasPreciseScrollingDeltas])
    d = (float)[e scrollingDeltaY];
  else
    d = (float)[e deltaY] * 10.0f;
  g_cam.dist *= (1.0f - d * 0.0012f);
  if (g_cam.dist < 3.5f) g_cam.dist = 3.5f;
  if (g_cam.dist > 34.0f) g_cam.dist = 34.0f;
}
- (void)keyDown:(NSEvent*)e {
  NSString* ch = [e charactersIgnoringModifiers];
  if ([ch isEqualToString:@" "]) {
    g_autoSpin = !g_autoSpin;
  } else if ([ch isEqualToString:@"r"] || [ch isEqualToString:@"R"]) {
    g_cam = Camera();
  } else if ([ch isEqualToString:@"l"] || [ch isEqualToString:@"L"]) {
    g_english = !g_english;
    if (g_langSeg != nil) [g_langSeg setSelectedSegment:(g_english ? 1 : 0)];
    g_langDirty = true;
  } else {
    [super keyDown:e];
  }
}
@end

// -------------------------------------------------------------- renderer ---

@interface ViewerRenderer : NSObject {
 @public
  MetalCamView* metalView;
  FrameGraphView* graphView;
  NSTimer* timer;
  CAMetalLayer* metalLayer;
  std::vector<float> xyz;
  std::vector<float> rgba;
}
- (void)tick:(NSTimer*)t;
- (void)renderFrame;
- (void)refreshStats;
- (void)langChanged:(id)sender;
@end

@implementation ViewerRenderer

- (void)tick:(NSTimer*)t {
  (void)t;
  [self renderFrame];
}

- (void)renderFrame {
  if (g_ctx == nullptr || !g_bridge || !g_bridge->IsInitialized()) return;
  double now = CFAbsoluteTimeGetCurrent();
  float dt = g_lastT > 0.0 ? (float)(now - g_lastT) : 1.0f / 60.0f;
  if (dt <= 0.0f) dt = 0.0001f;
  if (dt > 0.25f) dt = 0.25f;  // app was in background / resize stall

  if (g_autoSpin) g_spin += dt * 0.55f;

  // Backing-pixel size of the Metal view.
  NSSize pts = [metalView bounds].size;
  NSSize px = [metalView convertSizeToBacking:pts];
  int w = (int)llround(px.width), h = (int)llround(px.height);
  if (w < 8 || h < 8) {
    g_lastT = now;
    return;
  }
  EnsureGlTargets(w, h);

  const float aspect = (float)w / (float)h;
  const float kPi = 3.141592653589793f;
  Mat4 view = ViewMatrix(g_cam);
  Mat4 proj = Mat4::Perspective(kPi / 3.0f, aspect, 0.1f, 120.0f);
  Mat4 mvp = Mat4::Multiply(proj, view);

  BuildSceneFrame(view, g_light, g_spin, xyz, rgba);
  const int vcount = (int)(xyz.size() / 3);
  g_lastVertCount = vcount;

  double t0 = CFAbsoluteTimeGetCurrent();
  g_ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, g_posBuf);
  g_ctx->buffers().BufferData(tgles::kGlArrayBuffer,
                              (tgles::GLsizeiptr)(xyz.size() * sizeof(float)),
                              xyz.data(), tgles::kGlDynamicDraw);
  g_ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, g_colBuf);
  g_ctx->buffers().BufferData(tgles::kGlArrayBuffer,
                              (tgles::GLsizeiptr)(rgba.size() * sizeof(float)),
                              rgba.data(), tgles::kGlDynamicDraw);
  g_ctx->programs().UniformMatrix4fv(g_mvpLoc, 1, tgles::kGlFalse, mvp.m);

  bool ok = g_ctx->RenderFrame(*g_bridge, tgles::kGlTriangles, 0, vcount);
  tgles::GLenum ctxErr = g_ctx->GetError();
  if (ok) {
    ok = g_bridge->Present();
    if (ok) g_bridge->WaitForCompletion(g_bridge->FrameSerial());
  }
  tgles::GLenum brErr = g_bridge->GetError();
  double t1 = CFAbsoluteTimeGetCurrent();

  float ms = (float)((t1 - t0) * 1000.0);
  g_hist[g_histHead] = ms;
  g_histHead = (g_histHead + 1) % kHistN;
  if (g_histCount < kHistN) ++g_histCount;
  if (ms > g_worstMs) g_worstMs = ms;
  g_msEma += (ms - g_msEma) * 0.06f;
  float instFps = 1.0f / dt;
  g_fpsEma += (instFps - g_fpsEma) * 0.05f;
  ++g_frame;
  g_lastT = now;

  g_lastOk = ok;
  g_lastCtxErr = ctxErr;
  g_lastBrErr = brErr;

  [graphView setNeedsDisplay:YES];
  if (g_langDirty || (g_frame % 15) == 0) [self refreshStats];
}

- (void)refreshStats {
  if (g_statsView == nil) return;
  if (g_langDirty) {
    g_langDirty = false;
    ApplyStaticLanguageUI();
  }
  char status[192];
  if (!g_lastOk) {
    if (g_english) {
      std::snprintf(status, sizeof(status),
                    "RenderFrame/Present FAILED (ctx=0x%x bridge=0x%x, "
                    "verts=%d). Check FBO size / Metal device.",
                    g_lastCtxErr, g_lastBrErr, g_lastVertCount);
    } else {
      std::snprintf(status, sizeof(status),
                    "RenderFrame/Present thất bại (ctx=0x%x bridge=0x%x, "
                    "verts=%d). Kiểm tra kích thước FBO / Metal device.",
                    g_lastCtxErr, g_lastBrErr, g_lastVertCount);
    }
  } else if (g_lastCtxErr != tgles::kGlNoError ||
             g_lastBrErr != tgles::kGlNoError) {
    if (g_english) {
      std::snprintf(status, sizeof(status),
                    "Pending GL errors: ctx=0x%x bridge=0x%x", g_lastCtxErr,
                    g_lastBrErr);
    } else {
      std::snprintf(status, sizeof(status),
                    "Có GL error tồn đọng: ctx=0x%x bridge=0x%x", g_lastCtxErr,
                    g_lastBrErr);
    }
  } else {
    std::snprintf(status, sizeof(status), "%s",
                  g_english ? "Status: OK" : "Trạng thái: OK");
  }
  char buf[2048];
  std::string ver = g_ctx->VersionReport();
  if (g_english) {
    std::snprintf(
        buf, sizeof(buf),
        "TGL VIEWER - OpenGL ES 3.2 -> Metal (Mac Intel)\n"
        "FPS %5.1f   |   frame %6.2f ms (EMA)   |   worst %.1f ms\n"
        "Frame #%d   |   %d verts / %d tris   |   draws %u\n"
        "Bridge serial %llu done %llu swap %llu\n"
        "Cam yaw %+.2f pitch %+.2f dist %.1f target (%.1f, %.1f, %.1f)%s\n"
        "Spun %.2f rad | light (%.2f, %.2f, %.2f)\n"
        "FBO %dx%d | %s\n"
        "%s\n"
        "- GLES3.2: glDrawArrays(TRIANGLES) + u_modelViewProj\n"
        "- MSL: buffer(0) MVP, attr0 pos, attr1 col, stride 32\n"
        "- GPU depth (DEPTH24, LESS) + painter order + CPU diffuse\n",
        g_fpsEma, g_msEma, g_worstMs, g_frame, g_lastVertCount,
        g_lastVertCount / 3, g_bridge->DrawCount(),
        (unsigned long long)g_bridge->FrameSerial(),
        (unsigned long long)g_bridge->CompletedSerial(),
        (unsigned long long)g_bridge->SwapCount(), g_cam.yaw, g_cam.pitch,
        g_cam.dist, g_cam.tx, g_cam.ty, g_cam.tz,
        g_autoSpin ? " [spinning]" : "", g_spin, g_light[0], g_light[1],
        g_light[2], g_fbW, g_fbH, ver.c_str(), status);
  } else {
    std::snprintf(
        buf, sizeof(buf),
        "TGL VIEWER - OpenGL ES 3.2 -> Metal (Mac Intel)\n"
        "FPS %5.1f   |   frame %6.2f ms (EMA)   |   worst %.1f ms\n"
        "Frame #%d   |   %d verts / %d tris   |   draws %u\n"
        "Bridge serial %llu done %llu swap %llu\n"
        "Cam yaw %+.2f pitch %+.2f dist %.1f tgt (%.1f, %.1f, %.1f)%s\n"
        "Đã xoay %.2f rad | đèn (%.2f, %.2f, %.2f)\n"
        "FBO %dx%d | %s\n"
        "%s\n"
        "- GLES3.2: glDrawArrays(TRIANGLES) + u_modelViewProj\n"
        "- MSL: buffer(0) MVP, attr0 pos, attr1 col, stride 32\n"
        "- GPU depth (DEPTH24, LESS) + painter order + diffuse CPU\n",
        g_fpsEma, g_msEma, g_worstMs, g_frame, g_lastVertCount,
        g_lastVertCount / 3, g_bridge->DrawCount(),
        (unsigned long long)g_bridge->FrameSerial(),
        (unsigned long long)g_bridge->CompletedSerial(),
        (unsigned long long)g_bridge->SwapCount(), g_cam.yaw, g_cam.pitch,
        g_cam.dist, g_cam.tx, g_cam.ty, g_cam.tz, g_autoSpin ? " [xoay]" : "",
        g_spin, g_light[0], g_light[1], g_light[2], g_fbW, g_fbH, ver.c_str(),
        status);
  }
  NSString* s = [NSString stringWithUTF8String:buf];
  dispatch_async(dispatch_get_main_queue(), ^{
    [g_statsView setString:s ?: @""];
  });
}

// VI/EN segmented control in the panel header.
- (void)langChanged:(id)sender {
  const bool en = ([sender selectedSegment] == 1);
  if (en != g_english) {
    g_english = en;
    g_langDirty = true;
    [self refreshStats];
  }
}

@end

// ------------------------------------------------------------------ main ---

static bool SetupGlesScene(std::string& err) {
  g_ctx->buffers().GenBuffers(1, &g_posBuf);
  g_ctx->buffers().GenBuffers(1, &g_colBuf);
  // Pre-size stores; per-frame BufferData re-uploads (DYNAMIC_DRAW ring).
  std::vector<float> zero_xyz(1024 * 3, 0.0f), zero_rgba(1024 * 4, 0.0f);
  g_ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, g_posBuf);
  g_ctx->buffers().BufferData(tgles::kGlArrayBuffer,
                              (tgles::GLsizeiptr)(zero_xyz.size() * 4),
                              zero_xyz.data(), tgles::kGlDynamicDraw);
  g_ctx->buffers().BindBuffer(tgles::kGlArrayBuffer, g_colBuf);
  g_ctx->buffers().BufferData(tgles::kGlArrayBuffer,
                              (tgles::GLsizeiptr)(zero_rgba.size() * 4),
                              zero_rgba.data(), tgles::kGlDynamicDraw);

  g_ctx->vertex_arrays().GenVertexArrays(1, &g_vao);
  g_ctx->vertex_arrays().BindVertexArray(g_vao);
  g_ctx->vertex_arrays().VertexAttribPointer(0, 3, tgles::kGlFloat,
                                             tgles::kGlFalse, 0, 0, g_posBuf);
  g_ctx->vertex_arrays().VertexAttribPointer(1, 4, tgles::kGlFloat,
                                             tgles::kGlFalse, 0, 0, g_colBuf);
  g_ctx->vertex_arrays().EnableVertexAttribArray(0);
  g_ctx->vertex_arrays().EnableVertexAttribArray(1);

  tgles::GLuint vs = g_ctx->shaders().CreateShader(tgles::kGlVertexShader);
  g_ctx->shaders().ShaderSource(vs, 1, &kVsSrc, nullptr);
  g_ctx->shaders().CompileShader(vs);
  tgles::GLuint fs = g_ctx->shaders().CreateShader(tgles::kGlFragmentShader);
  g_ctx->shaders().ShaderSource(fs, 1, &kFsSrc, nullptr);
  g_ctx->shaders().CompileShader(fs);
  g_prog = g_ctx->programs().CreateProgram();
  g_ctx->programs().AttachShader(g_prog, vs);
  g_ctx->programs().AttachShader(g_prog, fs);
  g_ctx->programs().LinkProgram(g_prog);
  if (!g_ctx->programs().LinkSucceeded(g_prog)) {
    err = "LinkProgram that bai: " + g_ctx->programs().GetProgramInfoLog(g_prog);
    return false;
  }
  g_ctx->programs().UseProgram(g_prog);
  g_mvpLoc = g_ctx->programs().GetUniformLocation(g_prog, "u_modelViewProj");
  if (g_mvpLoc < 0) {
    err = "Khong tim thay uniform u_modelViewProj (RenderFrame can ten nay).";
    return false;
  }
  g_ctx->textures().GenTextures(1, &g_tex);
  g_ctx->framebuffers().GenFramebuffers(1, &g_fbo);
  g_ctx->renderbuffers().GenRenderbuffers(1, &g_depthRb);
  // Real GPU depth test (LESS, write on, clear 1): visibility is resolved
  // per-pixel on Metal. The CPU painter sort stays as a harmless stable
  // order underneath.
  g_ctx->foundation().Enable(tgles::kGlDepthTest);
  if (g_ctx->GetError() != tgles::kGlNoError) {
    err = "GL error ngay sau setup (kiem tra log compile/link).";
    return false;
  }
  return true;
}

int main(int argc, const char* argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    Normalize3(g_light);
    // Default UI language follows the system locale (Vietnamese stays the
    // default on vi machines); the user can flip it anytime with the VI/EN
    // segmented control or the L key. Pure test-window UI, not TGL core.
    NSString* pref =
        [[[[NSLocale preferredLanguages] firstObject] lowercaseString]
            stringByTrimmingCharactersInSet:
                [NSCharacterSet whitespaceCharacterSet]] ?: @"";
    g_english = ![pref hasPrefix:@"vi"];

    g_bridge = tgles::metal_bridge::CreateAppleBridge();
    if (!g_bridge || !g_bridge->Initialize("Apple3")) {
      NSAlert* a = [[NSAlert alloc] init];
      [a setMessageText:(g_english ? @"Cannot initialize Metal (this Intel "
                                        @"Mac needs a GPU)."
                                  : @"Không khởi tạo được Metal (Intel Mac "
                                    @"cần GPU).")];
      [a setInformativeText:
              (g_english
                   ? @"TGL needs MTLCreateSystemDefaultDevice. VMs / remote "
                     @"desktops without a GPU show this message."
                   : @"TGL cần MTLCreateSystemDefaultDevice. Máy ảo / remote "
                     @"desktop không có GPU sẽ thấy thông báo này.")];
      [a runModal];
      return 1;
    }
    static tgles::GlesContext ctx_storage = tgles::GlesContext::Create(false);
    g_ctx = &ctx_storage;
    std::string setupErr;
    if (!SetupGlesScene(setupErr)) {
      NSAlert* a = [[NSAlert alloc] init];
      [a setMessageText:(g_english ? @"Failed to build the GLES scene."
                                  : @"Dựng scene GLES thất bại.")];
      [a setInformativeText:[NSString stringWithUTF8String:setupErr.c_str()]];
      [a runModal];
      return 1;
    }

    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSRect frame = NSMakeRect(0, 0, 1160, 720);
    NSWindow* win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskResizable |
                             NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [win setTitle:WindowTitle()];
    [win setMinSize:NSMakeSize(900, 560)];
    [win center];
    g_win = win;

    ViewerRenderer* renderer = [[ViewerRenderer alloc] init];

    NSView* content = [win contentView];

    // Responsive split: Metal view takes all free space, the HUD panel keeps
    // a fixed 360pt width pinned right. Inside the panel, Auto Layout shares
    // extra height between the stats and help scroll views, so enlarging the
    // window grows content instead of leaving dead space (and shrinking never
    // overlaps text thanks to scroll + minimum heights + window min size).
    MetalCamView* mv = [[MetalCamView alloc] initWithFrame:NSZeroRect];
    NSView* panel = [[NSView alloc] initWithFrame:NSZeroRect];
    for (NSView* v in @[ mv, panel ]) {
      [v setTranslatesAutoresizingMaskIntoConstraints:NO];
      [content addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
      [mv.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
      [mv.topAnchor constraintEqualToAnchor:content.topAnchor],
      [mv.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
      [mv.trailingAnchor constraintEqualToAnchor:panel.leadingAnchor],
      [panel.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
      [panel.topAnchor constraintEqualToAnchor:content.topAnchor],
      [panel.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
      [panel.widthAnchor constraintEqualToConstant:360.0],
    ]];

    // Panel header: VI/EN switch + live-metrics title.
    NSSegmentedControl* langSeg =
        [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    [langSeg setSegmentCount:2];
    [langSeg setLabel:@"Tiếng Việt" forSegment:0];
    [langSeg setLabel:@"English" forSegment:1];
    [langSeg setSelectedSegment:(g_english ? 1 : 0)];
    [langSeg setTarget:renderer];
    [langSeg setAction:@selector(langChanged:)];
    [langSeg setToolTip:(g_english ? @"Switch UI language (or press L)"
                                   : @"Đổi ngôn ngữ UI (hoặc phím L)")];
    g_langSeg = langSeg;

    NSTextField* titleLb = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [titleLb setStringValue:(g_english ? @"Live metrics"
                                       : @"Thông số đo (trực tiếp)")];
    [titleLb setBezeled:NO];
    [titleLb setDrawsBackground:NO];
    [titleLb setEditable:NO];
    [titleLb setSelectable:NO];
    [titleLb setFont:[NSFont boldSystemFontOfSize:12]];
    g_titleLabel = titleLb;

    FrameGraphView* gv = [[FrameGraphView alloc] initWithFrame:NSZeroRect];
    g_graphViewShared = gv;

    NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    [sv setHasVerticalScroller:YES];
    NSTextView* tv = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 320, 300)];
    [tv setEditable:NO];
    [tv setSelectable:YES];
    [tv setFont:[NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular]];
    [sv setDocumentView:tv];
    g_statsView = tv;

    NSScrollView* helpScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    [helpScroll setHasVerticalScroller:YES];
    NSTextView* help = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 320, 620)];
    [help setEditable:NO];
    [help setSelectable:YES];
    [help setDrawsBackground:NO];
    [help setFont:[NSFont systemFontOfSize:11]];
    [help setString:BuildHelpText()];
    [helpScroll setDocumentView:help];
    g_helpView = help;

    for (NSView* v in @[ langSeg, titleLb, gv, sv, helpScroll ]) {
      [v setTranslatesAutoresizingMaskIntoConstraints:NO];
      [panel addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
      [langSeg.topAnchor constraintEqualToAnchor:panel.topAnchor constant:8],
      [langSeg.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor
                                           constant:10],
      [langSeg.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor
                                            constant:-10],
      [langSeg.heightAnchor constraintEqualToConstant:26],
      [titleLb.topAnchor constraintEqualToAnchor:langSeg.bottomAnchor
                                        constant:8],
      [titleLb.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor
                                           constant:10],
      [titleLb.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor
                                            constant:-10],
      [titleLb.heightAnchor constraintEqualToConstant:20],
      [gv.topAnchor constraintEqualToAnchor:titleLb.bottomAnchor constant:8],
      [gv.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor
                                       constant:10],
      [gv.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor
                                        constant:-10],
      [gv.heightAnchor constraintEqualToConstant:110],
      [sv.topAnchor constraintEqualToAnchor:gv.bottomAnchor constant:8],
      [sv.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor
                                       constant:10],
      [sv.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor
                                        constant:-10],
      [sv.heightAnchor constraintGreaterThanOrEqualToConstant:140],
      [helpScroll.topAnchor constraintEqualToAnchor:sv.bottomAnchor constant:8],
      [helpScroll.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor
                                              constant:10],
      [helpScroll.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor
                                               constant:-10],
      [helpScroll.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor
                                             constant:-8],
      [helpScroll.heightAnchor constraintGreaterThanOrEqualToConstant:120],
      // Stats and help share leftover height (help slightly smaller).
      [helpScroll.heightAnchor constraintEqualToAnchor:sv.heightAnchor
                                            multiplier:0.85],
    ]];
    g_langDirty = true;  // first refreshStats paints localized statics.

    // Wire the real drawable to the bridge (after a first layout pass so the
    // Metal view already has its constraint-driven size).
    [content layoutSubtreeIfNeeded];
    CAMetalLayer* layer = (CAMetalLayer*)[mv layer];
    NSSize back0 = [mv convertSizeToBacking:[mv bounds].size];
    int w0 = (int)back0.width, h0 = (int)back0.height;
    if (w0 < 8) w0 = 800;
    if (h0 < 8) h0 = 600;
    g_bridge->SetLayer((__bridge void*)layer, w0, h0);
    EnsureGlTargets(w0, h0);

    renderer->metalView = mv;
    renderer->graphView = gv;
    renderer->metalLayer = layer;
    renderer->timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                      target:renderer
                                                    selector:@selector(tick:)
                                                    userInfo:nil
                                                     repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:renderer->timer
                              forMode:NSRunLoopCommonModes];

    [win makeKeyAndOrderFront:nil];
    [win makeFirstResponder:mv];
    [app activateIgnoringOtherApps:YES];
    [app run];
    return 0;
  }
}
