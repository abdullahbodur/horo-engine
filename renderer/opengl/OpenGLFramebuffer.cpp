#include "renderer/opengl/OpenGLFramebuffer.h"

#include <glad/glad.h>

#include <cassert>

#include "core/Logger.h"

namespace Horo {

namespace {

bool IsDepthFormat(FramebufferTextureFormat format) {
    return format == FramebufferTextureFormat::DEPTH24STENCIL8;
}

GLenum ColorAttachmentInternalFormat(FramebufferTextureFormat format) {
    switch (format) {
        case FramebufferTextureFormat::RGBA8:        return GL_RGBA8;
        case FramebufferTextureFormat::RED_INTEGER:  return GL_R32I;
        default:                                     return GL_RGBA8;
    }
}

GLenum ColorAttachmentDataFormat(FramebufferTextureFormat format) {
    switch (format) {
        case FramebufferTextureFormat::RGBA8:        return GL_RGBA;
        case FramebufferTextureFormat::RED_INTEGER:  return GL_RED_INTEGER;
        default:                                     return GL_RGBA;
    }
}

} // namespace

OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpec& spec)
    : m_spec(spec) {
    // Separate color and depth attachment specs
    for (const auto& att : spec.attachmentSpec.attachments) {
        if (IsDepthFormat(att.format))
            m_depthAttachmentSpec = att;
        else
            m_colorAttachmentSpecs.push_back(att);
    }
    Create();
}

OpenGLFramebuffer::~OpenGLFramebuffer() {
    Release();
}

OpenGLFramebuffer::OpenGLFramebuffer(OpenGLFramebuffer&& o) noexcept
    : m_spec(o.m_spec),
      m_framebufferId(o.m_framebufferId),
      m_colorAttachments(std::move(o.m_colorAttachments)),
      m_depthAttachment(o.m_depthAttachment),
      m_colorAttachmentSpecs(std::move(o.m_colorAttachmentSpecs)),
      m_depthAttachmentSpec(o.m_depthAttachmentSpec) {
    o.m_framebufferId  = 0;
    o.m_depthAttachment = 0;
}

OpenGLFramebuffer& OpenGLFramebuffer::operator=(OpenGLFramebuffer&& o) noexcept {
    if (this != &o) {
        Release();
        m_spec                = o.m_spec;
        m_framebufferId       = o.m_framebufferId;
        m_colorAttachments    = std::move(o.m_colorAttachments);
        m_depthAttachment     = o.m_depthAttachment;
        m_colorAttachmentSpecs = std::move(o.m_colorAttachmentSpecs);
        m_depthAttachmentSpec = o.m_depthAttachmentSpec;
        o.m_framebufferId  = 0;
        o.m_depthAttachment = 0;
    }
    return *this;
}

void OpenGLFramebuffer::Create() {
    if (!glGenFramebuffers)
        return;

    glGenFramebuffers(1, &m_framebufferId);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebufferId);

    // --- Color attachments ---
    m_colorAttachments.resize(m_colorAttachmentSpecs.size(), 0);

    for (size_t i = 0; i < m_colorAttachmentSpecs.size(); ++i) {
        const auto fmt = m_colorAttachmentSpecs[i].format;
        glGenTextures(1, &m_colorAttachments[i]);
        glBindTexture(GL_TEXTURE_2D, m_colorAttachments[i]);

        const GLenum internalFmt = ColorAttachmentInternalFormat(fmt);
        const GLenum dataFmt     = ColorAttachmentDataFormat(fmt);

        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFmt),
                     static_cast<GLsizei>(m_spec.width),
                     static_cast<GLsizei>(m_spec.height),
                     0, dataFmt, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                               GL_TEXTURE_2D, m_colorAttachments[i], 0);
    }

    // Configure which color buffers are drawn to
    if (!m_colorAttachments.empty()) {
        std::vector<GLenum> drawBuffers(m_colorAttachments.size());
        for (size_t i = 0; i < drawBuffers.size(); ++i)
            drawBuffers[i] = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()),
                      drawBuffers.data());
    } else {
        glDrawBuffer(GL_NONE);
    }

    // --- Depth/stencil attachment ---
    if (m_depthAttachmentSpec.format ==
        FramebufferTextureFormat::DEPTH24STENCIL8) {
        glGenTextures(1, &m_depthAttachment);
        glBindTexture(GL_TEXTURE_2D, m_depthAttachment);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                     static_cast<GLsizei>(m_spec.width),
                     static_cast<GLsizei>(m_spec.height),
                     0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, m_depthAttachment, 0);
    }

    if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        status != GL_FRAMEBUFFER_COMPLETE)
        LogError("OpenGLFramebuffer: FBO incomplete, status=0x{:x}",
                 static_cast<unsigned>(status));

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFramebuffer::Release() {
    if (!m_colorAttachments.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_colorAttachments.size()),
                         m_colorAttachments.data());
        m_colorAttachments.clear();
    }
    if (m_depthAttachment) {
        glDeleteTextures(1, &m_depthAttachment);
        m_depthAttachment = 0;
    }
    if (m_framebufferId) {
        glDeleteFramebuffers(1, &m_framebufferId);
        m_framebufferId = 0;
    }
}

void OpenGLFramebuffer::Bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebufferId);
    glViewport(0, 0, static_cast<GLsizei>(m_spec.width),
               static_cast<GLsizei>(m_spec.height));
    if (!m_colorAttachments.empty())
        glReadBuffer(GL_COLOR_ATTACHMENT0);
}

void OpenGLFramebuffer::Unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return;
    m_spec.width  = width;
    m_spec.height = height;
    Release();
    Create();
}

uint32_t OpenGLFramebuffer::GetColorAttachmentId(uint32_t index) const {
    if (index >= static_cast<uint32_t>(m_colorAttachments.size()))
        return 0;
    return m_colorAttachments[index];
}

int OpenGLFramebuffer::ReadPixel(uint32_t attachmentIndex, int x, int y) {
    assert(attachmentIndex < m_colorAttachments.size());
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
    int pixelData = -1;
    glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
    return pixelData;
}

void OpenGLFramebuffer::ClearAttachment(uint32_t attachmentIndex, int value) {
    assert(attachmentIndex < m_colorAttachmentSpecs.size());
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebufferId);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
    glDrawBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
    glClearBufferiv(GL_COLOR, static_cast<GLint>(attachmentIndex), &value);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace Horo
