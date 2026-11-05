#include "mesh/mesh_json.h"

#include <cmath>

namespace de::mesh {

namespace {

constexpr double kMetresPerUnit = 1.0 / catalog::kBlockUnitsPerMetre;   // Daggerfall Unity's GlobalScale, 0.025

double rounded(double v, double step) {
    const double r = std::round(v / step) * step;
    return r == 0 ? 0.0 : r;   // no "-0"
}

} // namespace

Json meshToJson(const Mesh& mesh) {
    Json points = Json::array();
    for (const auto& p : mesh.points) points.push_back({rounded(p.x, 1e-4), rounded(p.y, 1e-4), rounded(p.z, 1e-4)});
    Json faces = Json::array();
    for (const auto& f : mesh.faces) {
        faces.push_back({{"points", f.points},
                         {"texture", {f.texture.archive, f.texture.record}},
                         {"uv", {f.uv.scaleU, f.uv.scaleV, f.uv.offsetU, f.uv.offsetV, f.uv.angle}}});
    }
    return {{"points", std::move(points)}, {"faces", std::move(faces)}};
}

Result<Mesh> meshFromJson(const Json& json) {
    if (!json.is_object()) return fail("a mesh is not a JSON object");
    Mesh mesh;
    try {
        for (const auto& p : json.value("points", Json::array())) {
            if (!p.is_array() || p.size() != 3) return fail("a mesh point is not [x, y, z]");
            mesh.points.push_back({p[0].get<double>(), p[1].get<double>(), p[2].get<double>()});
        }
        for (const auto& f : json.value("faces", Json::array())) {
            Face face;
            face.points = f.at("points").get<std::vector<std::uint32_t>>();
            for (const auto p : face.points) {
                if (p >= mesh.points.size()) return fail("a mesh face names a point that does not exist");
            }
            const auto& t = f.at("texture");
            face.texture = {t.at(0).get<int>(), t.at(1).get<int>()};
            if (const auto uv = f.value("uv", Json::array()); uv.size() == 5) {
                face.uv = {uv[0].get<double>(), uv[1].get<double>(), uv[2].get<double>(), uv[3].get<double>(),
                           uv[4].get<double>()};
            }
            mesh.faces.push_back(std::move(face));
        }
    } catch (const Json::exception& e) {
        return fail(std::string("a mesh does not read: ") + e.what());
    }
    return mesh;
}

Json bunnyfallModelJson(const Mesh& mesh, std::uint32_t modelId, std::string_view name, const TextureSize& size) {
    const auto drawn = toModelMesh(mesh, modelId);
    Json positions = Json::array(), normals = Json::array(), uvs = Json::array(), subMeshes = Json::array();
    // Each vertex belongs to one sub-mesh (faces are grouped by texture), so its UVs divide by that texture's size.
    std::vector<std::pair<int, int>> sizeOf(drawn.vertices.size(), {64, 64});
    for (const auto& sm : drawn.subMeshes) {
        auto s = size(sm.textureArchive, sm.textureRecord);
        if (s.first <= 0 || s.second <= 0) s = {64, 64};
        for (std::uint32_t i = sm.firstIndex; i < sm.firstIndex + sm.indexCount; ++i) sizeOf[drawn.indices[i]] = s;
        subMeshes.push_back({{"textureArchive", sm.textureArchive},
                             {"textureRecord", sm.textureRecord},
                             {"startIndex", sm.firstIndex},
                             {"primitiveCount", sm.indexCount / 3}});
    }
    for (std::size_t i = 0; i < drawn.vertices.size(); ++i) {
        const auto& v = drawn.vertices[i];
        for (const double c : {v.position.x, v.position.y, v.position.z}) positions.push_back(rounded(c * kMetresPerUnit, 1e-4));
        for (const double c : {v.normal.x, v.normal.y, v.normal.z}) normals.push_back(rounded(c, 1e-4));
        uvs.push_back(rounded(double(v.u) / sizeOf[i].first, 1e-5));
        uvs.push_back(rounded(-(double(v.v) / sizeOf[i].second), 1e-5));
    }
    return {{"format", kModelFileFormat},
            {"id", modelId},
            {"name", name},
            {"positions", std::move(positions)},
            {"normals", std::move(normals)},
            {"uvs", std::move(uvs)},
            {"indices", drawn.indices},
            {"subMeshes", std::move(subMeshes)}};
}

} // namespace de::mesh
