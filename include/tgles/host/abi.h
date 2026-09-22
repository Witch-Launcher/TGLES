#ifndef TGLES_HOST_ABI_H
#define TGLES_HOST_ABI_H

// TGL host ABI: the C surface a GLES host (OpenGL ES app, MobileGL, KHR CTS)
// links or dlopens. Two rules govern this header:
//
//   1. No system GLES/EGL headers. macOS ships no GLES headers, and TGL's
//      scalar types must match Khronos widths exactly (checked against
//      docs/reference/egl.h and docs/reference/gl32.h by the tests).
//   2. Every name declared here is exported by libtgles (tgles_host) as a real
//      function. Nothing is a placeholder address: either the call is served
//      by a manager in src/state|pipeline|facade, or it is a declared gap that
//      records itself in the ledger (tglesAbiGapCount below) and returns that
//      call class's spec default. A gap is visible; a sentinel is not, and
//      MobileGL's loader treats any non-null pointer as usable.
//
// Reference: EGL 1.5 (docs/reference/egl.h) and OpenGL ES 3.2
// (docs/reference/gl32.h), both copied verbatim from Khronos.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Scalar types (Khronos widths; see docs/reference/gl32.h + egl.h)
// ---------------------------------------------------------------------------
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef int32_t GLfixed;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef char GLchar;
typedef struct __GLsync* GLsync;

typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLImage;
typedef void* EGLSync;
typedef void* EGLClientBuffer;
typedef void* EGLNativeDisplayType;
typedef void* EGLNativePixmapType;
typedef void* EGLNativeWindowType;
typedef int EGLint;
typedef unsigned int EGLenum;
typedef unsigned int EGLBoolean;
typedef intptr_t EGLAttrib;
typedef uint64_t EGLTime;

#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_IMAGE ((EGLImage)0)
#define EGL_NO_SYNC ((EGLSync)0)
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)

#define EGL_SUCCESS 0x3000
#define EGL_NOT_INITIALIZED 0x3001
#define EGL_BAD_ACCESS 0x3002
#define EGL_BAD_ALLOC 0x3003
#define EGL_BAD_ATTRIBUTE 0x3004
#define EGL_BAD_CONFIG 0x3005
#define EGL_BAD_CONTEXT 0x3006
#define EGL_BAD_CURRENT_SURFACE 0x3007
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_MATCH 0x3009
#define EGL_BAD_NATIVE_PIXMAP 0x300A
#define EGL_BAD_NATIVE_WINDOW 0x300B
#define EGL_BAD_PARAMETER 0x300C
#define EGL_BAD_SURFACE 0x300D
#define EGL_CONTEXT_LOST 0x300E

// Config attributes (docs/reference/egl.h, EGL 1.5 core).
#define EGL_BUFFER_SIZE 0x3020
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_CONFIG_CAVEAT 0x3027
#define EGL_CONFIG_ID 0x3028
#define EGL_LEVEL 0x3029
#define EGL_MAX_PBUFFER_HEIGHT 0x302A
#define EGL_MAX_PBUFFER_PIXELS 0x302B
#define EGL_MAX_PBUFFER_WIDTH 0x302C
#define EGL_NATIVE_RENDERABLE 0x302D
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_NATIVE_VISUAL_TYPE 0x302F
#define EGL_SAMPLES 0x3031
#define EGL_SAMPLE_BUFFERS 0x3032
#define EGL_SURFACE_TYPE 0x3033
#define EGL_TRANSPARENT_TYPE 0x3034
#define EGL_NONE 0x3038
#define EGL_BIND_TO_TEXTURE_RGB 0x3039
#define EGL_BIND_TO_TEXTURE_RGBA 0x303A
#define EGL_MIN_SWAP_INTERVAL 0x303B
#define EGL_MAX_SWAP_INTERVAL 0x303C
#define EGL_LUMINANCE_SIZE 0x303D
#define EGL_ALPHA_MASK_SIZE 0x303E
#define EGL_COLOR_BUFFER_TYPE 0x303F
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_MATCH_NATIVE_PIXMAP 0x3041
#define EGL_CONFORMANT 0x3042
#define EGL_SLOW_CONFIG 0x3050
#define EGL_NON_CONFORMANT_CONFIG 0x3051
#define EGL_TRANSPARENT_RGB 0x3052
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057
#define EGL_LARGEST_PBUFFER 0x3058
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A
#define EGL_CORE_NATIVE_ENGINE 0x305B
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_MULTISAMPLE_RESOLVE 0x3099
#define EGL_SWAP_BEHAVIOR 0x3093
#define EGL_BUFFER_PRESERVED 0x3094
#define EGL_BUFFER_DESTROYED 0x3095
#define EGL_MIPMAP_LEVEL 0x3083
#define EGL_CLIENT_APIS 0x308D

// Context attributes (EGL 1.5 core + EGL_KHR_create_context spellings).
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_CONTEXT_FLAGS_KHR 0x30FC
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR 0x30FD
#define EGL_CONTEXT_OPENGL_DEBUG 0x31B0
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE 0x31B1
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS 0x31B2
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY 0x31BD
#define EGL_NO_RESET_NOTIFICATION 0x31BE
#define EGL_LOSE_CONTEXT_ON_RESET 0x31BF
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x00000002

// Sync objects (EGL 1.5 core; EGL_KHR_fence_sync uses the same values).
#define EGL_SYNC_FENCE 0x30F9
#define EGL_SYNC_PRIOR_COMMANDS_COMPLETE 0x30F0
#define EGL_SYNC_TYPE 0x30F7
#define EGL_SYNC_STATUS 0x30F1
#define EGL_SYNC_CONDITION 0x30F8
#define EGL_SIGNALED 0x30F2
#define EGL_UNSIGNALED 0x30F3
#define EGL_SYNC_FLUSH_COMMANDS_BIT 0x0001
#define EGL_SYNC_FLUSH_COMMANDS_BIT_KHR 0x0001
#define EGL_TIMEOUT_EXPIRED 0x30F5
#define EGL_CONDITION_SATISFIED 0x30F6
#define EGL_ALREADY_SIGNALED 0x30FA
#define EGL_FOREVER 0xFFFFFFFFFFFFFFFFull

// Surface-type / renderable-type bits.
#define EGL_PBUFFER_BIT 0x0001
#define EGL_PIXMAP_BIT 0x0002
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_ES_BIT 0x0001
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_OPENGL_ES3_BIT 0x00000040
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#define EGL_OPENGL_BIT 0x0008

// Platform tokens (EGL 1.5 core + EGL_EXT_platform_*).
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#define EGL_PLATFORM_GBM_KHR 0x31D7
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#define EGL_PLATFORM_X11_KHR 0x31D5

// GLES constants a host needs before it can query anything else
// (docs/reference/gl32.h; the full set lives in tgles/base/gl_types.h).
// Values below are verbatim Khronos; the contract test static_asserts the
// widths, and test_abi_contract.cpp drives a real ES 3.2 conversation that
// needs these tokens (buffers, shaders, draws, version queries).
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 0x0004
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

// EGL API enum used by eglBindAPI/eglQueryAPI (docs/reference/egl.h: EGL 1.5).
#define EGL_OPENGL_ES_API 0x30A0

// ---------------------------------------------------------------------------
// EGL 1.5 entry points. All 46 names below are REQUIRED by MobileGL's loader
// (INIT_EGL_FUNC in Loader.cpp) and are resolved there with dlsym, so they
// must be exported symbols, not only eglGetProcAddress entries.
// ---------------------------------------------------------------------------
EGLDisplay eglGetDisplay(EGLNativeDisplayType native_display);
EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                 const EGLAttrib* attrib_list);
EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void* native_display,
                                    const EGLint* attrib_list);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor);
EGLBoolean eglTerminate(EGLDisplay dpy);
const char* eglQueryString(EGLDisplay dpy, EGLint name);
EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig* configs, EGLint config_size,
                         EGLint* num_config);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list,
                           EGLConfig* configs, EGLint config_size,
                           EGLint* num_config);
EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute,
                              EGLint* value);
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint* attrib_list);
EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                          void* native_window,
                                          const EGLAttrib* attrib_list);
EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                             void* native_window,
                                             const EGLint* attrib_list);
EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint* attrib_list);
EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
                                            EGLClientBuffer buffer,
                                            EGLConfig config,
                                            const EGLint* attrib_list);
EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativePixmapType pixmap,
                                  const EGLint* attrib_list);
EGLSurface eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config,
                                          void* native_pixmap,
                                          const EGLAttrib* attrib_list);
EGLSurface eglCreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                             void* native_pixmap,
                                             const EGLint* attrib_list);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                           EGLint* value);
EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                            EGLint value);
EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface,
                                       const EGLint* rects, EGLint n_rects);
EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                          EGLNativePixmapType target);
EGLBoolean eglUnlockSurfaceKHR(EGLDisplay dpy, EGLSurface surface);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint* attrib_list);
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx);
EGLContext eglGetCurrentContext(void);
EGLSurface eglGetCurrentSurface(EGLint readdraw);
EGLDisplay eglGetCurrentDisplay(void);
EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute,
                           EGLint* value);
EGLBoolean eglWaitClient(void);
EGLBoolean eglWaitGL(void);
EGLBoolean eglWaitNative(EGLint engine);
EGLBoolean eglBindAPI(EGLenum api);
EGLenum eglQueryAPI(void);
EGLBoolean eglReleaseThread(void);
EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags);
EGLBoolean eglWaitSyncKHR(EGLDisplay dpy, EGLSync sync, EGLint flags);
EGLImage eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                        EGLClientBuffer buffer, const EGLAttrib* attrib_list);
EGLBoolean eglDestroyImage(EGLDisplay dpy, EGLImage image);
EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib* attrib_list);
EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync);
EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                         EGLTime timeout);
EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute,
                            EGLAttrib* value);
EGLint eglGetError(void);
void* eglGetProcAddress(const char* procname);

// --- TGL present extensions (NOT part of the Khronos/MobileGL contract) ---
// Lets a dlopen() host attach a CAMetalLayer owned by its own window and
// present through the built library. See src/host/abi/present_abi.cpp.
// All return 1 on success, 0 on failure (bad args, no device, non-Apple).
// Resolvable via BOTH dlsym and eglGetProcAddress (same single-table rule).
int tglHostAttachMetalLayer(void* ca_metal_layer, int width, int height);
int tglHostResizeMetalLayer(int width, int height);
int tglHostPresent(void);
int tglHostReadbackPixel(int x, int y, unsigned char out_rgba[4]);
unsigned long long tglHostFrameSerial(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // TGLES_HOST_ABI_H
