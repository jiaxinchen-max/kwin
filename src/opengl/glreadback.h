/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "opengl/eglcontext.h"

#include <QImage>
#include <cstring>
#include <limits>

namespace KWin
{

// Read the bound framebuffer into a caller-owned BGRA QImage without requiring
// GL_EXT_read_format_bgra. Never replace the caller's external storage or padding.
inline bool readRgbaPixelsToBgraImage(EglContext *context, QImage *target)
{
    if (!context || !target || target->isNull() || !context->supportsRgbaReadbackFallback()) {
        return false;
    }
    const auto format = target->format();
    if (format != QImage::Format_ARGB32 && format != QImage::Format_ARGB32_Premultiplied && format != QImage::Format_RGB32) {
        return false;
    }
    const qint64 bytes = qint64(target->width()) * target->height() * 4;
    if (bytes > std::numeric_limits<GLsizei>::max()) {
        return false;
    }
    QImage rgba(target->size(), format == QImage::Format_ARGB32_Premultiplied
                    ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGBA8888);
    if (rgba.isNull()) {
        return false;
    }

    GLint alignment, rowLength, skipRows, skipPixels, packBuffer;
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    context->glReadnPixels(0, 0, rgba.width(), rgba.height(), GL_RGBA, GL_UNSIGNED_BYTE, GLsizei(bytes), rgba.bits());
    const GLenum error = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
    if (error != GL_NO_ERROR) {
        return false;
    }

    // Preserve premultiplied alpha semantics and opaque RGB32 alpha. Qt also
    // performs the channel conversion correctly on either CPU byte order.
    const QImage converted = rgba.convertToFormat(format);
    if (converted.isNull()) {
        return false;
    }
    for (int y = 0; y < target->height(); ++y) {
        std::memcpy(target->scanLine(y), converted.constScanLine(y), size_t(target->width()) * 4);
    }
    return true;
}

}
