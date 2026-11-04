#include "render/thumbnails.h"

#include <algorithm>
#include <cmath>

namespace de::render {

Thumbnails::~Thumbnails() {
    for (auto& [id, tex] : done_) {
        if (tex) glDeleteTextures(1, &tex);
    }
}

GLuint Thumbnails::get(std::uint32_t modelId, const catalog::Box& bounds) {
    if (const auto it = done_.find(modelId); it != done_.end()) return it->second;
    if (std::ranges::find_if(queue_, [&](const auto& q) { return q.first == modelId; }) == queue_.end()) {
        queue_.emplace_back(modelId, bounds);
    }
    return 0;
}

void Thumbnails::renderPending(Renderer& renderer, int budget) {
    if (queue_.empty()) return;
    // Newest requests first: what the palette shows now, not what scrolled past.
    while (budget-- > 0 && !queue_.empty()) {
        const auto [id, box] = queue_.back();
        queue_.pop_back();
        if (!target_.bind(kSize, kSize)) break;

        scene::Scene one;
        one.models.push_back({id, {}, {}});
        // A three-quarter view from the south-west and above, far enough to fit the whole box.
        const catalog::Vec3 c{(box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2, (box.min.z + box.max.z) / 2};
        const auto s = box.size();
        const double radius = std::max(8.0, std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z) / 2);
        Camera cam;
        cam.fovY = 40.0 * 3.14159265358979 / 180.0;
        const double distance = radius / std::sin(cam.fovY / 2) * 1.05;
        const double yaw = 0.75, pitch = -0.45;
        const catalog::Vec3 back{-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
        cam.position = {c.x + back.x * distance, c.y + back.y * distance, c.z + back.z * distance};
        cam.lookAt(c);
        cam.nearPlane = std::max(1.0, distance - radius * 1.5);
        cam.farPlane = distance + radius * 1.5;
        DrawOptions options;
        options.cullBackFaces = false;   // pieces are often open on the side facing the camera
        renderer.draw(one, cam, kSize, kSize, options, {});

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, kSize, kSize, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        done_[id] = tex;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace de::render
