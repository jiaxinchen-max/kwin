/*
 * Minimal Termux/Bionic validation client for the Xwayland AHB DRI3 bridge.
 *
 * This is not a standard DRI3 client: the fd passed in PixmapFromBuffer is a
 * Unix socket containing an AHardwareBuffer native handle.  Xwayland imports
 * that handle as an EGLImage and the normal Present extension displays it.
 */

#include <android/hardware_buffer.h>
#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/xcb.h>

#ifndef AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
#define AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM 5
#endif

typedef EGLClientBuffer(EGLAPIENTRYP PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)(
    const AHardwareBuffer *buffer);

static void
die(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void
check_xcb(xcb_connection_t *connection, xcb_void_cookie_t cookie,
          const char *operation)
{
    xcb_generic_error_t *error = xcb_request_check(connection, cookie);
    if (!error)
        return;

    fprintf(stderr, "%s failed: X11 error %u, major %u, minor %u\n",
            operation, error->error_code, error->major_code,
            error->minor_code);
    free(error);
    exit(1);
}

int
main(void)
{
    const uint16_t width = 640;
    const uint16_t height = 480;
    int screen_number = 0;
    xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
    const xcb_setup_t *setup;
    xcb_screen_iterator_t screen_iterator;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_pixmap_t pixmap;
    uint32_t window_values[2];
    const xcb_query_extension_reply_t *dri3;
    const xcb_query_extension_reply_t *present;
    AHardwareBuffer_Desc description = {0};
    AHardwareBuffer *buffer = NULL;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    EGLint config_count = 0;
    GLuint texture = 0;
    GLuint framebuffer = 0;
    EGLImageKHR image;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC get_native_client_buffer;
    PFNEGLCREATEIMAGEKHRPROC create_image;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture;
    EGLClientBuffer egl_buffer;
    int sockets[2] = {-1, -1};
    uint32_t dri3_stride;
    void *android_library;
    int (*allocate_ahb)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
    void (*describe_ahb)(const AHardwareBuffer *, AHardwareBuffer_Desc *);
    int (*send_ahb)(const AHardwareBuffer *, int);

    if (xcb_connection_has_error(connection))
        die("cannot connect to X11 display");

    dri3 = xcb_get_extension_data(connection, &xcb_dri3_id);
    present = xcb_get_extension_data(connection, &xcb_present_id);
    if (!dri3 || !dri3->present || !present || !present->present)
        die("Xwayland does not expose DRI3 and Present");

    setup = xcb_get_setup(connection);
    screen_iterator = xcb_setup_roots_iterator(setup);
    while (screen_number-- > 0)
        xcb_screen_next(&screen_iterator);
    screen = screen_iterator.data;

    window = xcb_generate_id(connection);
    window_values[0] = screen->black_pixel;
    window_values[1] = XCB_EVENT_MASK_EXPOSURE |
                       XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    check_xcb(connection,
              xcb_create_window_checked(
                  connection, screen->root_depth, window, screen->root,
                  40, 40, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                  screen->root_visual,
                  XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, window_values),
              "CreateWindow");
    check_xcb(connection, xcb_map_window_checked(connection, window),
              "MapWindow");
    xcb_flush(connection);

    android_library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!android_library)
        die("cannot load libandroid.so");
    allocate_ahb = dlsym(android_library, "AHardwareBuffer_allocate");
    describe_ahb = dlsym(android_library, "AHardwareBuffer_describe");
    send_ahb = dlsym(android_library,
                     "AHardwareBuffer_sendHandleToUnixSocket");
    if (!allocate_ahb || !describe_ahb || !send_ahb)
        die("AHardwareBuffer API is unavailable");

    description.width = width;
    description.height = height;
    description.layers = 1;
    /* KWin's Android wl_buffer import compensates for the system EGL
     * RGBA/BGRA channel inversion.  Client render targets therefore use the
     * normal RGBA format; Xwayland's own X pixmaps remain BGRA. */
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
                        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if (allocate_ahb(&description, &buffer) != 0)
        die("AHardwareBuffer_allocate failed");
    describe_ahb(buffer, &description);
    dri3_stride = (description.stride ? description.stride : width) * 4;
    printf("AHB: %ux%u stride=%u format=%u usage=0x%llx, X depth=%u\n",
           description.width, description.height, description.stride,
           description.format, (unsigned long long)description.usage,
           screen->root_depth);

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, NULL, NULL))
        die("eglInitialize failed");
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        die("eglBindAPI failed");
    {
        const EGLint config_attributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };
        const EGLint pbuffer_attributes[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE,
        };
        const EGLint context_attributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE,
        };

        if (!eglChooseConfig(egl_display, config_attributes, &egl_config, 1,
                             &config_count) || config_count == 0)
            die("eglChooseConfig failed");
        egl_surface = eglCreatePbufferSurface(egl_display, egl_config,
                                              pbuffer_attributes);
        egl_context = eglCreateContext(egl_display, egl_config,
                                       EGL_NO_CONTEXT, context_attributes);
    }
    if (egl_surface == EGL_NO_SURFACE || egl_context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context))
        die("cannot create Android GLES context");

    printf("EGL renderer: %s\n", glGetString(GL_RENDERER));
    get_native_client_buffer = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
        eglGetProcAddress("eglGetNativeClientBufferANDROID");
    create_image = (PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    image_target_texture = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!get_native_client_buffer || !create_image || !image_target_texture)
        die("required Android EGLImage entry points are missing");

    /* gfxstream requires the texture name to be allocated before importing
     * an Android native buffer. */
    glGenTextures(1, &texture);
    egl_buffer = get_native_client_buffer(buffer);
    image = create_image(egl_display, EGL_NO_CONTEXT,
                         EGL_NATIVE_BUFFER_ANDROID, egl_buffer,
                         (const EGLint[]){EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
                                          EGL_NONE});
    if (image == EGL_NO_IMAGE_KHR)
        die("eglCreateImageKHR(AHardwareBuffer) failed");
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context))
        die("cannot restore GLES context after AHB import");

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    image_target_texture(GL_TEXTURE_2D, image);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("AHardwareBuffer framebuffer is incomplete");

    glViewport(0, 0, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, width, height);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(32, 32, 176, height - 64);
    glClearColor(0.95f, 0.15f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(232, 32, 176, height - 64);
    glClearColor(0.10f, 0.85f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(432, 32, 176, height - 64);
    glClearColor(0.10f, 0.30f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0)
        die("socketpair failed");
    if (send_ahb(buffer, sockets[0]) != 0)
        die("cannot serialize AHardwareBuffer handle");

    pixmap = xcb_generate_id(connection);
    check_xcb(connection,
              xcb_dri3_pixmap_from_buffer_checked(
                  connection, pixmap, window,
                  dri3_stride * description.height,
                  width, height, dri3_stride,
                  screen->root_depth, 32, sockets[1]),
              "DRI3PixmapFromBuffer(AHB)");
    close(sockets[0]);

    check_xcb(connection,
              xcb_present_pixmap_checked(
                  connection, window, pixmap, 1,
                  XCB_NONE, XCB_NONE, 0, 0, XCB_NONE,
                  XCB_NONE, XCB_NONE, 0, 0, 0, 0, 0, NULL),
              "PresentPixmap");
    xcb_flush(connection);
    printf("Presented %ux%u AHardwareBuffer through Xwayland DRI3; Ctrl-C to exit.\n",
           width, height);

    for (;;) {
        xcb_generic_event_t *event = xcb_wait_for_event(connection);
        if (!event)
            break;
        free(event);
    }
    return 0;
}
