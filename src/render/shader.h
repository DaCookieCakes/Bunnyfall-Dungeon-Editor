#pragma once

#include "core/error.h"

#include <epoxy/gl.h>

#include <string>
#include <unordered_map>
#include <utility>

namespace de::render {

/// A linked GLSL program. Owns its GL object; move-only.
class ShaderProgram {
public:
    static Result<ShaderProgram> build(const char* vertexSource, const char* fragmentSource);

    ShaderProgram() = default;
    ShaderProgram(ShaderProgram&& o) noexcept : id_(std::exchange(o.id_, 0)), uniforms_(std::move(o.uniforms_)) {}
    ShaderProgram& operator=(ShaderProgram&& o) noexcept;
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ~ShaderProgram();

    void use() const { glUseProgram(id_); }
    /// Uniform location, looked up once per name.
    GLint uniform(const char* name);

private:
    GLuint id_ = 0;
    std::unordered_map<std::string, GLint> uniforms_;
};

} // namespace de::render
