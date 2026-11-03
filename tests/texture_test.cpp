#include "arena2_fixture.h"
#include "bytes_builder.h"
#include "catalog/catalog.h"
#include "core/file.h"
#include "formats/arch3d.h"
#include "formats/texture.h"

#include <doctest/doctest.h>

#include <map>
#include <set>

using namespace de::formats;
using de::ByteView;
using de::test::Bytes;

TEST_CASE("palette: .PAL and .COL layouts") {
    Bytes pal;
    for (int i = 0; i < 256; ++i) pal.u8(static_cast<std::uint8_t>(i)).u8(1).u8(2);
    auto p = Palette::fromBytes(ByteView(pal.data));
    REQUIRE(p);
    CHECK(p->rgb[200][0] == 200);

    Bytes col;
    col.zeros(8);
    col.data.insert(col.data.end(), pal.data.begin(), pal.data.end());
    auto c = Palette::fromBytes(ByteView(col.data));
    REQUIRE(c);
    CHECK(c->rgb[200][0] == 200);
    CHECK(c->rgb[200][2] == 2);

    CHECK_FALSE(Palette::fromBytes(ByteView(std::span<const std::uint8_t>(pal.data).subspan(0, 100))));
}

TEST_CASE("toRgba: index 0 is transparent only for flats") {
    Palette p;
    p.rgb[0] = {9, 9, 9};
    p.rgb[5] = {50, 60, 70};
    const IndexedImage img{2, 1, {0, 5}};
    const auto flat = toRgba(img, p, true);
    CHECK(flat[3] == 0);
    CHECK(flat[4] == 50);
    CHECK(flat[7] == 255);
    CHECK(toRgba(img, p, false)[3] == 255);
}

TEST_CASE("texture file names") {
    CHECK(TextureArchive::fileName(7) == "TEXTURE.007");
    CHECK(TextureArchive::fileName(120) == "TEXTURE.120");
}

TEST_CASE("ARENA2: every texture a dungeon model uses decodes") {
    DE_REQUIRE_ARENA2(a);
    auto palette = Palette::load(*de::findArena2File(a->dir, "ART_PAL.COL"));
    REQUIRE(palette);

    auto catalog = de::catalog::Catalog::build(*a);
    REQUIRE(catalog);
    // Textures are named by the ARCH3D faces; collect every (archive, record) the dungeon models use.
    std::map<int, std::set<int>> used;
    for (const auto& [id, info] : catalog->models()) {
        const auto index = a->arch3d.findById(id);
        REQUIRE(index);
        auto mesh = parseArch3dMesh(a->arch3d.record(*index), id);
        REQUIRE(mesh);
        for (const auto& plane : mesh->planes) used[plane.textureArchive()].insert(plane.textureRecord());
    }
    CHECK(used.size() > 20);

    int decoded = 0;
    for (const auto& [archive, records] : used) {
        INFO("archive " << archive);
        auto tex = TextureArchive::load(a->dir, archive);
        REQUIRE_MESSAGE(tex, (tex ? "" : tex.error().message));
        for (const auto record : records) {
            INFO("record " << record);
            REQUIRE(static_cast<std::size_t>(record) < tex->recordCount());
            auto img = tex->frame(static_cast<std::size_t>(record));
            REQUIRE_MESSAGE(img, (img ? "" : img.error().message));
            CHECK(img->pixels.size() == static_cast<std::size_t>(img->width * img->height));
            ++decoded;
        }
    }
    MESSAGE(decoded << " textures in " << used.size() << " archives");
}

TEST_CASE("ARENA2: the corridor wall texture decodes at its header's size and is not blank") {
    DE_REQUIRE_ARENA2(a);
    auto tex = TextureArchive::load(a->dir, 120);
    REQUIRE(tex);
    auto img = tex->frame(0);
    REQUIRE(img);
    CHECK(img->width == tex->record(0).width);
    CHECK(img->height == tex->record(0).height);
    CHECK(img->width == 64);
    std::set<std::uint8_t> colours(img->pixels.begin(), img->pixels.end());
    CHECK(colours.size() > 8);
}
