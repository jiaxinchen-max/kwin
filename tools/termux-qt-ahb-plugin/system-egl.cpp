/* SPDX-License-Identifier: LGPL-3.0-only */
#include <EGL/egl.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
extern "C" void *platform_dlopen(const char *, int);
namespace {
void *systemEgl()
{
    static void *library = [] {
#if defined(__LP64__)
        constexpr auto path = "/system/lib64/libEGL.so";
#else
        constexpr auto path = "/system/lib/libEGL.so";
#endif
        void *handle = platform_dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) { std::fprintf(stderr, "termux-system-egl: cannot load %s: %s\n", path, dlerror()); std::abort(); }
        return handle;
    }();
    return library;
}
template<typename Function> Function resolve(const char *name)
{
    auto function = reinterpret_cast<Function>(dlsym(systemEgl(), name));
    if (!function) { std::fprintf(stderr, "termux-system-egl: required entry point unavailable: %s\n", name); std::abort(); }
    return function;
}
}
extern "C" const char *termux_system_egl_dispatch()
{
    systemEgl();
    return "android-system-egl-v1";
}
#define EGL_FORWARD(result, name, parameters, arguments) \
extern "C" EGLAPI result EGLAPIENTRY name parameters { \
    using Function = result (EGLAPIENTRY *) parameters; \
    static auto function = resolve<Function>(#name); \
    return function arguments; \
}
EGL_FORWARD(EGLBoolean, eglBindAPI, (EGLenum api), (api))
EGL_FORWARD(EGLBoolean, eglBindTexImage, (EGLDisplay d, EGLSurface s, EGLint b), (d,s,b))
EGL_FORWARD(EGLBoolean, eglChooseConfig, (EGLDisplay d, const EGLint *a, EGLConfig *c, EGLint n, EGLint *r), (d,a,c,n,r))
EGL_FORWARD(EGLBoolean, eglCopyBuffers, (EGLDisplay d, EGLSurface s, EGLNativePixmapType p), (d,s,p))
EGL_FORWARD(EGLContext, eglCreateContext, (EGLDisplay d, EGLConfig c, EGLContext s, const EGLint *a), (d,c,s,a))
EGL_FORWARD(EGLSurface, eglCreatePbufferFromClientBuffer, (EGLDisplay d, EGLenum t, EGLClientBuffer b, EGLConfig c, const EGLint *a), (d,t,b,c,a))
EGL_FORWARD(EGLSurface, eglCreatePbufferSurface, (EGLDisplay d, EGLConfig c, const EGLint *a), (d,c,a))
EGL_FORWARD(EGLSurface, eglCreatePixmapSurface, (EGLDisplay d, EGLConfig c, EGLNativePixmapType p, const EGLint *a), (d,c,p,a))
EGL_FORWARD(EGLSurface, eglCreateWindowSurface, (EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint *a), (d,c,w,a))
EGL_FORWARD(EGLBoolean, eglDestroyContext, (EGLDisplay d, EGLContext c), (d,c))
EGL_FORWARD(EGLBoolean, eglDestroySurface, (EGLDisplay d, EGLSurface s), (d,s))
EGL_FORWARD(EGLBoolean, eglGetConfigAttrib, (EGLDisplay d, EGLConfig c, EGLint a, EGLint *v), (d,c,a,v))
EGL_FORWARD(EGLBoolean, eglGetConfigs, (EGLDisplay d, EGLConfig *c, EGLint n, EGLint *r), (d,c,n,r))
EGL_FORWARD(EGLContext, eglGetCurrentContext, (void), ())
EGL_FORWARD(EGLDisplay, eglGetCurrentDisplay, (void), ())
EGL_FORWARD(EGLSurface, eglGetCurrentSurface, (EGLint r), (r))
EGL_FORWARD(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType d), (d))
EGL_FORWARD(EGLint, eglGetError, (void), ())
EGL_FORWARD(__eglMustCastToProperFunctionPointerType, eglGetProcAddress, (const char *n), (n))
EGL_FORWARD(EGLBoolean, eglInitialize, (EGLDisplay d, EGLint *a, EGLint *b), (d,a,b))
EGL_FORWARD(EGLBoolean, eglMakeCurrent, (EGLDisplay d, EGLSurface a, EGLSurface b, EGLContext c), (d,a,b,c))
EGL_FORWARD(EGLenum, eglQueryAPI, (void), ())
EGL_FORWARD(EGLBoolean, eglQueryContext, (EGLDisplay d, EGLContext c, EGLint a, EGLint *v), (d,c,a,v))
EGL_FORWARD(const char *, eglQueryString, (EGLDisplay d, EGLint n), (d,n))
EGL_FORWARD(EGLBoolean, eglQuerySurface, (EGLDisplay d, EGLSurface s, EGLint a, EGLint *v), (d,s,a,v))
EGL_FORWARD(EGLBoolean, eglReleaseTexImage, (EGLDisplay d, EGLSurface s, EGLint b), (d,s,b))
EGL_FORWARD(EGLBoolean, eglReleaseThread, (void), ())
EGL_FORWARD(EGLBoolean, eglSurfaceAttrib, (EGLDisplay d, EGLSurface s, EGLint a, EGLint v), (d,s,a,v))
EGL_FORWARD(EGLBoolean, eglSwapBuffers, (EGLDisplay d, EGLSurface s), (d,s))
EGL_FORWARD(EGLBoolean, eglSwapInterval, (EGLDisplay d, EGLint i), (d,i))
EGL_FORWARD(EGLBoolean, eglTerminate, (EGLDisplay d), (d))
EGL_FORWARD(EGLBoolean, eglWaitClient, (void), ())
EGL_FORWARD(EGLBoolean, eglWaitGL, (void), ())
EGL_FORWARD(EGLBoolean, eglWaitNative, (EGLint e), (e))
#ifdef EGL_VERSION_1_5
EGL_FORWARD(EGLSync, eglCreateSync, (EGLDisplay d, EGLenum t, const EGLAttrib *a), (d,t,a))
EGL_FORWARD(EGLBoolean, eglDestroySync, (EGLDisplay d, EGLSync s), (d,s))
EGL_FORWARD(EGLint, eglClientWaitSync, (EGLDisplay d, EGLSync s, EGLint f, EGLTime t), (d,s,f,t))
EGL_FORWARD(EGLBoolean, eglGetSyncAttrib, (EGLDisplay d, EGLSync s, EGLint a, EGLAttrib *v), (d,s,a,v))
EGL_FORWARD(EGLImage, eglCreateImage, (EGLDisplay d, EGLContext c, EGLenum t, EGLClientBuffer b, const EGLAttrib *a), (d,c,t,b,a))
EGL_FORWARD(EGLBoolean, eglDestroyImage, (EGLDisplay d, EGLImage i), (d,i))
EGL_FORWARD(EGLDisplay, eglGetPlatformDisplay, (EGLenum p, void *d, const EGLAttrib *a), (p,d,a))
EGL_FORWARD(EGLSurface, eglCreatePlatformWindowSurface, (EGLDisplay d, EGLConfig c, void *w, const EGLAttrib *a), (d,c,w,a))
EGL_FORWARD(EGLSurface, eglCreatePlatformPixmapSurface, (EGLDisplay d, EGLConfig c, void *p, const EGLAttrib *a), (d,c,p,a))
EGL_FORWARD(EGLBoolean, eglWaitSync, (EGLDisplay d, EGLSync s, EGLint f), (d,s,f))
#endif
