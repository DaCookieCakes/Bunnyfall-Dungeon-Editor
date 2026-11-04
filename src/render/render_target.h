#pragma once

#include <epoxy/gl.h>

#include <utility>

namespace de::render {

/// An offscreen picture to draw a view into: a colour texture (shown by the UI) and a depth buffer. Move-only.
class RenderTarget {
public:
    RenderTarget() = default;
    RenderTarget(RenderTarget&& o) noexcept
        : fbo_(std::exchange(o.fbo_, 0)), colour_(std::exchange(o.colour_, 0)), depth_(std::exchange(o.depth_, 0)),
          width_(o.width_), height_(o.height_) {}
    RenderTarget& operator=(RenderTarget&& o) noexcept {
        if (this != &o) {
            release();
            fbo_ = std::exchange(o.fbo_, 0);
            colour_ = std::exchange(o.colour_, 0);
            depth_ = std::exchange(o.depth_, 0);
            width_ = o.width_;
            height_ = o.height_;
        }
        return *this;
    }
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    ~RenderTarget() { release(); }

    /// Makes the target `width` x `height` pixels (recreating it only when the size changes) and binds it.
    /// False if the driver refused it.
    bool bind(int width, int height);

    [[nodiscard]] GLuint texture() const { return colour_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    void release();
    GLuint fbo_ = 0, colour_ = 0, depth_ = 0;
    int width_ = 0, height_ = 0;
};

} // namespace de::render
