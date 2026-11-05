#include "render/texture_cache.h"

namespace de::render {

namespace {
constexpr int kScaleDivisor = 256;
} // namespace

TextureCache::TextureCache(std::filesystem::path arena2Dir, formats::Palette palette)
    : dir_(std::move(arena2Dir)), palette_(palette) {}

TextureCache::~TextureCache() {
    for (const auto& [_, t] : textures_) glDeleteTextures(1, &t.id);
    if (missing_) glDeleteTextures(1, &missing_->id);
}

const formats::TextureArchive* TextureCache::archive(int number) {
    auto it = archives_.find(number);
    if (it == archives_.end()) {
        auto loaded = formats::TextureArchive::load(dir_, number);
        if (!loaded) problems_.push_back(loaded.error().message);
        it = archives_.emplace(number, loaded ? std::optional(std::move(*loaded)) : std::nullopt).first;
    }
    return it->second ? &*it->second : nullptr;
}

GpuTexture TextureCache::upload(int width, int height, const std::vector<std::uint8_t>& rgba) {
    GpuTexture t{0, width, height};
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Rows go up top-first, so texture row 0 is the image's top: v (texels down from the top) / height samples it.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

const GpuTexture& TextureCache::missing() {
    if (!missing_) {
        constexpr int n = 16;
        std::vector<std::uint8_t> rgba(n * n * 4);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const bool odd = ((x / 4) + (y / 4)) % 2;
                const auto i = static_cast<std::size_t>((y * n + x) * 4);
                rgba[i] = odd ? 255 : 40;
                rgba[i + 1] = 0;
                rgba[i + 2] = odd ? 255 : 40;
                rgba[i + 3] = 255;
            }
        missing_ = upload(n, n, rgba);
    }
    return *missing_;
}

const GpuTexture& TextureCache::get(int archiveNumber, int record) {
    const std::pair key{archiveNumber, record};
    if (const auto it = textures_.find(key); it != textures_.end()) return it->second;
    const auto* a = archive(archiveNumber);
    if (!a) return missing();
    if (failed_.contains(key)) return missing();
    auto img = a->frame(static_cast<std::size_t>(record));
    if (!img) {
        problems_.push_back(formats::TextureArchive::fileName(archiveNumber) + ": " + img.error().message);
        failed_.insert(key);
        return missing();
    }
    return textures_.emplace(key, upload(img->width, img->height, formats::toRgba(*img, palette_, true))).first->second;
}

std::size_t TextureCache::recordCount(int archiveNumber) {
    const auto* a = archive(archiveNumber);
    return a ? a->recordCount() : 0;
}

std::string TextureCache::description(int archiveNumber) {
    const auto* a = archive(archiveNumber);
    return a ? a->description() : std::string();
}

std::pair<double, double> TextureCache::flatSize(int archiveNumber, int record) {
    const auto* a = archive(archiveNumber);
    if (!a || record < 0 || static_cast<std::size_t>(record) >= a->recordCount()) return {32, 32};
    const auto& r = a->record(static_cast<std::size_t>(record));
    // Daggerfall Unity's scaled billboard size: each side grows by trunc(side * scale / 256).
    const int w = r.width + r.width * r.scaleX / kScaleDivisor;
    const int h = r.height + r.height * r.scaleY / kScaleDivisor;
    return {double(w), double(h)};
}

} // namespace de::render
