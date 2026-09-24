/*
    SPDX-FileCopyrightText: 2026 Termux Community
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "androidhardwarebuffer.h"

#include "../compositor.h"
#include "display.h"
#include "opengl/eglbackend.h"
#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"

#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <epoxy/gl.h>
#include <unistd.h>

namespace KWin
{

static constexpr uint32_t s_version = 2;
static constexpr uint32_t s_bgra8888Format = 5;

struct AndroidHardwareBufferApi
{
    using Receive = int (*)(int, AHardwareBuffer **);
    using Describe = void (*)(const AHardwareBuffer *, AHardwareBuffer_Desc *);
    using Release = void (*)(AHardwareBuffer *);

    void *library = nullptr;
    Receive receive = nullptr;
    Describe describe = nullptr;
    Release release = nullptr;
};

static const AndroidHardwareBufferApi &hardwareBufferApi()
{
    static const AndroidHardwareBufferApi api = []() {
        AndroidHardwareBufferApi result;
        result.library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (result.library) {
            result.receive = reinterpret_cast<AndroidHardwareBufferApi::Receive>(dlsym(result.library, "AHardwareBuffer_recvHandleFromUnixSocket"));
            result.describe = reinterpret_cast<AndroidHardwareBufferApi::Describe>(dlsym(result.library, "AHardwareBuffer_describe"));
            result.release = reinterpret_cast<AndroidHardwareBufferApi::Release>(dlsym(result.library, "AHardwareBuffer_release"));
        }
        return result;
    }();
    return api;
}

static GLuint compileProbeShader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;
    if (!shader) {
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static uint32_t probeChannelOrder(AHardwareBuffer *buffer)
{
    static const char vertexSource[] =
        "attribute vec2 position;\n"
        "varying vec2 texcoord;\n"
        "void main() {\n"
        "  texcoord = position * 0.5 + 0.5;\n"
        "  gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    static const char fragmentSource[] =
        "precision mediump float;\n"
        "uniform sampler2D source;\n"
        "varying vec2 texcoord;\n"
        "void main() { gl_FragColor = texture2D(source, texcoord); }\n";
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
        3.0f, -1.0f,
        -1.0f, 3.0f,
    };
    using GetNativeClientBufferProc = EGLClientBuffer(EGLAPIENTRYP)(const AHardwareBuffer *buffer);

    auto compositor = Compositor::self();
    auto backend = compositor ? qobject_cast<EglBackend *>(compositor->backend()) : nullptr;
    if (!backend || !backend->openglContext()->makeCurrent()) {
        return QtWaylandServer::termux_ahardware_buffer_manager_v1::channel_order_unknown;
    }

    const auto getNativeClientBuffer = reinterpret_cast<GetNativeClientBufferProc>(eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    if (!getNativeClientBuffer) {
        return QtWaylandServer::termux_ahardware_buffer_manager_v1::channel_order_unknown;
    }
    const EGLClientBuffer clientBuffer = getNativeClientBuffer(buffer);
    const EGLint imageAttributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    EGLImageKHR image = backend->eglDisplayObject()->createImage(EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, clientBuffer, imageAttributes);
    if (image == EGL_NO_IMAGE_KHR || !backend->openglContext()->makeCurrent()) {
        return QtWaylandServer::termux_ahardware_buffer_manager_v1::channel_order_unknown;
    }

    GLint previousFramebuffer = 0;
    GLint previousProgram = 0;
    GLint previousActiveTexture = 0;
    GLint previousTexture = 0;
    GLint previousArrayBuffer = 0;
    GLint previousPackAlignment = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    GLuint sourceTexture = 0;
    GLuint destinationTexture = 0;
    GLuint framebuffer = 0;
    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    GLuint program = 0;
    GLuint vertexBuffer = 0;
    GLuint vertexArray = 0;
    GLint linked = GL_FALSE;
    GLubyte pixel[4] = {0, 0, 0, 0};
    uint32_t result = QtWaylandServer::termux_ahardware_buffer_manager_v1::channel_order_unknown;

    glGenTextures(1, &sourceTexture);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, static_cast<GLeglImageOES>(image));

    glGenTextures(1, &destinationTexture);
    glBindTexture(GL_TEXTURE_2D, destinationTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, destinationTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        goto out;
    }

    vertexShader = compileProbeShader(GL_VERTEX_SHADER, vertexSource);
    fragmentShader = compileProbeShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertexShader || !fragmentShader) {
        goto out;
    }
    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glBindAttribLocation(program, 0, "position");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        goto out;
    }

    if (backend->openglContext()->hasVersion(Version(3, 0))) {
        glGenVertexArrays(1, &vertexArray);
        glBindVertexArray(vertexArray);
    }
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "source"), 0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glViewport(0, 0, 1, 1);
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glFinish();

    if (pixel[0] > 192 && pixel[2] < 64) {
        result = QtWaylandServer::termux_ahardware_buffer_manager_v1::channel_order_identity;
    } else if (pixel[2] > 192 && pixel[0] < 64) {
        result = QtWaylandServer::termux_ahardware_buffer_manager_v1::channel_order_swap_red_blue;
    }

out:
    if (vertexArray) {
        glBindVertexArray(0);
        glDeleteVertexArrays(1, &vertexArray);
    }
    if (vertexBuffer) {
        glDeleteBuffers(1, &vertexBuffer);
    }
    if (program) {
        glDeleteProgram(program);
    }
    if (vertexShader) {
        glDeleteShader(vertexShader);
    }
    if (fragmentShader) {
        glDeleteShader(fragmentShader);
    }
    if (framebuffer) {
        glDeleteFramebuffers(1, &framebuffer);
    }
    if (sourceTexture) {
        glDeleteTextures(1, &sourceTexture);
    }
    if (destinationTexture) {
        glDeleteTextures(1, &destinationTexture);
    }
    backend->eglDisplayObject()->destroyImage(image);

    glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
    glUseProgram(previousProgram);
    glBindBuffer(GL_ARRAY_BUFFER, previousArrayBuffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, previousTexture);
    glActiveTexture(previousActiveTexture);
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    return result;
}

AndroidHardwareBufferManagerV1::AndroidHardwareBufferManagerV1(Display *display, QObject *parent)
    : QObject(parent)
    , QtWaylandServer::termux_ahardware_buffer_manager_v1(*display, s_version)
{
}

void AndroidHardwareBufferManagerV1::termux_ahardware_buffer_manager_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void AndroidHardwareBufferManagerV1::termux_ahardware_buffer_manager_v1_create_buffer(Resource *resource, uint32_t id, int32_t socketFd, uint32_t flags)
{
    const auto &api = hardwareBufferApi();
    AHardwareBuffer *buffer = nullptr;
    const bool apiAvailable = api.receive && api.describe && api.release;
    const int result = apiAvailable ? api.receive(socketFd, &buffer) : -1;
    close(socketFd);
    if (result != 0 || !buffer) {
        wl_resource_post_error(resource->handle, error_invalid_handle, "failed to receive AHardwareBuffer");
        return;
    }

    AHardwareBuffer_Desc description = {};
    api.describe(buffer, &description);
    const bool validSize = description.width > 0 && description.height > 0 && description.layers == 1;
    const bool sampleable = description.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if (!validSize || !sampleable) {
        api.release(buffer);
        wl_resource_post_error(resource->handle, error_invalid_buffer,
                               "invalid AHardwareBuffer dimensions, layers, or usage");
        return;
    }

    new AndroidHardwareClientBuffer(buffer, resource->client(), id, flags);
}

void AndroidHardwareBufferManagerV1::termux_ahardware_buffer_manager_v1_probe_buffer(Resource *resource, int32_t socketFd, uint32_t token)
{
    const auto &api = hardwareBufferApi();
    AHardwareBuffer *buffer = nullptr;
    const int receiveResult = api.receive ? api.receive(socketFd, &buffer) : -1;
    close(socketFd);

    uint32_t result = channel_order_unknown;
    if (receiveResult == 0 && buffer) {
        result = probeChannelOrder(buffer);
        api.release(buffer);
    }
    send_probe_result(resource->handle, token, result);
}

const struct wl_buffer_interface AndroidHardwareClientBuffer::implementation = {
    .destroy = buffer_destroy,
};

AndroidHardwareClientBuffer::AndroidHardwareClientBuffer(AHardwareBuffer *buffer, wl_client *client, uint32_t id, uint32_t flags)
    : m_buffer(buffer)
    , m_resource(wl_resource_create(client, &wl_buffer_interface, 1, id))
{
    AHardwareBuffer_Desc description = {};
    hardwareBufferApi().describe(buffer, &description);
    m_attributes = {
        .buffer = buffer,
        .format = description.format,
        .stride = description.stride,
        .usage = description.usage,
        .flags = flags,
    };
    m_size = QSize(description.width, description.height);

    if (!m_resource) {
        hardwareBufferApi().release(m_buffer);
        m_buffer = nullptr;
        deleteLater();
        return;
    }

    connect(this, &GraphicsBuffer::released, [this]() {
        if (m_resource) {
            wl_buffer_send_release(m_resource);
        }
    });
    wl_resource_set_implementation(m_resource, &implementation, this, buffer_destroy_resource);
}

AndroidHardwareClientBuffer::~AndroidHardwareClientBuffer()
{
    if (m_buffer) {
        hardwareBufferApi().release(m_buffer);
    }
}

QSize AndroidHardwareClientBuffer::size() const
{
    return m_size;
}

bool AndroidHardwareClientBuffer::hasAlphaChannel() const
{
    if (m_attributes.flags & QtWaylandServer::termux_ahardware_buffer_manager_v1::buffer_flags_opaque) {
        return false;
    }
    switch (m_attributes.format) {
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:
    case s_bgra8888Format:
        return true;
    default:
        return false;
    }
}

const AndroidHardwareBufferAttributes *AndroidHardwareClientBuffer::androidHardwareBufferAttributes() const
{
    return &m_attributes;
}

void AndroidHardwareClientBuffer::buffer_destroy_resource(wl_resource *resource)
{
    auto buffer = get(resource);
    buffer->m_resource = nullptr;
    buffer->drop();
}

void AndroidHardwareClientBuffer::buffer_destroy(wl_client *client, wl_resource *resource)
{
    wl_resource_destroy(resource);
}

AndroidHardwareClientBuffer *AndroidHardwareClientBuffer::get(wl_resource *resource)
{
    if (wl_resource_instance_of(resource, &wl_buffer_interface, &implementation)) {
        return static_cast<AndroidHardwareClientBuffer *>(wl_resource_get_user_data(resource));
    }
    return nullptr;
}

}
