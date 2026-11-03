// dftool: inspect the ARENA2 data the dungeon editor reads.
//
//   dftool <arena2-dir> stats              counts and grid measurements over every dungeon block and dungeon
//   dftool <arena2-dir> block <NAME>       one RDB block's model table and objects
//   dftool <arena2-dir> dungeon <name|id>  one dungeon's block grid
//   dftool <arena2-dir> model <id>         one ARCH3D model's size, planes and textures
//   dftool <arena2-dir> models [N]         the N most used models (default 30), with kind and size
//   dftool <arena2-dir> ports [NAME]       one block's edge openings, or how openings fall across all N/W blocks
//   dftool <arena2-dir> boxes <NAME>       every placed model's box in editor space (for plotting)
//   dftool <arena2-dir> export-block <NAME> <out.json>
//                                          a classic block as World Data JSON, to edit by hand. It holds Daggerfall's
//                                          own data: never ship it; turn the edit into a patch (make-patch)
//   dftool <arena2-dir> make-patch <BASE> <edited.json> <out-patch.json>
//                                          the edit as a Bunnyfall world-data patch, checked by rebuilding it
//   dftool <arena2-dir> rebuild-patch <patch.json> [out.json]
//                                          rebuild a patch from this ARENA2 and check its sha256

#include "catalog/catalog.h"
#include "export/world_data_patch.h"
#include "formats/arch3d.h"
#include "formats/arena2.h"
#include "formats/maps.h"
#include "formats/rdb.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <optional>
#include <cstdio>
#include <map>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace de;
using namespace de::formats;

namespace {

int usage() {
    std::println(stderr, "usage: dftool <arena2-dir> stats | block <NAME> | dungeon <name|locationId> | model <id> |"
                         " models [N] | ports [NAME] | boxes NAME |"
                         " export-block NAME OUT | make-patch BASE EDITED OUT | rebuild-patch PATCH [OUT]");
    return 2;
}

template <typename T>
int report(const Result<T>& r) {
    std::println(stderr, "error: {}", r.error().message);
    return 1;
}

std::vector<std::pair<std::string, RdbBlock>> loadAllRdb(const Arena2& a, int& failures) {
    std::vector<std::pair<std::string, RdbBlock>> out;
    for (std::size_t i = 0; i < a.blocks.count(); ++i) {
        const auto& name = a.blocks.entry(i).name;
        if (!name.ends_with(".RDB")) continue;
        auto block = readClassicRdbBlock(a.blocks, i);
        if (!block) {
            std::println(stderr, "{}: {}", name, block.error().message);
            ++failures;
            continue;
        }
        out.emplace_back(name, std::move(*block));
    }
    return out;
}

double share(std::size_t part, std::size_t whole) { return whole ? double(part) / double(whole) : 0.0; }

int cmdStats(const Arena2& a) {
    int failures = 0;
    const auto blocks = loadAllRdb(a, failures);
    std::map<char, int> prefixes;
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> rootDims;
    std::size_t models = 0, flats = 0, lights = 0, arch = 0, arch64 = 0, arch128 = 0, archY256 = 0, rot90 = 0;
    std::vector<std::size_t> perBlock;
    std::map<int, int> editorFlats;
    for (const auto& [name, block] : blocks) {
        ++prefixes[name.front()];
        ++rootDims[{block.width, block.height}];
        perBlock.push_back(block.objectCount());
        for (const auto& root : block.roots) {
            for (const auto& obj : root.objects) {
                if (const auto* m = obj.model()) {
                    ++models;
                    if (m->yRotation % 512 == 0) ++rot90;
                    if (!block.modelRefs[m->modelIndex].description.starts_with('C')) continue;
                    ++arch;
                    if (obj.x % 64 == 0 && obj.z % 64 == 0) ++arch64;
                    if (obj.x % 128 == 0 && obj.z % 128 == 0) ++arch128;
                    if (obj.y % 256 == 0) ++archY256;
                } else if (const auto* f = obj.flat()) {
                    ++flats;
                    if (f->textureArchive() == 199) ++editorFlats[f->textureRecord()];
                } else {
                    ++lights;
                }
            }
        }
    }
    std::ranges::sort(perBlock);

    std::println("RDB blocks: {} ({} failed to parse)", blocks.size(), failures);
    for (const auto& [p, n] : prefixes) std::println("  {} {:4}  {}", p, n, roleName(rdbRoleFromName(std::string(1, p))));
    for (const auto& [dims, n] : rootDims) std::println("  root list {}x{}: {}", dims.first, dims.second, n);
    std::println("objects: {} models, {} flats, {} lights", models, flats, lights);
    if (!perBlock.empty()) {
        std::println("objects per block: min {} median {} max {}", perBlock.front(), perBlock[perBlock.size() / 2],
                     perBlock.back());
    }
    std::println("architecture pieces (C-prefixed): {}", arch);
    std::println("  x/z on 64:  {:.3f}", share(arch64, arch));
    std::println("  x/z on 128: {:.3f}", share(arch128, arch));
    std::println("  y on 256:   {:.3f}", share(archY256, arch));
    std::println("models with 90-degree y rotation: {:.3f}", share(rot90, models));
    std::print("editor flats (199/record: count):");
    for (const auto& [rec, n] : editorFlats) std::print(" {}:{}", rec, n);
    std::println("");

    auto dungeons = readDungeons(a.maps);
    if (!dungeons) return report(dungeons);
    std::size_t startAtOrigin = 0, starts = 0, interiorSides = 0, openSides = 0;
    std::map<std::size_t, int> sizes;
    std::set<std::string> missing;
    std::set<std::string> rdbNames;
    for (const auto& [name, _] : blocks) rdbNames.insert(name);
    for (const auto& d : *dungeons) {
        ++sizes[d.blocks.size()];
        std::set<std::pair<int, int>> cells;
        for (const auto& b : d.blocks) cells.insert({b.x, b.z});
        for (const auto& b : d.blocks) {
            if (!rdbNames.contains(b.blockName)) missing.insert(b.blockName);
            if (b.isStartingBlock) {
                ++starts;
                if (b.x == 0 && b.z == 0) ++startAtOrigin;
            }
            if (rdbRoleFromName(b.blockName) == RdbRole::Border) continue;
            for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                ++interiorSides;
                if (!cells.contains({b.x + dx, b.z + dz})) ++openSides;
            }
        }
    }
    std::println("dungeons: {}", dungeons->size());
    std::println("  starting blocks at (0,0): {} of {}", startAtOrigin, starts);
    std::println("  non-border block sides with no neighbour: {} of {}", openSides, interiorSides);
    std::print("  blocks per dungeon (size:count):");
    for (const auto& [n, c] : sizes) std::print(" {}:{}", n, c);
    std::println("");
    std::println("  block names missing from BLOCKS.BSA: {}", missing.size());
    return failures == 0 ? 0 : 1;
}

int cmdBlock(const Arena2& a, std::string name) {
    if (!name.ends_with(".RDB")) name += ".RDB";
    const auto index = a.blocks.findByName(name);
    if (!index) {
        std::println(stderr, "error: no block {}", name);
        return 1;
    }
    auto block = readClassicRdbBlock(a.blocks, *index);
    if (!block) return report(block);
    std::println("{}  role {}  record {}  offset {}  roots {}x{}  objects {}", name,
                 roleName(rdbRoleFromName(name)), *index, a.blocks.entry(*index).offset, block->width, block->height,
                 block->objectCount());
    std::println("model references in use:");
    std::set<std::uint16_t> used;
    for (const auto& root : block->roots)
        for (const auto& obj : root.objects)
            if (const auto* m = obj.model()) used.insert(m->modelIndex);
    for (const auto i : used) {
        const auto& ref = block->modelRefs[i];
        std::println("  [{:3}] {:>5} {}", i, ref.modelId, ref.description);
    }
    std::println("objects (root.index @position: kind ...):");
    for (std::size_t r = 0; r < block->roots.size(); ++r) {
        for (const auto& obj : block->roots[r].objects) {
            std::print("  {:2}.{:<3} @{:<6} ({:5},{:5},{:5}) ", r, obj.index, obj.position, obj.x, obj.y, obj.z);
            if (const auto* m = obj.model()) {
                const auto& ref = block->modelRefs[m->modelIndex];
                std::print("model {} {} rot({},{},{})", ref.modelId, ref.description, m->xRotation, m->yRotation,
                           m->zRotation);
                if (m->triggerFlagStartingLock) std::print(" trig/lock {:#x}", m->triggerFlagStartingLock);
                if (m->hasAction()) {
                    std::print(" action flags {} axis {} dur {} mag {} next {}", m->action.flags, m->action.axis,
                               m->action.duration, m->action.magnitude, m->action.nextObjectOffset);
                }
            } else if (const auto* f = obj.flat()) {
                std::print("flat {}/{} flags {} mag {} snd {}", f->textureArchive(), f->textureRecord(), f->flags,
                           f->magnitude, f->soundIndex);
                if (f->action) std::print(" action {} next {}", f->action, f->nextObjectOffset);
            } else if (const auto* l = obj.light()) {
                std::print("light radius {}", l->radius);
            }
            std::println("");
        }
    }
    return 0;
}

int cmdDungeon(const Arena2& a, std::string_view key) {
    auto dungeons = readDungeons(a.maps);
    if (!dungeons) return report(dungeons);
    unsigned id = 0;
    const bool byId = std::from_chars(key.data(), key.data() + key.size(), id).ec == std::errc{};
    int shown = 0;
    for (const auto& d : *dungeons) {
        if (byId ? d.locationId != id : d.name.find(key) == std::string::npos) continue;
        if (++shown > 10) {
            std::println("... more matches; narrow the name");
            break;
        }
        std::println("{}  region {}  locationId {}  blocks {}", d.name, d.regionIndex, d.locationId, d.blocks.size());
        int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
        for (const auto& b : d.blocks) {
            minX = std::min<int>(minX, b.x); maxX = std::max<int>(maxX, b.x);
            minZ = std::min<int>(minZ, b.z); maxZ = std::max<int>(maxZ, b.z);
        }
        for (int z = maxZ; z >= minZ; --z) {   // north up
            std::print("  {:3} ", z);
            for (int x = minX; x <= maxX; ++x) {
                const auto it = std::ranges::find_if(d.blocks, [&](const auto& b) { return b.x == x && b.z == z; });
                if (it == d.blocks.end()) std::print("     .      ");
                else std::print(" {}{}", it->blockName.substr(0, 8), it->isStartingBlock ? "*  " : "   ");
            }
            std::println("");
        }
        std::print("      ");
        for (int x = minX; x <= maxX; ++x) std::print("    {:3}     ", x);
        std::println("\n  (* = starting block)\n");
    }
    if (shown == 0) {
        std::println(stderr, "error: no dungeon matches '{}'", key);
        return 1;
    }
    return 0;
}

int cmdModel(const Arena2& a, std::string_view key) {
    std::uint32_t id = 0;
    if (std::from_chars(key.data(), key.data() + key.size(), id).ec != std::errc{}) return usage();
    const auto index = a.arch3d.findById(id);
    if (!index) {
        std::println(stderr, "error: no model {}", id);
        return 1;
    }
    auto mesh = parseArch3dMesh(a.arch3d.record(*index), id);
    if (!mesh) return report(mesh);
    const auto& b = mesh->bounds;
    const auto toBlock = [](std::int64_t v) { return double(v) / kArch3dUnitsPerBlockUnit; };
    const auto toMetres = [&](std::int64_t v) { return toBlock(v) * 0.025; };
    std::println("model {}  points {}  planes {}", id, mesh->points.size(), mesh->planes.size());
    std::println("  size (block units) {:.1f} x {:.1f} x {:.1f}   ({:.2f} x {:.2f} x {:.2f} m)", toBlock(b.sizeX()),
                 toBlock(b.sizeY()), toBlock(b.sizeZ()), toMetres(b.sizeX()), toMetres(b.sizeY()),
                 toMetres(b.sizeZ()));
    std::println("  x {:.1f}..{:.1f}  y {:.1f}..{:.1f}  z {:.1f}..{:.1f} (block units)", toBlock(b.min.x),
                 toBlock(b.max.x), toBlock(b.min.y), toBlock(b.max.y), toBlock(b.min.z), toBlock(b.max.z));
    std::map<std::pair<int, int>, int> textures;
    for (const auto& p : mesh->planes) ++textures[{p.textureArchive(), p.textureRecord()}];
    std::print("  textures (archive/record: planes):");
    for (const auto& [t, n] : textures) std::print(" {}/{}:{}", t.first, t.second, n);
    std::println("");
    return 0;
}

int cmdModels(const Arena2& a, std::size_t limit) {
    auto catalog = de::catalog::Catalog::build(a);
    if (!catalog) return report(catalog);
    std::vector<const de::catalog::ModelInfo*> sorted;
    for (const auto& [_, m] : catalog->models()) sorted.push_back(&m);
    std::ranges::stable_sort(sorted, [](auto* x, auto* y) { return x->uses > y->uses; });
    std::map<de::catalog::ModelKind, int> kinds;
    for (const auto* m : sorted) ++kinds[m->kind];
    std::print("{} models used by dungeon blocks:", sorted.size());
    for (const auto& [k, n] : kinds) std::print(" {} {}", n, de::catalog::kindName(k));
    std::println("");
    std::println("   id   desc  kind          uses   size x/y/z (block units)");
    for (std::size_t i = 0; i < std::min(limit, sorted.size()); ++i) {
        const auto* m = sorted[i];
        const auto s = m->bounds.size();
        std::println("{:6}  {:4}  {:12} {:5}   {} x {} x {}", m->id, m->description, de::catalog::kindName(m->kind),
                     m->uses, s.x, s.y, s.z);
    }
    return 0;
}

int cmdPorts(const Arena2& a, std::string name) {
    auto catalog = de::catalog::Catalog::build(a);
    if (!catalog) return report(catalog);
    if (!name.empty()) {
        if (!name.ends_with(".RDB")) name += ".RDB";
        const auto* b = catalog->block(name);
        if (!b) {
            std::println(stderr, "error: no block {}", name);
            return 1;
        }
        std::println("{}  role {}  {} ports ({} outside the standard, marked +)", b->name, roleName(b->role),
                     b->ports.size(), b->otherPorts.size());
        for (const auto& p : b->ports) {
            const bool other = std::ranges::find(b->otherPorts, p) != b->otherPorts.end();
            std::println("  {} {} along {:5}  floor {:6}  width {:4}  ({})", other ? '+' : ' ',
                         de::catalog::edgeName(p.edge), p.along, p.floor, p.width, p.modelId);
        }
        return 0;
    }
    // Across all blocks: how closely each follows the standard sockets.
    std::map<RdbRole, std::pair<int, int>> byRole;   // role -> (blocks, blocks with all 8 sockets)
    std::map<std::string, int> missing;              // "edge socket" -> blocks without it
    std::size_t withOther = 0;
    for (const auto& b : catalog->blocks()) {
        auto& [count, full] = byRole[b.role];
        ++count;
        bool all = true;
        for (std::size_t e = 0; e < 4; ++e)
            for (std::size_t k = 0; k < 2; ++k)
                if (!b.sockets[e][k]) {
                    all = false;
                    ++missing[std::format("{} {}", de::catalog::edgeName(static_cast<de::catalog::Edge>(e)),
                                          de::catalog::kStandardSockets[k].along)];
                }
        if (all) ++full;
        if (!b.otherPorts.empty()) ++withOther;
    }
    std::println("standard sockets: along 640 at floor 1280, along 1408 at floor 512, on every edge");
    for (const auto& [role, counts] : byRole) {
        std::println("  {:9} {:3} blocks, {:3} open all eight", roleName(role), counts.first, counts.second);
    }
    std::print("missing sockets (edge along: blocks):");
    for (const auto& [k, n] : missing) std::print("  {}: {}", k, n);
    std::println("\nblocks with openings outside the standard: {}", withOther);
    for (const auto& b : catalog->blocks()) {
        if (b.otherPorts.empty()) continue;
        std::print("  {}:", b.name);
        for (const auto& p : b.otherPorts) {
            std::print(" {}{}@{}w{}", de::catalog::edgeName(p.edge), p.along, p.floor, p.width);
        }
        std::println("");
    }
    return 0;
}

int cmdBoxes(const Arena2& a, std::string name) {
    if (!name.ends_with(".RDB")) name += ".RDB";
    auto catalog = de::catalog::Catalog::build(a);
    if (!catalog) return report(catalog);
    const auto index = a.blocks.findByName(name);
    if (!index) return report(Result<int>(de::fail("no block " + name)));
    auto block = readClassicRdbBlock(a.blocks, *index);
    if (!block) return report(block);
    for (const auto& root : block->roots) {
        for (const auto& obj : root.objects) {
            const auto* m = obj.model();
            if (!m) continue;
            const auto id = block->modelRefs[m->modelIndex].modelIdNum;
            const auto* info = catalog->model(id);
            if (!info) continue;
            const auto box = de::catalog::transformBox(
                info->bounds, de::catalog::rdbModelRotation(m->xRotation, m->yRotation, m->zRotation),
                de::catalog::fromFilePosition(obj.x, obj.y, obj.z));
            std::println("{} {} {} {} {} {} {} {}", id, de::catalog::kindName(info->kind)[0], box.min.x, box.min.y,
                         box.min.z, box.max.x, box.max.y, box.max.z);
        }
    }
    return 0;
}

std::optional<std::size_t> blockIndex(const Arena2& a, std::string name) {
    if (!name.ends_with(".RDB")) name += ".RDB";
    return a.blocks.findByName(name);
}

int writeJson(const std::string& path, const de::exporter::Json& json) {
    std::ofstream out(path);
    out << json.dump(1) << "\n";
    if (!out) {
        std::println(stderr, "error: cannot write {}", path);
        return 1;
    }
    return 0;
}

std::optional<de::exporter::Json> readJson(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    auto json = de::exporter::Json::parse(in, nullptr, false);
    if (json.is_discarded()) return std::nullopt;
    return json;
}

int cmdExportBlock(const Arena2& a, const std::string& name, const std::string& out) {
    const auto index = blockIndex(a, name);
    if (!index) return report(Result<int>(de::fail("no block " + name)));
    auto json = de::exporter::classicBlockJson(a.blocks, *index);
    if (!json) return report(json);
    if (int r = writeJson(out, *json)) return r;
    std::println("wrote {} ({} sha256 {})", out, a.blocks.entry(*index).name, de::exporter::canonicalSha256(*json));
    std::println("this file holds Daggerfall's own data: edit it, then make a patch to ship");
    return 0;
}

int cmdMakePatch(const Arena2& a, const std::string& base, const std::string& editedPath, const std::string& out) {
    const auto index = blockIndex(a, base);
    if (!index) return report(Result<int>(de::fail("no block " + base)));
    const auto edited = readJson(editedPath);
    if (!edited) return report(Result<int>(de::fail("cannot read JSON from " + editedPath)));
    const std::string name = edited->value("Name", a.blocks.entry(*index).name);
    auto patch = de::exporter::makeBlockPatch(a.blocks, *index, *edited, name + ".json");
    if (!patch) return report(patch);
    if (int r = writeJson(out, *patch)) return r;
    std::println("wrote {}: {} ops over {}, rebuilds {} (sha256 {})", out, (*patch)["ops"].size(),
                 a.blocks.entry(*index).name, (*patch)["rebuilds"].get<std::string>(),
                 (*patch)["sha256"].get<std::string>());
    return 0;
}

int cmdRebuildPatch(const Arena2& a, const std::string& patchPath, const std::string& out) {
    const auto patch = readJson(patchPath);
    if (!patch) return report(Result<int>(de::fail("cannot read JSON from " + patchPath)));
    auto json = de::exporter::rebuildBlockPatch(*patch, a.blocks);
    if (!json) return report(json);
    const auto sha = de::exporter::canonicalSha256(*json);
    const bool same = sha == patch->value("sha256", "");
    std::println("{}: {} (sha256 {})", patch->value("rebuilds", "?"), same ? "rebuilds exactly" : "DIFFERS from the author's", sha);
    if (!out.empty()) {
        if (int r = writeJson(out, *json)) return r;
    }
    return same ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    auto arena2 = Arena2::open(argv[1]);
    if (!arena2) return report(arena2);
    const std::string_view cmd = argv[2];
    if (cmd == "stats") return cmdStats(*arena2);
    if (cmd == "models") return cmdModels(*arena2, argc > 3 ? std::stoul(argv[3]) : 30);
    if (cmd == "ports") return cmdPorts(*arena2, argc > 3 ? argv[3] : "");
    if (argc < 4) return usage();
    if (cmd == "block") return cmdBlock(*arena2, argv[3]);
    if (cmd == "dungeon") return cmdDungeon(*arena2, argv[3]);
    if (cmd == "model") return cmdModel(*arena2, argv[3]);
    if (cmd == "boxes") return cmdBoxes(*arena2, argv[3]);
    if (cmd == "export-block" && argc > 4) return cmdExportBlock(*arena2, argv[3], argv[4]);
    if (cmd == "make-patch" && argc > 5) return cmdMakePatch(*arena2, argv[3], argv[4], argv[5]);
    if (cmd == "rebuild-patch") return cmdRebuildPatch(*arena2, argv[3], argc > 4 ? argv[4] : "");
    return usage();
}
