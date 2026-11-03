#include "render/renderer.h"

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
out vec4 fragColour;
void main() {
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

    const float corners[] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    glGenVertexArrays(1, &r.quadVao_);
    glGenBuffers(1, &r.quadVbo_);
    glBindVertexArray(r.quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, r.quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    return r;
}

Renderer::Renderer(Renderer&& o) noexcept
    : textures_(o.textures_), models_(o.models_), modelShader_(std::move(o.modelShader_)),
      billboardShader_(std::move(o.billboardShader_)), quadVao_(std::exchange(o.quadVao_, 0)),
      quadVbo_(std::exchange(o.quadVbo_, 0)) {}

Renderer::~Renderer() {
    if (quadVao_) glDeleteVertexArrays(1, &quadVao_);
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
}

void Renderer::draw(const scene::Scene& scene, const Camera& camera, int width, int height, const DrawOptions& options,
                    std::optional<scene::Hit> selection) {
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
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        const auto& inst = scene.models[i];
        const auto* model = models_->get(inst.modelId);
        if (!model) continue;
        const bool selected = selection && selection->kind == scene::Hit::Kind::Model && selection->index == i;
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
        if (flat.editorMarker ? !options.markers : !options.flats) continue;
        const bool selected = selection && selection->kind == scene::Hit::Kind::Flat && selection->index == i;
        glUniform4fv(uFlatTint, 1, selected ? kSelectionTint : kNoTint);
        const auto& tex = textures_->get(flat.archive, flat.record);
        const auto [w, h] = textures_->flatSize(flat.archive, flat.record);
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glUniform3f(uCentre, float(flat.position.x), float(flat.position.y), float(flat.position.z));
        glUniform2f(uSize, float(w), float(h));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
}

} // namespace de::render
