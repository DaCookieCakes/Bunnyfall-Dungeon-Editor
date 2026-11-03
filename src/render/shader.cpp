#include "render/shader.h"

#include <vector>

namespace de::render {

namespace {

Result<GLuint> compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        return fail(std::string(type == GL_VERTEX_SHADER ? "vertex" : "fragment") + " shader: " + log.data());
    }
    return shader;
}

} // namespace

Result<ShaderProgram> ShaderProgram::build(const char* vertexSource, const char* fragmentSource) {
    auto vs = compile(GL_VERTEX_SHADER, vertexSource);
    if (!vs) return std::unexpected(vs.error());
    auto fs = compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fs) {
        glDeleteShader(*vs);
        return std::unexpected(fs.error());
    }
    ShaderProgram program;
    program.id_ = glCreateProgram();
    glAttachShader(program.id_, *vs);
    glAttachShader(program.id_, *fs);
    glLinkProgram(program.id_);
    glDeleteShader(*vs);
    glDeleteShader(*fs);
    GLint ok = 0;
    glGetProgramiv(program.id_, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(program.id_, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
        glGetProgramInfoLog(program.id_, length, nullptr, log.data());
        return fail(std::string("link: ") + log.data());
    }
    return program;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& o) noexcept {
    if (this != &o) {
        if (id_) glDeleteProgram(id_);
        id_ = std::exchange(o.id_, 0);
        uniforms_ = std::move(o.uniforms_);
    }
    return *this;
}

ShaderProgram::~ShaderProgram() {
    if (id_) glDeleteProgram(id_);
}

GLint ShaderProgram::uniform(const char* name) {
    const auto it = uniforms_.find(name);
    if (it != uniforms_.end()) return it->second;
    const GLint location = glGetUniformLocation(id_, name);
    uniforms_.emplace(name, location);
    return location;
}

} // namespace de::render
