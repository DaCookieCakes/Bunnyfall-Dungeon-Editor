#pragma once

// Small pictures of models for the palette: drawn on request, a few per frame, then kept.

#include "catalog/space.h"
#include "render/render_target.h"
#include "render/renderer.h"

#include <cstdint>
#include <map>
#include <vector>

namespace de::render {

class Thumbnails {
public:
    static constexpr int kSize = 96;   // pixels square

    /// The picture of a model, or 0 when it is not drawn yet (it is queued; renderPending draws it).
    GLuint get(std::uint32_t modelId, const catalog::Box& bounds);
    /// Draws up to `budget` queued pictures. Call with no UI frame in progress (after ImGui::Render); leaves the
    /// default framebuffer bound.
    void renderPending(Renderer& renderer, int budget = 6);

    Thumbnails() = default;
    Thumbnails(const Thumbnails&) = delete;
    Thumbnails& operator=(const Thumbnails&) = delete;
    ~Thumbnails();

private:
    std::map<std::uint32_t, GLuint> done_;
    std::vector<std::pair<std::uint32_t, catalog::Box>> queue_;
    RenderTarget target_;
};

} // namespace de::render
