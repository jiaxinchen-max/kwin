/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"
#include "opengl/glreadback.h"
#include "opengl/glshadermanager.h"
#include "opengl/glshader.h"
#include <QCoreApplication>
#include <QDebug>
#include <array>
#include <cstdlib>

using namespace KWin;
static int checks = 0;
static void require(bool ok, const char *name)
{
    if (!ok) {
        qCritical() << "FAIL:" << name;
        std::exit(1);
    }
    ++checks;
    qInfo() << "PASS:" << name;
}
static bool near(int actual, int expected) { return std::abs(actual - expected) <= 1; }
int main(int argc, char **argv)
{
    qputenv("KWIN_ANDROID_GL_MODE", "system");
    qputenv("KWIN_COMPOSE", "O2ES");
    qputenv("TERMUX_ANDROID_ZINK", "0");
    QCoreApplication app(argc, argv);
    auto display = EglDisplay::create(eglGetDisplay(EGL_DEFAULT_DISPLAY), nullptr, false);
    require(bool(display), "system EGL display");
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    require(eglChooseConfig(display->handle(), attrs, &config, 1, &count) && count,
        "Android backend EGL config");
    auto context = EglContext::create(display.get(), config, nullptr);
    require(bool(context) && context->makeCurrent(), "real KWin context passes capability and shader checks");
    qInfo() << "Renderer:" << context->renderer() << "Version:" << context->openglVersionString();
    require(!context->supportsBgraReadback(), "emulator does not advertise native BGRA readback");
    require(context->supportsRgbaReadbackFallback(), "Android System GLES fallback selected");
    qputenv("KWIN_ANDROID_GL_MODE", "zink");
    require(!context->supportsRgbaReadbackFallback() && !context->checkSupported(),
        "non-system renderer gate remains strict");
    qputenv("KWIN_ANDROID_GL_MODE", "system");

    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        qWarning() << "Pre-existing context initialization GL error:" << Qt::hex << error;
    }
    GLuint texture, fbo;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "RGBA8 framebuffer");
    require(glGetError() == GL_NO_ERROR, "framebuffer allocation has no GL errors");
    glClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(1, 1, 1, 1);
    glClearColor(0.8f, 0.1f, 0.2f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    require(glGetError() == GL_NO_ERROR, "framebuffer colors prepared");

    std::array<uchar, 64> storage;
    storage.fill(0xa5);
    QImage target(storage.data() + 16, 2, 2, 16, QImage::Format_ARGB32);
    uchar *const external = target.bits();
    GLuint packBuffer;
    glGenBuffers(1, &packBuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
    std::array<uchar, 128> bufferData;
    bufferData.fill(0x5a);
    glBufferData(GL_PIXEL_PACK_BUFFER, bufferData.size(), bufferData.data(), GL_STREAM_READ);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 7);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
    require(readRgbaPixelsToBgraImage(context.get(), &target), "production RGBA-to-BGRA image readback");
    require(target.bits() == external, "external image storage preserved");
    require(near(qRed(target.pixel(0, 0)), 51) && near(qGreen(target.pixel(0, 0)), 102)
        && near(qBlue(target.pixel(0, 0)), 204) && qAlpha(target.pixel(0, 0)) == 255,
        "red blue and opaque alpha correct");
    require(near(qRed(target.pixel(1, 1)), 204) && near(qGreen(target.pixel(1, 1)), 26)
        && near(qBlue(target.pixel(1, 1)), 51) && near(qAlpha(target.pixel(1, 1)), 128),
        "row orientation and straight alpha preserved");
    bool guards = true;
    for (size_t n = 0; n < storage.size(); ++n) {
        const bool pixel = (n >= 16 && n < 24) || (n >= 32 && n < 40);
        if (!pixel) { guards &= storage[n] == 0xa5; }
    }
    require(guards, "external row padding and both boundary canaries preserved");
    const std::array<GLenum, 5> states = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
        GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS, GL_PIXEL_PACK_BUFFER_BINDING};
    const std::array<GLint, 5> expected = {8, 7, 1, 2, GLint(packBuffer)};
    bool restored = true;
    for (size_t n = 0; n < states.size(); ++n) {
        GLint value; glGetIntegerv(states[n], &value); restored &= value == expected[n];
    }
    require(restored, "all pack state and PBO binding restored");
    auto mapped = static_cast<uchar *>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bufferData.size(), GL_MAP_READ_BIT));
    bool untouched = mapped;
    if (mapped) {
        untouched = std::memcmp(mapped, bufferData.data(), bufferData.size()) == 0;
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    require(untouched, "previously bound PBO contents untouched");
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    QImage opaque(2, 2, QImage::Format_RGB32);
    require(readRgbaPixelsToBgraImage(context.get(), &opaque) && qAlpha(opaque.pixel(1, 1)) == 255,
        "RGB32 target stays opaque");
    glClearColor(0.1f, 0.2f, 0.3f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    QImage premultiplied(2, 2, QImage::Format_ARGB32_Premultiplied);
    require(readRgbaPixelsToBgraImage(context.get(), &premultiplied)
        && near(qRed(premultiplied.pixel(0, 0)), 26) && near(qGreen(premultiplied.pixel(0, 0)), 51)
        && near(qBlue(premultiplied.pixel(0, 0)), 77) && near(qAlpha(premultiplied.pixel(0, 0)), 128),
        "premultiplied channels are not multiplied twice");
    QImage unsupported(2, 2, QImage::Format_Grayscale8);
    unsupported.fill(0x33);
    require(!readRgbaPixelsToBgraImage(context.get(), &unsupported)
        && unsupported.constBits()[0] == 0x33, "unsupported format rejected without writing");
    QImage empty;
    require(!readRgbaPixelsToBgraImage(context.get(), &empty)
        && !readRgbaPixelsToBgraImage(context.get(), nullptr), "empty targets rejected");
    require(glGetError() == GL_NO_ERROR, "no GL errors after conversion and state restoration");
    QImage redImage(2, 2, QImage::Format_ARGB32_Premultiplied);
    QImage blueImage(2, 2, QImage::Format_ARGB32_Premultiplied);
    redImage.fill(QColor(255, 0, 0));
    blueImage.fill(QColor(0, 0, 255));
    auto redTexture = GLTexture::upload(redImage);
    auto blueTexture = GLTexture::upload(blueImage);
    require(redTexture && blueTexture && redTexture->texture() != blueTexture->texture(), "production texture uploads stay distinct");
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 2, 2);
    glDisable(GL_BLEND);
    {
        ShaderBinder binder(ShaderTrait::MapTexture);
        QMatrix4x4 projection;
        projection.ortho(0.0f, 2.0f, 2.0f, 0.0f, -1.0f, 1.0f);
        binder.shader()->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, projection);
        binder.shader()->setUniform(GLShader::IntUniform::Sampler, 0);
        glActiveTexture(GL_TEXTURE0);
        auto checkTexture = [&](GLTexture *texture, QRgb expected) {
            GLuint sourceFbo;
            glGenFramebuffers(1, &sourceFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->texture(), 0);
            std::array<uchar, 4> sourcePixel;
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sourcePixel.data());
            qInfo() << "Source texture pixel" << texture->texture() << sourcePixel;
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glDeleteFramebuffers(1, &sourceFbo);
            glClearColor(0, 1, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            texture->setFilter(GL_NEAREST);
            texture->bind();
            texture->render(QSizeF(2, 2));
            texture->unbind();
            std::array<uchar, 4> rawPixel;
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rawPixel.data());
            qInfo() << "Raw rendered pixel" << rawPixel;
            QImage pixels(2, 2, QImage::Format_ARGB32);
            require(readRgbaPixelsToBgraImage(context.get(), &pixels), "production texture render readback");
            qInfo() << "Rendered" << texture->texture() << Qt::hex << pixels.pixel(0, 0) << "expected" << expected;
            require(pixels.pixel(0, 0) == expected, "production texture sampling does not alias");
        };
        checkTexture(redTexture.get(), qRgb(255, 0, 0));
        checkTexture(blueTexture.get(), qRgb(0, 0, 255));
        checkTexture(redTexture.get(), qRgb(255, 0, 0));
    }
    redTexture.reset();
    blueTexture.reset();
    for (const auto &name : {QStringLiteral("downsample"), QStringLiteral("upsample")}) {
        const auto shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture,
            QStringLiteral(":/effects/blur/shaders/vertex.vert"),
            QStringLiteral(":/effects/blur/shaders/") + name + QStringLiteral(".frag"));
        require(bool(shader), qPrintable(name + QStringLiteral(" blur shader compiles on real GLES")));
    }
    require(glGetError() == GL_NO_ERROR, "texture sampling and blur validation leave no GL errors");
    glDeleteBuffers(1, &packBuffer);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    qInfo() << "RESULT: PASS" << checks << "checks";
    return 0;
}
