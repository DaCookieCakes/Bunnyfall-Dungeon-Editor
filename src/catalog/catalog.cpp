#include "catalog/catalog.h"

#include "formats/arch3d.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace de::catalog {

namespace {

constexpr std::uint32_t kExitDoorModel = 70300;
constexpr std::uint32_t kRedBrickDoorModel = 72100;
constexpr int kEditorFlatArchive = 199;
constexpr int kStartMarkerRecord = 10;
constexpr double kEdgeTolerance = 1.0;   // block units
constexpr double kStructuralSize = 96;   // block units: three quarters of a 128-unit cell

Box meshBounds(const formats::Arch3dMesh& mesh) {
    const auto& b = mesh.bounds;
    // Y flips, so the file's max becomes editor min.
    return {fromMeshPoint(b.min.x, b.max.y, b.min.z), fromMeshPoint(b.max.x, b.min.y, b.max.z)};
}

/// The rotated, placed box of one model object.
Box placedBox(const formats::RdbObject& obj, const formats::RdbModel& m, const ModelInfo& info) {
    return transformBox(info.bounds, rdbModelRotation(m.xRotation, m.yRotation, m.zRotation),
                        fromFilePosition(obj.x, obj.y, obj.z));
}

std::int32_t toInt(double v) { return static_cast<std::int32_t>(std::lround(v)); }

} // namespace

std::string_view kindName(ModelKind kind) {
    switch (kind) {
        case ModelKind::Architecture: return "architecture";
        case ModelKind::Door: return "door";
        case ModelKind::ExitDoor: return "exit door";
        case ModelKind::Prop: break;
    }
    return "prop";
}

std::string_view edgeName(Edge edge) {
    switch (edge) {
        case Edge::West: return "W";
        case Edge::East: return "E";
        case Edge::South: return "S";
        case Edge::North: break;
    }
    return "N";
}

std::string_view markerName(int record) {
    switch (record) {
        case 8: return "enter marker";
        case 10: return "start marker";
        case 11: return "quest spawn marker";
        case 15: return "random enemy";
        case 16: return "fixed enemy";
        case 18: return "quest item marker";
        case 19: return "random treasure";
        default: return "unknown marker";
    }
}

ModelKind classifyModel(std::uint32_t id, std::string_view description, const Box& bounds) {
    if (id == kExitDoorModel) return ModelKind::ExitDoor;
    if (id != kRedBrickDoorModel &&
        (description == "DOR" || description == "DDR" || description == "NEW" || description == "CAV")) {
        return ModelKind::Door;
    }
    // Families that are always structure: corridors and junctions ("C??"), large rooms ("R??"), odd-shaped rooms
    // ("O??"), and rooms named by size in 128-unit cells less one ("000" one cell, "012" two by three).
    const bool roomSize =
        description.size() == 3 && std::ranges::all_of(description, [](char c) { return c >= '0' && c <= '9'; });
    if (roomSize || description.starts_with('C') || description.starts_with('R') || description.starts_with('O')) {
        return ModelKind::Architecture;
    }
    // Other families mix structure and decoration (the "L" family holds a corridor cube and a lamp), so size decides:
    // a piece at least most of a cell in every direction is something to walk through, not to look at.
    const auto size = bounds.size();
    if (size.x >= kStructuralSize && size.y >= kStructuralSize && size.z >= kStructuralSize) {
        return ModelKind::Architecture;
    }
    return ModelKind::Prop;
}

std::vector<Port> detectPorts(const formats::RdbBlock& block, const std::map<std::uint32_t, ModelInfo>& models) {
    // Where each architecture piece meets an edge plane, as an interval along that edge.
    struct Candidate { Edge edge; std::int32_t floor; double lo, hi; std::uint32_t modelId; };
    std::vector<Candidate> candidates;
    const double side = kRdbBlockSide;
    struct Plane { Edge edge; double at; bool isX; };   // isX: the plane is x = at (else z = at)
    constexpr Plane planes[] = {
        {Edge::West, 0, true}, {Edge::East, kRdbBlockSide, true},
        {Edge::South, 0, false}, {Edge::North, kRdbBlockSide, false},
    };

    std::vector<Vec3> placed;
    for (const auto& root : block.roots) {
        for (const auto& obj : root.objects) {
            const auto* m = obj.model();
            if (!m) continue;
            const auto it = models.find(block.modelRefs[m->modelIndex].modelIdNum);
            if (it == models.end() || it->second.kind != ModelKind::Architecture) continue;
            const auto& info = it->second;
            const Box box = placedBox(obj, *m, info);

            const Mat3 rotation = rdbModelRotation(m->xRotation, m->yRotation, m->zRotation);
            const Vec3 position = fromFilePosition(obj.x, obj.y, obj.z);
            placed.clear();
            for (const auto& p : info.points) placed.push_back(rotation * p + position);

            for (const auto& plane : planes) {
                const double lo = plane.isX ? box.min.x : box.min.z, hi = plane.isX ? box.max.x : box.max.z;
                if (hi < plane.at - kEdgeTolerance || lo > plane.at + kEdgeTolerance) continue;   // never reaches it
                double alongLo = 1e300, alongHi = -1e300, floor = 1e300;
                auto take = [&](const Vec3& p) {
                    const double along = plane.isX ? p.z : p.x;
                    alongLo = std::min(alongLo, along);
                    alongHi = std::max(alongHi, along);
                    floor = std::min(floor, p.y);
                };
                for (const auto& face : info.faces) {
                    bool offPlane = false;
                    for (const auto i : face) {
                        const auto& p = placed[i];
                        if (std::abs((plane.isX ? p.x : p.z) - plane.at) > kEdgeTolerance) offPlane = true;
                    }
                    if (!offPlane) continue;   // lies in the plane: a wall closing the block
                    for (std::size_t k = 0; k < face.size(); ++k) {
                        const auto& a = placed[face[k]];
                        const auto& b = placed[face[(k + 1) % face.size()]];
                        const double da = (plane.isX ? a.x : a.z) - plane.at;
                        const double db = (plane.isX ? b.x : b.z) - plane.at;
                        if (std::abs(da) <= kEdgeTolerance) take(a);
                        if ((da < -kEdgeTolerance && db > kEdgeTolerance) || (da > kEdgeTolerance && db < -kEdgeTolerance)) {
                            const double t = da / (da - db);
                            take({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
                        }
                    }
                }
                if (alongHi - alongLo <= kEdgeTolerance) continue;   // touches at a point or a line only
                if (alongHi <= 0 || alongLo >= side) continue;       // beyond the block's corner
                candidates.push_back({plane.edge, toInt(floor), alongLo, alongHi, info.id});
            }
        }
    }

    // Pieces overlapping on one edge and floor are one opening; its extent is all of them together.
    std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) {
        return std::tie(a.edge, a.floor, a.lo, a.hi, a.modelId) < std::tie(b.edge, b.floor, b.lo, b.hi, b.modelId);
    });
    std::vector<Port> out;
    for (std::size_t i = 0; i < candidates.size();) {
        double clusterLo = candidates[i].lo, clusterHi = candidates[i].hi;
        std::size_t j = i + 1;
        for (; j < candidates.size(); ++j) {
            const auto& c = candidates[j];
            if (c.edge != candidates[i].edge || c.floor != candidates[i].floor || c.lo >= clusterHi) break;
            clusterHi = std::max(clusterHi, c.hi);
        }
        out.push_back({candidates[i].edge, toInt((clusterLo + clusterHi) / 2), candidates[i].floor,
                       toInt(clusterHi - clusterLo), candidates[i].modelId});
        i = j;
    }
    return out;
}

SocketMap standardSocketsOf(const std::vector<Port>& ports) {
    SocketMap map{};
    for (const auto& port : ports) {
        for (std::size_t i = 0; i < std::size(kStandardSockets); ++i) {
            if (port.along == kStandardSockets[i].along && port.floor == kStandardSockets[i].floor) {
                map[static_cast<std::size_t>(port.edge)][i] = true;
            }
        }
    }
    return map;
}

Result<Catalog> Catalog::build(const formats::Arena2& arena2) {
    Catalog catalog;
    std::vector<std::pair<std::string, formats::RdbBlock>> parsed;
    std::map<std::uint32_t, std::map<std::string, std::size_t>> descriptions;

    for (std::size_t i = 0; i < arena2.blocks.count(); ++i) {
        const auto& name = arena2.blocks.entry(i).name;
        if (!name.ends_with(".RDB")) continue;
        auto block = formats::readClassicRdbBlock(arena2.blocks, i);
        if (!block) return fail(name + ": " + block.error().message);
        for (const auto& root : block->roots)
            for (const auto& obj : root.objects)
                if (const auto* m = obj.model()) {
                    const auto& ref = block->modelRefs[m->modelIndex];
                    ++descriptions[ref.modelIdNum][ref.description];
                }
        parsed.emplace_back(name, std::move(*block));
    }

    for (const auto& [id, counts] : descriptions) {
        const auto index = arena2.arch3d.findById(id);
        if (!index) return fail("model " + std::to_string(id) + " is used by a block but not in ARCH3D.BSA");
        auto mesh = formats::parseArch3dMesh(arena2.arch3d.record(*index), id);
        if (!mesh) return std::unexpected(mesh.error());
        ModelInfo info;
        info.id = id;
        // The most used description; ties go to the alphabetically first, so the result never depends on map order.
        const auto best = std::ranges::max_element(counts, [](const auto& a, const auto& b) {
            return a.second < b.second || (a.second == b.second && a.first > b.first);
        });
        info.description = best->first;
        info.bounds = meshBounds(*mesh);
        info.kind = classifyModel(id, info.description, info.bounds);
        info.points.reserve(mesh->points.size());
        for (const auto& p : mesh->points) info.points.push_back(fromMeshPoint(p.x, p.y, p.z));
        info.faces.reserve(mesh->planes.size());
        for (const auto& plane : mesh->planes) {
            auto& face = info.faces.emplace_back();
            for (const auto& v : plane.vertices) face.push_back(v.point);
        }
        for (const auto& [_, n] : counts) info.uses += n;
        catalog.models_.emplace(id, std::move(info));
    }

    for (const auto& [name, block] : parsed) {
        BlockInfo info;
        info.name = name;
        info.role = formats::rdbRoleFromName(name);
        info.objects = block.objectCount();
        bool sawStart = false;
        for (const auto& root : block.roots) {
            for (const auto& obj : root.objects) {
                if (const auto* m = obj.model()) {
                    ++info.models;
                    if (block.modelRefs[m->modelIndex].modelIdNum == kExitDoorModel) info.hasExitDoor = true;
                } else if (const auto* f = obj.flat()) {
                    ++info.flats;
                    if (f->textureArchive() != kEditorFlatArchive) continue;
                    ++info.editorMarkers[f->textureRecord()];
                    if (f->textureRecord() == kStartMarkerRecord && !sawStart) {
                        sawStart = true;
                        if (f->soundIndex != 0) info.waterLevel = -8 * f->soundIndex;
                    }
                } else {
                    ++info.lights;
                }
            }
        }
        info.ports = detectPorts(block, catalog.models_);
        info.sockets = standardSocketsOf(info.ports);
        for (const auto& port : info.ports) {
            const bool standard = std::ranges::any_of(kStandardSockets, [&](const Socket& s) {
                return port.along == s.along && port.floor == s.floor;
            });
            if (!standard) info.otherPorts.push_back(port);
        }
        catalog.blocks_.push_back(std::move(info));
    }
    return catalog;
}

const ModelInfo* Catalog::model(std::uint32_t id) const {
    const auto it = models_.find(id);
    return it == models_.end() ? nullptr : &it->second;
}

const BlockInfo* Catalog::block(std::string_view name) const {
    const auto it = std::ranges::find(blocks_, name, &BlockInfo::name);
    return it == blocks_.end() ? nullptr : &*it;
}

} // namespace de::catalog
