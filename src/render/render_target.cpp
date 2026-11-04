#include "render/render_target.h"

#include <algorithm>

namespace de::render {

void RenderTarget::release() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (colour_) glDeleteTextures(1, &colour_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    fbo_ = colour_ = depth_ = 0;
    width_ = height_ = 0;
}

bool RenderTarget::bind(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (fbo_ && width == width_ && height == height_) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        return true;
    }
    release();
    glGenTextures(1, &colour_);
    glBindTexture(GL_TEXTURE_2D, colour_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    width_ = width;
    height_ = height;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        release();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    return true;
}

} // namespace de::render
