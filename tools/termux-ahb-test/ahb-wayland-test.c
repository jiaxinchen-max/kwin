/* SPDX-License-Identifier: MIT */
#include "termux-ahardware-buffer-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <errno.h>
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-client.h>

#ifndef AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
#define AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM 5
#endif

struct android_api {
    void *library;
    int (*allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
    void (*release)(AHardwareBuffer *);
    int (*send)(const AHardwareBuffer *, int);
};

struct app {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct termux_ahardware_buffer_manager_v1 *buffer_manager;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *wl_buffer;
    struct android_api android;
    AHardwareBuffer *hardware_buffer;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLImageKHR egl_image;
    GLuint texture;
    GLuint framebuffer;
    bool configured;
    bool released;
};

static void die(const char *message)
{
    fprintf(stderr, "termux-ahb-test: %s (errno=%d)\n", message, errno);
    exit(1);
}

static bool load_android_api(struct android_api *api)
{
    api->library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!api->library) {
        return false;
    }
    api->allocate = dlsym(api->library, "AHardwareBuffer_allocate");
    api->release = dlsym(api->library, "AHardwareBuffer_release");
    api->send = dlsym(api->library, "AHardwareBuffer_sendHandleToUnixSocket");
    return api->allocate && api->release && api->send;
}

static void handle_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct app *app = data;
    app->released = true;
    fprintf(stderr, "termux-ahb-test: wl_buffer.release received\n");
}

static const struct wl_buffer_listener buffer_listener = {
    .release = handle_buffer_release,
};

static bool create_gpu_buffer(struct app *app, uint32_t width, uint32_t height)
{
    const AHardwareBuffer_Desc description = {
        .width = width,
        .height = height,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
    };
    if (app->android.allocate(&description, &app->hardware_buffer) != 0) {
        return false;
    }

    app->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (app->egl_display == EGL_NO_DISPLAY || !eglInitialize(app->egl_display, &major, &minor)) {
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        return false;
    }
    EGLConfig config = NULL;
    EGLint config_count = 0;
    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    if (!eglChooseConfig(app->egl_display, config_attributes, &config, 1, &config_count) || config_count == 0) {
        return false;
    }
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    app->egl_context = eglCreateContext(app->egl_display, config, EGL_NO_CONTEXT, context_attributes);
    if (app->egl_context == EGL_NO_CONTEXT || !eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, app->egl_context)) {
        return false;
    }

    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC get_native_client_buffer =
        (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!get_native_client_buffer || !image_target_texture) {
        return false;
    }
    EGLClientBuffer client_buffer = get_native_client_buffer(app->hardware_buffer);
    const EGLint image_attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    app->egl_image = eglCreateImageKHR(app->egl_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                       client_buffer, image_attributes);
    if (app->egl_image == EGL_NO_IMAGE_KHR) {
        return false;
    }

    glGenTextures(1, &app->texture);
    glBindTexture(GL_TEXTURE_2D, app->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    image_target_texture(GL_TEXTURE_2D, app->egl_image);
    glGenFramebuffers(1, &app->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, app->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app->texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return false;
    }

    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.04f, 0.07f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(24, 24, width - 48, height - 48);
    glClearColor(0.05f, 0.70f, 0.45f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(width / 3, height / 3, width / 3, height / 3);
    glClearColor(0.95f, 0.35f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
        return false;
    }
    if (app->android.send(app->hardware_buffer, sockets[0]) != 0) {
        close(sockets[0]);
        close(sockets[1]);
        return false;
    }
    app->wl_buffer = termux_ahardware_buffer_manager_v1_create_buffer(app->buffer_manager, sockets[1]);
    close(sockets[0]);
    close(sockets[1]);
    if (!app->wl_buffer) {
        return false;
    }
    wl_buffer_add_listener(app->wl_buffer, &buffer_listener, app);
    return true;
}

static void handle_xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    struct app *app = data;
    xdg_surface_ack_configure(surface, serial);
    if (app->configured) {
        return;
    }
    app->configured = true;
    if (!create_gpu_buffer(app, 640, 420)) {
        die("failed to create or export GPU AHardwareBuffer");
    }
    wl_surface_attach(app->surface, app->wl_buffer, 0, 0);
    wl_surface_damage_buffer(app->surface, 0, 0, 640, 420);
    wl_surface_commit(app->surface);
    fprintf(stderr, "termux-ahb-test: submitted 640x420 GPU AHardwareBuffer\n");
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_xdg_surface_configure,
};

static void handle_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = handle_ping,
};

static void handle_global(void *data, struct wl_registry *registry, uint32_t name,
                          const char *interface, uint32_t version)
{
    struct app *app = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 6 ? version : 6);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, termux_ahardware_buffer_manager_v1_interface.name) == 0) {
        app->buffer_manager = wl_registry_bind(registry, name, &termux_ahardware_buffer_manager_v1_interface, 1);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

int main(void)
{
    struct app app = {0};
    if (!load_android_api(&app.android)) {
        die("Android 8+ AHardwareBuffer API is unavailable");
    }
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        die("failed to connect to Wayland display");
    }
    struct wl_registry *registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);
    if (!app.compositor || !app.wm_base || !app.buffer_manager) {
        die("required Wayland globals are unavailable");
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_set_title(app.toplevel, "Termux AHardwareBuffer GPU test");
    xdg_toplevel_set_app_id(app.toplevel, "org.termux.AHardwareBufferTest");
    wl_surface_commit(app.surface);

    while (wl_display_dispatch(app.display) >= 0) {
    }
    return 0;
}
