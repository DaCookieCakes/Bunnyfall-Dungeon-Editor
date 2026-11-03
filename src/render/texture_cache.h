#pragma once

#include "formats/texture.h"

#include <epoxy/gl.h>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace de::render {

struct GpuTexture {
    GLuint id = 0;
    int width = 0, height = 0;   ///< Texels; model UVs are divided by these.
};

/// Daggerfall textures on the GPU, decoded on first use. Palette index 0 is transparent (cut out by the shaders),
/// as Daggerfall Unity treats it. A texture that will not load is drawn as a checkerboard and reported once.
class TextureCache {
public:
    TextureCache(std::filesystem::path arena2Dir, formats::Palette palette);
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    const GpuTexture& get(int archive, int record);

    /// A flat's drawn size in block units: the texture's size grown by its record's scale.
    std::pair<double, double> flatSize(int archive, int record);

    /// How many records an archive has (0 if it will not load).
    std::size_t recordCount(int archive);

    /// Problems met so far (missing archives, bad records), one line each.
    [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

private:
    const formats::TextureArchive* archive(int number);
    GpuTexture upload(int width, int height, const std::vector<std::uint8_t>& rgba);
    const GpuTexture& missing();

    std::filesystem::path dir_;
    formats::Palette palette_;
    std::map<int, std::optional<formats::TextureArchive>> archives_;
    std::map<std::pair<int, int>, GpuTexture> textures_;
    std::set<std::pair<int, int>> failed_;   // reported once, then drawn as the checkerboard
    std::optional<GpuTexture> missing_;
    std::vector<std::string> problems_;
};

} // namespace de::render
