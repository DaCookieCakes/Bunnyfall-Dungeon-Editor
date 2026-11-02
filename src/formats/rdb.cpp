#include "formats/rdb.h"

#include <algorithm>
#include <unordered_set>

namespace de::formats {

namespace {

constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kModelRefSize = 8;
constexpr std::size_t kModelDataSize = 4;
constexpr std::size_t kObjectSectionHeaderSize = 512;
constexpr std::size_t kExpectedRootOffset =
    kHeaderSize + kRdbModelRefSlots * kModelRefSize + kRdbModelRefSlots * kModelDataSize + kObjectSectionHeaderSize;
// The largest classic block holds 391 objects; this bound only stops a corrupt chain from looping forever.
constexpr std::size_t kMaxObjectsPerRoot = 16384;
constexpr std::size_t kMaxRoots = 1024;

bool allDigits(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

RdbModel readModel(const ByteView& v, std::size_t at) {
    RdbModel m;
    m.xRotation = v.i32(at);
    m.yRotation = v.i32(at + 4);
    m.zRotation = v.i32(at + 8);
    m.modelIndex = v.u16(at + 12);
    m.triggerFlagStartingLock = v.u32(at + 14);
    m.soundIndex = v.u8(at + 18);
    m.actionOffset = v.i32(at + 19);
    if (m.hasAction()) {
        const auto a = static_cast<std::size_t>(m.actionOffset);
        m.action.position = static_cast<std::uint32_t>(a);
        m.action.axis = v.u8(a);
        m.action.duration = v.u16(a + 1);
        m.action.magnitude = v.u16(a + 3);
        m.action.nextObjectOffset = v.i32(a + 5);
        m.action.flags = v.u8(a + 9);
    }
    return m;
}

RdbFlat readFlat(const ByteView& v, std::size_t at) {
    RdbFlat f;
    f.position = static_cast<std::uint32_t>(at);
    f.textureBitfield = v.u16(at);
    f.flags = v.u16(at + 2);
    f.magnitude = v.u8(at + 4);
    f.soundIndex = v.u8(at + 5);
    f.nextObjectOffset = v.i32(at + 6);
    f.action = v.u8(at + 10);
    return f;
}

RdbLight readLight(const ByteView& v, std::size_t at) {
    RdbLight l;
    l.unknown1 = v.u32(at);
    l.unknown2 = v.u32(at + 4);
    l.radius = v.u16(at + 8);
    return l;
}

Result<std::vector<RdbObject>> readRoot(const ByteView& v, std::int32_t rootOffset) {
    std::vector<RdbObject> objects;
    std::unordered_set<std::int32_t> seen;
    std::int32_t pos = rootOffset;
    while (true) {
        if (!seen.insert(pos).second) return fail("object chain loops at offset " + std::to_string(pos));
        if (objects.size() >= kMaxObjectsPerRoot) return fail("object chain too long");
        const auto at = static_cast<std::size_t>(pos);
        RdbObject obj;
        obj.position = pos;
        obj.index = static_cast<std::int32_t>(objects.size());
        const auto next = v.i32(at);
        obj.x = v.i32(at + 8);
        obj.y = v.i32(at + 12);
        obj.z = v.i32(at + 16);
        const auto rawType = v.u8(at + 20);
        const auto resourceAt = static_cast<std::size_t>(v.u32(at + 21));
        switch (rawType) {
            case 0x01: obj.type = RdbObjectType::Model; obj.resource = readModel(v, resourceAt); break;
            case 0x02: obj.type = RdbObjectType::Light; obj.resource = readLight(v, resourceAt); break;
            case 0x03: obj.type = RdbObjectType::Flat; obj.resource = readFlat(v, resourceAt); break;
            default: return fail("object at " + std::to_string(pos) + " has unknown type " + std::to_string(rawType));
        }
        objects.push_back(std::move(obj));
        if (next < 0) break;
        pos = next;
    }

    // Link chains within the root: a model action's next object learns who points at it.
    for (auto& obj : objects) {
        auto* model = std::get_if<RdbModel>(&obj.resource);
        if (!model || !model->hasAction() || model->action.nextObjectOffset < 0) continue;
        for (std::size_t i = 0; i < objects.size(); ++i) {
            if (objects[i].position != model->action.nextObjectOffset) continue;
            model->action.nextObjectIndex = static_cast<std::int32_t>(i);
            if (auto* target = std::get_if<RdbModel>(&objects[i].resource)) {
                target->action.previousObjectOffset = obj.position;
            }
            break;
        }
    }
    return objects;
}

} // namespace

RdbRole rdbRoleFromName(std::string_view blockName) {
    if (blockName.empty()) return RdbRole::Unknown;
    switch (blockName.front()) {
        case 'B': return RdbRole::Border;
        case 'N': return RdbRole::Normal;
        case 'W': return RdbRole::Wet;
        case 'S': return RdbRole::Quest;
        case 'M': return RdbRole::Mausoleum;
        default: return RdbRole::Unknown;
    }
}

std::string_view roleName(RdbRole role) {
    switch (role) {
        case RdbRole::Border: return "border";
        case RdbRole::Normal: return "normal";
        case RdbRole::Wet: return "wet";
        case RdbRole::Quest: return "quest";
        case RdbRole::Mausoleum: return "mausoleum";
        case RdbRole::Unknown: break;
    }
    return "unknown";
}

std::size_t RdbBlock::objectCount() const {
    std::size_t n = 0;
    for (const auto& root : roots) n += root.objects.size();
    return n;
}

Result<RdbBlock> parseRdbBlock(ByteView v) {
    try {
        RdbBlock block;
        block.width = v.u32(4);
        block.height = v.u32(8);
        const auto rootOffset = v.u32(12);
        if (rootOffset != kExpectedRootOffset) {
            return fail("object root offset " + std::to_string(rootOffset) + ", expected " +
                        std::to_string(kExpectedRootOffset));
        }
        const auto rootCount = static_cast<std::size_t>(block.width) * block.height;
        if (rootCount > kMaxRoots) return fail("root list of " + std::to_string(rootCount) + " entries");

        block.modelRefs.reserve(kRdbModelRefSlots);
        for (std::size_t i = 0; i < kRdbModelRefSlots; ++i) {
            const auto at = kHeaderSize + i * kModelRefSize;
            RdbModelRef ref;
            ref.modelId = v.fixedString(at, 5);
            ref.description = v.fixedString(at + 5, 3);
            if (allDigits(ref.modelId)) ref.modelIdNum = static_cast<std::uint32_t>(std::stoul(ref.modelId));
            block.modelRefs.push_back(std::move(ref));
        }

        block.roots.resize(rootCount);
        for (std::size_t i = 0; i < rootCount; ++i) {
            auto& root = block.roots[i];
            root.offset = v.i32(rootOffset + i * 4);
            if (root.offset < 0) continue;
            auto objects = readRoot(v, root.offset);
            if (!objects) return fail("root " + std::to_string(i) + ": " + objects.error().message);
            root.objects = std::move(*objects);
        }

        for (const auto& root : block.roots) {
            for (const auto& obj : root.objects) {
                if (const auto* m = obj.model(); m && m->modelIndex >= kRdbModelRefSlots) {
                    return fail("object at " + std::to_string(obj.position) + " names model slot " +
                                std::to_string(m->modelIndex));
                }
            }
        }
        return block;
    } catch (const OutOfBounds& e) {
        return fail(std::string("truncated block: ") + e.what());
    }
}

} // namespace de::formats
