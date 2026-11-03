#pragma once

#include "catalog/space.h"
#include "core/error.h"
#include "formats/arena2.h"
#include "formats/rdb.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace de::catalog {

/// What a model is for, as far as building goes. Decided from the descriptions blocks give it.
enum class ModelKind {
    Architecture,   ///< Corridors, junctions, stairs and rooms: what a dungeon is walked through.
    Door,           ///< Action doors: DOR, DDR, NEW, CAV (except the red brick door, 72100).
    ExitDoor,       ///< 70300: only in a dungeon's starting block.
    Prop,           ///< Everything else.
};
std::string_view kindName(ModelKind kind);

struct ModelInfo {
    std::uint32_t id = 0;
    ModelKind kind = ModelKind::Prop;
    std::string description;    ///< The description classic blocks use most for this model.
    Box bounds;                 ///< Editor space, unrotated, block units.
    std::size_t uses = 0;       ///< Placements across all classic dungeon blocks.
    std::vector<Vec3> points;   ///< Editor space, block units.
    std::vector<std::vector<std::uint32_t>> faces;   ///< Each face's points, in order around it.
};

/// The side of a block a port sits on. West is x = 0, east x = 2048, south z = 0, north z = 2048.
enum class Edge { West, East, South, North };
std::string_view edgeName(Edge edge);

/// An opening in a block's edge: where architecture leaves the block so a neighbour's architecture can meet it.
struct Port {
    Edge edge = Edge::West;
    std::int32_t along = 0;   ///< Centre along the edge, block units from the edge's start (x for S/N, z for W/E).
    std::int32_t floor = 0;   ///< Height of the piece's bottom, editor space.
    std::int32_t width = 0;   ///< Extent along the edge.
    std::uint32_t modelId = 0;   ///< One piece that makes the opening (the first found).
    friend bool operator==(const Port&, const Port&) = default;
};

/// The two doorways every classic dungeon block offers on each edge (measured: all 187 blocks, see DESIGN.md §2.3).
/// Both are 50 block units wide; `along` and `floor` are in the same terms as Port.
struct Socket {
    std::int32_t along = 0;
    std::int32_t floor = 0;
};
inline constexpr Socket kStandardSockets[2] = {{640, 1280}, {1408, 512}};
inline constexpr std::int32_t kSocketWidth = 50;

/// Which standard sockets a set of ports opens: [edge][socket].
using SocketMap = std::array<std::array<bool, 2>, 4>;
SocketMap standardSocketsOf(const std::vector<Port>& ports);

/// What the editor needs to know about one classic block without reading it again.
struct BlockInfo {
    std::string name;
    formats::RdbRole role = formats::RdbRole::Unknown;
    std::size_t objects = 0;
    std::size_t models = 0, flats = 0, lights = 0;
    std::map<int, int> editorMarkers;   ///< 199 flat record -> count
    std::vector<Port> ports;
    SocketMap sockets{};             ///< Standard sockets this block opens.
    std::vector<Port> otherPorts;    ///< Openings that are not a standard socket.
    bool hasExitDoor = false;
    std::int32_t waterLevel = 10000;    ///< File units, from the first start marker; 10000 = no water.
};

/// Placeable models and classic blocks, built once from ARENA2.
class Catalog {
public:
    static Result<Catalog> build(const formats::Arena2& arena2);

    [[nodiscard]] const ModelInfo* model(std::uint32_t id) const;
    [[nodiscard]] const std::map<std::uint32_t, ModelInfo>& models() const { return models_; }
    [[nodiscard]] const BlockInfo* block(std::string_view name) const;
    [[nodiscard]] const std::vector<BlockInfo>& blocks() const { return blocks_; }

private:
    std::map<std::uint32_t, ModelInfo> models_;
    std::vector<BlockInfo> blocks_;
};

/// Openings in a block's four edges, from its architecture pieces. Each piece's faces are cut by the edge planes;
/// where faces pass through (or end on) a plane, the piece opens the block there. A face lying in the plane is a wall
/// closing the block and opens nothing. Openings that overlap on one edge and floor merge into one. Ordered by
/// edge, floor, then position.
std::vector<Port> detectPorts(const formats::RdbBlock& block, const std::map<std::uint32_t, ModelInfo>& models);

/// What an editor marker (a flat in archive 199) means, by its record. Records 17 and 20 are in classic data but
/// nothing reads them; they are named as unknown.
std::string_view markerName(int record);

/// A model's kind from its id, its most used description and its unrotated bounds.
ModelKind classifyModel(std::uint32_t id, std::string_view description, const Box& bounds);

} // namespace de::catalog
