#include "export/dfu_json.h"

#include "core/sha256.h"

#include <cmath>
#include <limits>

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
    Json json = {
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
    // Blank lines are rows still being typed, not words. A flat with nothing said keeps the classic shape exactly.
    Json lines = Json::array();
    for (const auto& line : f.personLines) {
        if (line.find_first_not_of(" \t") != std::string::npos) lines.push_back(line);
    }
    if (!f.personName.empty() || !lines.empty()) json["BunnyfallPerson"] = {{"Name", f.personName}, {"Lines", std::move(lines)}};
    return json;
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

namespace {

/// The block's unused model slot: five U+FFFD for the id, three for the description (how 0xFF bytes decode).
formats::RdbModelRef unusedModelRef() {
    std::string id;
    for (int i = 0; i < 5; ++i) id += "\xEF\xBF\xBD";
    return {id, 0, std::string(formats::kUnusedModelDescription)};
}

template <typename T>
Result<T> number(const Json& j, const char* key, const std::string& where) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return fail(where + ": " + key + " missing or not a whole number");
    const auto v = it->get<std::int64_t>();
    if (v < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        v > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
        return fail(where + ": " + key + " out of range (" + std::to_string(v) + ")");
    }
    return static_cast<T>(v);
}

#define DE_TRY_NUMBER(var, type, json, key, where)       \
    type var{};                                          \
    {                                                    \
        auto r_ = number<type>(json, key, where);        \
        if (!r_) return std::unexpected(r_.error());     \
        var = *r_;                                       \
    }

Result<formats::RdbObject> objectFromJson(const Json& o, const std::string& where) {
    formats::RdbObject obj;
    DE_TRY_NUMBER(position, std::int32_t, o, "Position", where);
    DE_TRY_NUMBER(index, std::int32_t, o, "Index", where);
    DE_TRY_NUMBER(x, std::int32_t, o, "XPos", where);
    DE_TRY_NUMBER(y, std::int32_t, o, "YPos", where);
    DE_TRY_NUMBER(z, std::int32_t, o, "ZPos", where);
    obj.position = position;
    obj.index = index;
    obj.x = x;
    obj.y = y;
    obj.z = z;
    const std::string type = o.value("Type", "");
    const auto& res = o.value("Resources", Json::object());
    if (type == "Model") {
        const auto& m = res.value("ModelResource", Json::object());
        const auto& a = m.value("ActionResource", Json::object());
        const auto w = where + " model";
        formats::RdbModel model;
        DE_TRY_NUMBER(xr, std::int32_t, m, "XRotation", w);
        DE_TRY_NUMBER(yr, std::int32_t, m, "YRotation", w);
        DE_TRY_NUMBER(zr, std::int32_t, m, "ZRotation", w);
        DE_TRY_NUMBER(modelIndex, std::uint16_t, m, "ModelIndex", w);
        DE_TRY_NUMBER(trig, std::uint32_t, m, "TriggerFlag_StartingLock", w);
        DE_TRY_NUMBER(sound, std::uint8_t, m, "SoundIndex", w);
        DE_TRY_NUMBER(apos, std::uint32_t, a, "Position", w);
        DE_TRY_NUMBER(axis, std::uint8_t, a, "Axis", w);
        DE_TRY_NUMBER(duration, std::uint16_t, a, "Duration", w);
        DE_TRY_NUMBER(magnitude, std::uint16_t, a, "Magnitude", w);
        DE_TRY_NUMBER(next, std::int32_t, a, "NextObjectOffset", w);
        DE_TRY_NUMBER(prev, std::int32_t, a, "PreviousObjectOffset", w);
        DE_TRY_NUMBER(nextIndex, std::int32_t, a, "NextObjectIndex", w);
        DE_TRY_NUMBER(flags, std::uint8_t, a, "Flags", w);
        model.xRotation = xr;
        model.yRotation = yr;
        model.zRotation = zr;
        model.modelIndex = modelIndex;
        model.triggerFlagStartingLock = trig;
        model.soundIndex = sound;
        // The JSON keeps the action record's own offset; a model has an action exactly when that offset is set.
        model.actionOffset = static_cast<std::int32_t>(apos);
        model.action = {apos, axis, duration, magnitude, next, flags, prev, nextIndex};
        obj.type = formats::RdbObjectType::Model;
        obj.resource = model;
    } else if (type == "Flat") {
        const auto& f = res.value("FlatResource", Json::object());
        const auto w = where + " flat";
        if (f.value("IsCustomData", false)) return fail(w + ": IsCustomData is not supported yet");
        DE_TRY_NUMBER(fpos, std::uint32_t, f, "Position", w);
        DE_TRY_NUMBER(archive, std::uint16_t, f, "TextureArchive", w);
        DE_TRY_NUMBER(record, std::uint16_t, f, "TextureRecord", w);
        DE_TRY_NUMBER(flags, std::uint16_t, f, "Flags", w);
        DE_TRY_NUMBER(magnitude, std::uint8_t, f, "Magnitude", w);
        DE_TRY_NUMBER(sound, std::uint8_t, f, "SoundIndex", w);
        DE_TRY_NUMBER(mobile, std::uint16_t, f, "FactionOrMobileId", w);
        DE_TRY_NUMBER(next, std::int32_t, f, "NextObjectOffset", w);
        DE_TRY_NUMBER(action, std::uint8_t, f, "Action", w);
        if (archive > 0x1ff || record > 0x7f) return fail(w + ": texture " + std::to_string(archive) + "/" +
                                                          std::to_string(record) + " does not fit the format");
        formats::RdbFlat flat;
        flat.position = fpos;
        flat.textureBitfield = static_cast<std::uint16_t>((archive << 7) | record);
        flat.flags = flags;
        flat.magnitude = magnitude;
        flat.soundIndex = sound;
        flat.nextObjectOffset = next;
        flat.action = action;
        if (flat.factionOrMobileId() != mobile) {
            return fail(w + ": FactionOrMobileId " + std::to_string(mobile) + " is not Magnitude and SoundIndex");
        }
        if (const auto person = f.find("BunnyfallPerson"); person != f.end()) {
            const auto name = person->find("Name");
            const auto lines = person->find("Lines");
            if (!person->is_object() || name == person->end() || !name->is_string() || lines == person->end() || !lines->is_array()) {
                return fail(w + ": BunnyfallPerson needs a Name and Lines");
            }
            flat.personName = name->get<std::string>();
            for (const auto& line : *lines) {
                if (!line.is_string()) return fail(w + ": a BunnyfallPerson line is not text");
                flat.personLines.push_back(line.get<std::string>());
            }
        }
        obj.type = formats::RdbObjectType::Flat;
        obj.resource = flat;
    } else if (type == "Light") {
        const auto& l = res.value("LightResource", Json::object());
        const auto w = where + " light";
        DE_TRY_NUMBER(u1, std::uint32_t, l, "Unknown1", w);
        DE_TRY_NUMBER(u2, std::uint32_t, l, "Unknown2", w);
        DE_TRY_NUMBER(radius, std::uint16_t, l, "Radius", w);
        obj.type = formats::RdbObjectType::Light;
        obj.resource = formats::RdbLight{u1, u2, radius};
    } else {
        return fail(where + ": unknown object type '" + type + "'");
    }
    return obj;
}

#undef DE_TRY_NUMBER

} // namespace

Result<formats::RdbBlock> rdbBlockFromJson(const Json& json) {
    if (!json.is_object() || json.value("Type", "") != "Rdb") return fail("not an RDB block");
    const std::string name = json.value("Name", "?");
    const auto& rdb = json.value("RdbBlock", Json::object());
    formats::RdbBlock block;
    for (const auto& ref : rdb.value("ModelReferenceList", Json::array())) {
        auto idNum = number<std::uint32_t>(ref, "ModelIdNum", name + " model list");
        if (!idNum) return std::unexpected(idNum.error());
        block.modelRefs.push_back({ref.value("ModelId", ""), *idNum, ref.value("Description", "")});
    }
    if (block.modelRefs.size() > formats::kRdbModelRefSlots) return fail(name + ": more than 750 model references");
    while (block.modelRefs.size() < formats::kRdbModelRefSlots) block.modelRefs.push_back(unusedModelRef());

    const auto& roots = rdb.value("ObjectRootList", Json::array());
    for (std::size_t r = 0; r < roots.size(); ++r) {
        formats::RdbRoot root;
        const auto& objects = roots[r].value("RdbObjects", Json());
        if (objects.is_array()) {
            for (std::size_t i = 0; i < objects.size(); ++i) {
                auto obj = objectFromJson(objects[i], name + " object " + std::to_string(r) + "." + std::to_string(i));
                if (!obj) return std::unexpected(obj.error());
                if (const auto* m = obj->model(); m && m->modelIndex >= formats::kRdbModelRefSlots) {
                    return fail(name + ": a model names slot " + std::to_string(m->modelIndex));
                }
                root.objects.push_back(std::move(*obj));
            }
        }
        root.offset = root.objects.empty() ? -1 : 0;
        block.roots.push_back(std::move(root));
    }
    // The JSON does not carry the root list's shape; classic lists are square.
    const auto n = static_cast<std::uint32_t>(block.roots.size());
    auto side = static_cast<std::uint32_t>(std::lround(std::sqrt(double(n))));
    if (side * side == n) {
        block.width = block.height = side;
    } else {
        block.width = n;
        block.height = 1;
    }
    return block;
}

std::string canonicalJson(const Json& json) {
    // Sorted keys come from the object type; escaping matches JSON.stringify for valid UTF-8 (control characters as
    // \b \f \n \r \t or lower-case \u00xx, everything else as is).
    return json.dump(-1, ' ', false, Json::error_handler_t::strict);
}

std::string canonicalSha256(const Json& json) { return sha256Hex(canonicalJson(json)); }

} // namespace de::exporter
