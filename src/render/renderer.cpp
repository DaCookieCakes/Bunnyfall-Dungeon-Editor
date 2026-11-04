#include "render/renderer.h"

#include <cmath>
#include <vector>

namespace de::render {

namespace {

// Lighting is the editor's, not the game's: a soft light carried by the camera so every surface reads clearly,
// fading with distance so depth stays legible. Index 0 texels are cut out.
constexpr const char* kModelVs = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec2 uTexSize;
out vec2 vUv;
out vec3 vNormal;
out vec3 vWorld;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUv = aUv / uTexSize;
    gl_Position = uViewProj * world;
}
)";

constexpr const char* kModelFs = R"(#version 330 core
in vec2 vUv;
in vec3 vNormal;
in vec3 vWorld;
uniform sampler2D uTexture;
uniform vec3 uEye;
uniform vec4 uTint;
uniform float uWireColours;
out vec4 fragColour;
void main() {
    if (uWireColours > 0.5) {
        // 2D wireframe: one even colour per texture (its smallest mip is the average), more saturated and lifted so
        // the lines read on the dark background; no distance falloff.
        vec3 avg = textureLod(uTexture, vUv, 16.0).rgb;
        float grey = dot(avg, vec3(0.299, 0.587, 0.114));
        vec3 vivid = clamp(mix(vec3(grey), avg, 1.5), 0.0, 1.0);
        float top = max(max(vivid.r, vivid.g), max(vivid.b, 0.001));
        vivid *= max(1.0, 0.75 / top);
        fragColour = vec4(mix(vivid, uTint.rgb, uTint.a), 1.0);
        return;
    }
    vec4 texel = texture(uTexture, vUv);
    if (texel.a < 0.5) discard;
    vec3 toEye = uEye - vWorld;
    float dist = length(toEye);
    float facing = abs(dot(normalize(vNormal), toEye / max(dist, 0.001)));
    float falloff = 1.0 / (1.0 + dist / 9000.0);
    float light = 0.30 + 0.70 * mix(0.55, 1.0, facing) * falloff;
    vec3 colour = texel.rgb * light;
    fragColour = vec4(mix(colour, uTint.rgb, uTint.a), 1.0);
}
)";

constexpr const char* kBillboardVs = R"(#version 330 core
layout(location = 0) in vec2 aCorner;   // -0.5..0.5
uniform mat4 uViewProj;
uniform vec3 uCentre;
uniform vec2 uSize;
uniform vec3 uRight;
uniform vec3 uUp;
out vec2 vUv;
void main() {
    vec3 world = uCentre + uRight * (aCorner.x * uSize.x) + uUp * (aCorner.y * uSize.y);
    vUv = vec2(aCorner.x + 0.5, 0.5 - aCorner.y);
    gl_Position = uViewProj * vec4(world, 1.0);
}
)";

constexpr const char* kBillboardFs = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTexture;
uniform vec4 uTint;
out vec4 fragColour;
void main() {
    vec4 texel = texture(uTexture, vUv);
    if (texel.a < 0.5) discard;
    fragColour = vec4(mix(texel.rgb, uTint.rgb, uTint.a), 1.0);
}
)";

constexpr const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColour;
uniform mat4 uViewProj;
out vec4 vColour;
void main() {
    vColour = aColour;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)";

constexpr const char* kLineFs = R"(#version 330 core
in vec4 vColour;
out vec4 fragColour;
void main() { fragColour = vColour; }
)";

constexpr float kSelectionTint[4] = {1.0f, 0.75f, 0.1f, 0.45f};
constexpr float kNoTint[4] = {0, 0, 0, 0};

void setMat4(GLint location, const Mat4& m) { glUniformMatrix4fv(location, 1, GL_FALSE, m.data()); }

} // namespace

Result<Renderer> Renderer::create(TextureCache& textures, ModelCache& models) {
    Renderer r(textures, models);
    auto model = ShaderProgram::build(kModelVs, kModelFs);
    if (!model) return fail("model shader: " + model.error().message);
    auto billboard = ShaderProgram::build(kBillboardVs, kBillboardFs);
    if (!billboard) return fail("billboard shader: " + billboard.error().message);
    r.modelShader_ = std::move(*model);
    r.billboardShader_ = std::move(*billboard);
    auto line = ShaderProgram::build(kLineVs, kLineFs);
    if (!line) return fail("line shader: " + line.error().message);
    r.lineShader_ = std::move(*line);
    glGenVertexArrays(1, &r.lineVao_);
    glGenBuffers(1, &r.lineVbo_);
    glBindVertexArray(r.lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, r.lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    const float corners[] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    glGenVertexArrays(1, &r.quadVao_);
    glGenBuffers(1, &r.quadVbo_);
    glBindVertexArray(r.quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, r.quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // The light gizmo: a soft yellow disc with a dark rim, made here rather than taken from the game's art.
    constexpr int n = 32;
    std::vector<std::uint8_t> rgba(n * n * 4, 0);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const double dx = (x + 0.5) / n * 2 - 1, dy = (y + 0.5) / n * 2 - 1, d = std::sqrt(dx * dx + dy * dy);
            if (d > 1) continue;
            const auto i = static_cast<std::size_t>((y * n + x) * 4);
            const bool rim = d > 0.82;
            rgba[i] = rim ? 60 : 255;
            rgba[i + 1] = rim ? 45 : static_cast<std::uint8_t>(230 - 60 * d);
            rgba[i + 2] = rim ? 10 : static_cast<std::uint8_t>(120 - 100 * d);
            rgba[i + 3] = 255;
        }
    glGenTextures(1, &r.lightIcon_);
    glBindTexture(GL_TEXTURE_2D, r.lightIcon_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return r;
}

Renderer::Renderer(Renderer&& o) noexcept
    : textures_(o.textures_), models_(o.models_), modelShader_(std::move(o.modelShader_)),
      billboardShader_(std::move(o.billboardShader_)), quadVao_(std::exchange(o.quadVao_, 0)),
      quadVbo_(std::exchange(o.quadVbo_, 0)), lightIcon_(std::exchange(o.lightIcon_, 0)),
      lineShader_(std::move(o.lineShader_)), lineVao_(std::exchange(o.lineVao_, 0)), lineVbo_(std::exchange(o.lineVbo_, 0)) {}

Renderer::~Renderer() {
    if (quadVao_) glDeleteVertexArrays(1, &quadVao_);
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
    if (lightIcon_) glDeleteTextures(1, &lightIcon_);
    if (lineVao_) glDeleteVertexArrays(1, &lineVao_);
    if (lineVbo_) glDeleteBuffers(1, &lineVbo_);
}

void Renderer::drawLines(const std::vector<scene::GridLine>& lines, const Mat4& viewProj, bool onTop) {
    if (lines.empty()) return;
    std::vector<float> data;
    data.reserve(lines.size() * 14);
    for (const auto& l : lines) {
        for (const auto& p : {l.a, l.b}) {
            data.insert(data.end(), {float(p.x), float(p.y), float(p.z), l.colour[0], l.colour[1], l.colour[2], l.colour[3]});
        }
    }
    lineShader_.use();
    glUniformMatrix4fv(lineShader_.uniform("uViewProj"), 1, GL_FALSE, viewProj.data());
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (onTop) glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size() * 2));
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void Renderer::draw(const scene::Scene& scene, const Camera& camera, int width, int height, const DrawOptions& options,
                    const InstanceFlags& flags) {
    glViewport(0, 0, width, height);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // Editor space is left-handed like Unity, so Daggerfall Unity's front faces are clockwise on screen here too.
    glFrontFace(GL_CW);
    if (options.cullBackFaces) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, options.wireframe ? GL_LINE : GL_FILL);
    glActiveTexture(GL_TEXTURE0);

    const Mat4 viewProj = multiply(camera.projection(double(width) / std::max(height, 1)), camera.view());

    modelShader_.use();
    setMat4(modelShader_.uniform("uViewProj"), viewProj);
    glUniform1i(modelShader_.uniform("uTexture"), 0);
    glUniform3f(modelShader_.uniform("uEye"), float(camera.position.x), float(camera.position.y),
                float(camera.position.z));
    const GLint uModel = modelShader_.uniform("uModel");
    const GLint uTexSize = modelShader_.uniform("uTexSize");
    const GLint uTint = modelShader_.uniform("uTint");
    glUniform1f(modelShader_.uniform("uWireColours"), options.wireframe && options.wireColours ? 1.0f : 0.0f);
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        const auto& inst = scene.models[i];
        const auto* model = models_->get(inst.modelId);
        const auto f = InstanceFlags::at(flags.models, i);
        if (!model || (f & InstanceFlags::kHidden)) continue;
        const bool selected = (f & InstanceFlags::kSelected) != 0;
        glUniform4fv(uTint, 1, selected ? kSelectionTint : kNoTint);
        setMat4(uModel, modelMatrix(inst.transform.rotation, inst.transform.translation));
        glBindVertexArray(model->vao);
        for (const auto& sub : model->mesh.subMeshes) {
            // UVs are sized by the texture the model names; the dungeon's swap only changes the picture drawn
            // (Daggerfall Unity builds the mesh first and swaps materials after).
            const auto& own = textures_->get(sub.textureArchive, sub.textureRecord);
            const auto& drawn = textures_->get(scene.textures.apply(sub.textureArchive), sub.textureRecord);
            glBindTexture(GL_TEXTURE_2D, drawn.id);
            glUniform2f(uTexSize, float(own.width), float(own.height));
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(std::size_t{sub.firstIndex} * sizeof(std::uint32_t)));
        }
    }

    // Billboards face the camera across its whole view (Daggerfall turns flats about Y only; the editor shows them
    // square on so they stay readable from above).
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    billboardShader_.use();
    setMat4(billboardShader_.uniform("uViewProj"), viewProj);
    glUniform1i(billboardShader_.uniform("uTexture"), 0);
    const auto right = camera.right(), up = camera.up();
    glUniform3f(billboardShader_.uniform("uRight"), float(right.x), float(right.y), float(right.z));
    glUniform3f(billboardShader_.uniform("uUp"), float(up.x), float(up.y), float(up.z));
    const GLint uCentre = billboardShader_.uniform("uCentre");
    const GLint uSize = billboardShader_.uniform("uSize");
    const GLint uFlatTint = billboardShader_.uniform("uTint");
    glBindVertexArray(quadVao_);
    for (std::size_t i = 0; i < scene.flats.size(); ++i) {
        const auto& flat = scene.flats[i];
        const auto f = InstanceFlags::at(flags.flats, i);
        if ((flat.editorMarker ? !options.markers : !options.flats) || (f & InstanceFlags::kHidden)) continue;
        const bool selected = (f & InstanceFlags::kSelected) != 0;
        glUniform4fv(uFlatTint, 1, selected ? kSelectionTint : kNoTint);
        const auto& tex = textures_->get(flat.archive, flat.record);
        const auto [w, h] = textures_->flatSize(flat.archive, flat.record);
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glUniform3f(uCentre, float(flat.position.x), float(flat.position.y), float(flat.position.z));
        glUniform2f(uSize, float(w), float(h));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    if (options.lights) {
        glBindTexture(GL_TEXTURE_2D, lightIcon_);
        glUniform2f(uSize, float(kLightGizmoSize), float(kLightGizmoSize));
        for (std::size_t i = 0; i < scene.lights.size(); ++i) {
            const auto& light = scene.lights[i];
            const auto f = InstanceFlags::at(flags.lights, i);
            if (f & InstanceFlags::kHidden) continue;
            const bool selected = (f & InstanceFlags::kSelected) != 0;
            glUniform4fv(uFlatTint, 1, selected ? kSelectionTint : kNoTint);
            glUniform3f(uCentre, float(light.position.x), float(light.position.y), float(light.position.z));
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    glBindVertexArray(0);
    if (options.grid) drawLines(scene::gridLines(scene, options.gridOptions), viewProj, options.gridOnTop);
}

} // namespace de::render
