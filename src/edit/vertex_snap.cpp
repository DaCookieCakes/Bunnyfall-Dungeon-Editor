#include "edit/vertex_snap.h"

#include <cmath>
#include <map>
#include <numeric>
#include <tuple>
#include <utility>

namespace de::edit {

namespace {

using catalog::Vec3;

/// Points closer than this (block units) are one point. ARCH3D points are 1/256 apart at the finest.
constexpr double kWeld = 1.0 / 16;

std::uint32_t findRoot(std::vector<std::uint32_t>& parent, std::uint32_t i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

double distance2(Vec3 a, Vec3 b) {
    const Vec3 d = a - b;
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

} // namespace

std::vector<Rim> openRims(const std::vector<Vec3>& points, const std::vector<std::vector<std::uint32_t>>& faces) {
    // One id per place.
    std::map<std::tuple<long long, long long, long long>, std::uint32_t> ids;
    std::vector<std::uint32_t> welded(points.size());
    std::vector<Vec3> places;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        const std::tuple key{std::llround(p.x / kWeld), std::llround(p.y / kWeld), std::llround(p.z / kWeld)};
        const auto [it, added] = ids.try_emplace(key, static_cast<std::uint32_t>(places.size()));
        if (added) places.push_back(p);
        welded[i] = it->second;
    }

    // How many faces use each edge.
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> uses;
    for (const auto& face : faces) {
        for (std::size_t i = 0; i < face.size(); ++i) {
            const auto a = face[i], b = face[(i + 1) % face.size()];
            if (a >= points.size() || b >= points.size()) continue;
            const auto wa = welded[a], wb = welded[b];
            if (wa == wb) continue;
            ++uses[{std::min(wa, wb), std::max(wa, wb)}];
        }
    }

    // Edges used once join their points into rims.
    std::vector<std::uint32_t> parent(places.size());
    std::iota(parent.begin(), parent.end(), 0u);
    std::vector<bool> onRim(places.size(), false);
    for (const auto& [edge, count] : uses) {
        if (count != 1) continue;
        onRim[edge.first] = onRim[edge.second] = true;
        parent[findRoot(parent, edge.first)] = findRoot(parent, edge.second);
    }
    std::map<std::uint32_t, std::size_t> rimOf;
    std::vector<Rim> rims;
    for (std::uint32_t i = 0; i < places.size(); ++i) {
        if (!onRim[i]) continue;
        const auto [it, added] = rimOf.try_emplace(findRoot(parent, i), rims.size());
        if (added) rims.emplace_back();
        rims[it->second].points.push_back(places[i]);
    }
    for (auto& r : rims) {
        Vec3 sum;
        for (const auto& p : r.points) sum = sum + p;
        r.centre = sum * (1.0 / double(r.points.size()));
    }
    return rims;
}

Rim placedRim(const Rim& rim, const catalog::Mat3& rotation, Vec3 translation) {
    Rim out;
    out.points.reserve(rim.points.size());
    for (const auto& p : rim.points) out.points.push_back(rotation * p + translation);
    out.centre = rotation * rim.centre + translation;
    return out;
}

std::optional<Vec3> rimShift(const Rim& moving, const Rim& fixed, double tolerance) {
    if (moving.points.size() != fixed.points.size() || moving.points.empty()) return std::nullopt;
    const Vec3 shift = fixed.centre - moving.centre;
    const double t2 = tolerance * tolerance;
    for (const auto& p : moving.points) {
        const Vec3 q = p + shift;
        bool found = false;
        for (const auto& f : fixed.points) {
            if (distance2(q, f) <= t2) {
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return shift;
}

} // namespace de::edit
