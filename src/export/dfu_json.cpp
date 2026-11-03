#include "export/dfu_json.h"

#include "core/sha256.h"

namespace de::exporter {

namespace {

using formats::RdbObjectType;

Json modelResourceJson(const formats::RdbModel& m) {
    const auto& a = m.action;
    return {
        {"XRotation", m.xRotation},
        {"YRotation", m.yRotation},
        {"ZRotation", m.zRotation},
        {"ModelIndex", m.modelIndex},
        {"TriggerFlag_StartingLock", m.triggerFlagStartingLock},
        {"SoundIndex", m.soundIndex},
        {"ActionResource",
         {
             {"Position", a.position},
             {"Axis", a.axis},
             {"Duration", a.duration},
             {"Magnitude", a.magnitude},
             {"NextObjectOffset", a.nextObjectOffset},
             {"PreviousObjectOffset", a.previousObjectOffset},
             {"NextObjectIndex", a.nextObjectIndex},
             {"Flags", a.flags},
         }},
    };
}

Json flatResourceJson(const formats::RdbFlat& f) {
    return {
        {"Position", f.position},
        {"TextureArchive", f.textureArchive()},
        {"TextureRecord", f.textureRecord()},
        {"Flags", f.flags},
        {"Magnitude", f.magnitude},
        {"SoundIndex", f.soundIndex},
        {"FactionOrMobileId", f.factionOrMobileId()},
        {"NextObjectOffset", f.nextObjectOffset},
        {"Action", f.action},
    };
}

Json objectJson(const formats::RdbObject& o) {
    Json resources = Json::object();
    const char* type = "Model";
    if (const auto* m = o.model()) {
        resources["ModelResource"] = modelResourceJson(*m);
    } else if (const auto* l = o.light()) {
        type = "Light";
        resources["LightResource"] = {{"Unknown1", l->unknown1}, {"Unknown2", l->unknown2}, {"Radius", l->radius}};
    } else if (const auto* f = o.flat()) {
        type = "Flat";
        resources["FlatResource"] = flatResourceJson(*f);
    }
    return {
        {"Position", o.position}, {"Index", o.index}, {"XPos", o.x}, {"YPos", o.y}, {"ZPos", o.z},
        {"Type", type},           {"Resources", resources},
    };
}

/// What an RDB block carries for its (unused) RMB half: an RmbBlockDesc at its defaults.
Json emptyRmbJson() {
    return {
        {"FldHeader",
         {
             {"NumBlockDataRecords", 0},
             {"NumMisc3dObjectRecords", 0},
             {"NumMiscFlatObjectRecords", 0},
             {"BlockPositions", nullptr},
             {"BuildingDataList", nullptr},
             {"BlockDataSizes", nullptr},
             {"GroundData", Json::object()},
             {"AutoMapData", nullptr},
             {"Name", nullptr},
             {"OtherNames", nullptr},
         }},
        {"SubRecords", nullptr},
        {"Misc3dObjectRecords", nullptr},
        {"MiscFlatObjectRecords", nullptr},
    };
}

} // namespace

Json rdbBlockToJson(const formats::RdbBlock& block, std::string_view name, std::size_t index, std::uint32_t position) {
    Json refs = Json::array();
    for (const auto& ref : block.modelRefs) {
        if (ref.description == formats::kUnusedModelDescription) break;   // the list ends at the first unused slot
        refs.push_back({{"ModelId", ref.modelId}, {"ModelIdNum", ref.modelIdNum}, {"Description", ref.description}});
    }
    Json roots = Json::array();
    for (const auto& root : block.roots) {
        if (root.objects.empty()) {
            roots.push_back({{"RdbObjects", nullptr}});
            continue;
        }
        Json objects = Json::array();
        for (const auto& o : root.objects) objects.push_back(objectJson(o));
        roots.push_back({{"RdbObjects", std::move(objects)}});
    }
    return {
        {"Position", position},
        {"Index", index},
        {"Name", name},
        {"Type", "Rdb"},
        {"RmbBlock", emptyRmbJson()},
        {"RdbBlock", {{"ModelReferenceList", std::move(refs)}, {"ObjectRootList", std::move(roots)}}},
        {"RdiBlock", {{"Data", nullptr}}},
    };
}

std::string canonicalJson(const Json& json) {
    // Sorted keys come from the object type; escaping matches JSON.stringify for valid UTF-8 (control characters as
    // \b \f \n \r \t or lower-case \u00xx, everything else as is).
    return json.dump(-1, ' ', false, Json::error_handler_t::strict);
}

std::string canonicalSha256(const Json& json) { return sha256Hex(canonicalJson(json)); }

} // namespace de::exporter
