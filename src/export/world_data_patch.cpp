#include "export/world_data_patch.h"

#include "formats/rdb.h"

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace de::exporter {

namespace {

std::string where(const Json& path) { return path.dump(); }

/// Follows `path` (all but its last step) to the container the op acts on.
Result<Json*> containerOf(Json& root, const Json& path) {
    Json* node = &root;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const auto& step = path[i];
        if (node->is_object() && step.is_string()) {
            const auto it = node->find(step.get<std::string>());
            if (it == node->end()) return fail("patch path " + where(path) + " does not reach a container");
            node = &*it;
        } else if (node->is_array() && step.is_number_integer()) {
            const auto k = step.get<std::int64_t>();
            if (k < 0 || static_cast<std::size_t>(k) >= node->size()) {
                return fail("patch path " + where(path) + " does not reach a container");
            }
            node = &(*node)[static_cast<std::size_t>(k)];
        } else {
            return fail("patch path " + where(path) + " does not reach a container");
        }
        if (!node->is_object() && !node->is_array()) return fail("patch path " + where(path) + " does not reach a container");
    }
    return node;
}

} // namespace

Result<Json> applyPatchOps(Json root, const Json& ops, const CopySource& copySource) {
    if (!ops.is_array()) return fail("patch ops are not a list");
    for (const auto& rawOp : ops) {
        if (!rawOp.is_array() || rawOp.size() < 2 || !rawOp[0].is_string() || !rawOp[1].is_array()) {
            return fail("malformed patch op " + rawOp.dump());
        }
        std::string kind = rawOp[0];
        const Json& path = rawOp[1];
        Json value = rawOp.size() > 2 ? rawOp[2] : Json();
        if (kind == "ci" || kind == "cs") {
            if (!copySource) return fail("patch has a copy op and nothing to copy from");
            auto copied = copySource(value);
            if (!copied) return std::unexpected(copied.error());
            value = std::move(*copied);
            kind = kind == "ci" ? "i" : "s";
        }

        if (path.empty()) {
            if (kind != "s") return fail("patch op '" + kind + "' at the root");
            root = std::move(value);
            continue;
        }
        auto parent = containerOf(root, path);
        if (!parent) return std::unexpected(parent.error());
        Json& p = **parent;
        const auto& key = path.back();

        if (kind == "s") {
            if (p.is_array()) {
                if (!key.is_number_integer() || key.get<std::int64_t>() < 0 ||
                    static_cast<std::size_t>(key.get<std::int64_t>()) >= p.size()) {
                    return fail("patch set past the end at " + where(path));
                }
                p[static_cast<std::size_t>(key.get<std::int64_t>())] = std::move(value);
            } else {
                if (!key.is_string()) return fail("patch set with a number key on an object at " + where(path));
                p[key.get<std::string>()] = std::move(value);
            }
        } else if (kind == "d") {
            if (!p.is_object() || !key.is_string() || !p.contains(key.get<std::string>())) {
                return fail("patch delete of a missing key at " + where(path));
            }
            p.erase(key.get<std::string>());
        } else if (kind == "i") {
            if (!p.is_array() || !key.is_number_integer() || key.get<std::int64_t>() < 0 ||
                static_cast<std::size_t>(key.get<std::int64_t>()) > p.size()) {
                return fail("patch insert outside the array at " + where(path));
            }
            p.insert(p.begin() + key.get<std::int64_t>(), std::move(value));
        } else if (kind == "r") {
            if (!p.is_array() || !key.is_number_integer() || key.get<std::int64_t>() < 0 ||
                static_cast<std::size_t>(key.get<std::int64_t>()) >= p.size()) {
                return fail("patch remove outside the array at " + where(path));
            }
            p.erase(p.begin() + key.get<std::int64_t>());
        } else {
            return fail("unknown patch op '" + kind + "'");
        }
    }
    return root;
}

Result<Json> classicBlockJson(const formats::BsaArchive& blocks, std::size_t index) {
    if (index >= blocks.count()) return fail("no block record " + std::to_string(index));
    const auto& entry = blocks.entry(index);
    if (!entry.name.ends_with(".RDB")) return fail(entry.name + " is not a dungeon block (only RDB blocks are supported)");
    auto block = formats::readClassicRdbBlock(blocks, index);
    if (!block) return std::unexpected(block.error());
    return rdbBlockToJson(*block, entry.name, index, entry.offset);
}

Result<Json> rebuildBlockPatch(const Json& patch, const formats::BsaArchive& blocks) {
    if (!patch.is_object() || patch.value("format", "") != kPatchFormat) return fail("not a " + std::string(kPatchFormat));
    const std::string rebuilds = patch.value("rebuilds", "?");
    const auto& base = patch.value("base", Json::object());
    if (base.value("kind", "") != "block") return fail(rebuilds + ": only block patches are supported");
    const std::string baseName = base.value("block", "");
    const auto baseIndex = base.value("index", static_cast<std::size_t>(-1));
    if (baseIndex >= blocks.count() || blocks.entry(baseIndex).name != baseName) {
        return fail(rebuilds + " wants " + baseName + " at block " + std::to_string(baseIndex) +
                    ", BLOCKS.BSA has " + (baseIndex < blocks.count() ? blocks.entry(baseIndex).name : "nothing"));
    }
    auto start = classicBlockJson(blocks, baseIndex);
    if (!start) return std::unexpected(start.error());

    std::map<std::size_t, Json> classic;   // copy sources, serialised once each
    const CopySource source = [&](const Json& ref) -> Result<Json> {
        const auto index = ref.value("block", static_cast<std::size_t>(-1));
        auto it = classic.find(index);
        if (it == classic.end()) {
            auto json = classicBlockJson(blocks, index);
            if (!json) return std::unexpected(json.error());
            it = classic.emplace(index, std::move(*json)).first;
        }
        const Json* node = &it->second;
        for (const auto& step : ref.value("path", Json::array())) {
            if (node->is_object() && step.is_string() && node->contains(step.get<std::string>())) {
                node = &(*node)[step.get<std::string>()];
            } else if (node->is_array() && step.is_number_integer() && step.get<std::int64_t>() >= 0 &&
                       static_cast<std::size_t>(step.get<std::int64_t>()) < node->size()) {
                node = &(*node)[static_cast<std::size_t>(step.get<std::int64_t>())];
            } else {
                return fail("copy source " + ref.dump() + " not found");
            }
        }
        return *node;
    };
    auto rebuilt = applyPatchOps(std::move(*start), patch.value("ops", Json::array()), source);
    if (!rebuilt) return fail(rebuilds + ": " + rebuilt.error().message);
    return rebuilt;
}

namespace {

// ---- diff (Bunnyfall's diffJson, worldDataJson.js) ----

constexpr std::size_t kMaxEdits = 1500;   // past this many edits an array is taken as one hunk

/// Length as JavaScript counts it (UTF-16 code units): the diff picks the shorter of two scripts by it.
std::size_t jsLength(const Json& j) {
    const auto s = j.dump();
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) == 0x80) continue;   // continuation byte
        n += c >= 0xF0 ? 2 : 1;             // 4-byte sequences are surrogate pairs
    }
    return n;
}

Json withStep(const Json& path, const Json& step) {
    Json p = path;
    p.push_back(step);
    return p;
}

void diffInto(const Json& a, const Json& b, const Json& path, Json& ops);

/// Myers' O((N+M)D) shortest edit script over element identity: matched (indexA, indexB) pairs in order, or nothing
/// past kMaxEdits.
std::optional<std::vector<std::pair<std::size_t, std::size_t>>> lcsPairs(const std::vector<std::string>& ka,
                                                                         const std::vector<std::string>& kb) {
    const long n = static_cast<long>(ka.size()), m = static_cast<long>(kb.size()), max = n + m, off = max + 1;
    std::vector<long> v(static_cast<std::size_t>(2 * max + 3), 0);
    std::vector<std::vector<long>> trace;
    auto at = [&](std::vector<long>& vv, long k) -> long& { return vv[static_cast<std::size_t>(off + k)]; };
    bool found = false;
    for (long d = 0; d <= max && !found; ++d) {
        if (d > static_cast<long>(kMaxEdits)) return std::nullopt;
        trace.push_back(v);
        for (long k = -d; k <= d; k += 2) {
            long x = (k == -d || (k != d && at(v, k - 1) < at(v, k + 1))) ? at(v, k + 1) : at(v, k - 1) + 1;
            long y = x - k;
            while (x < n && y < m && ka[static_cast<std::size_t>(x)] == kb[static_cast<std::size_t>(y)]) { ++x; ++y; }
            at(v, k) = x;
            if (x >= n && y >= m) { found = true; break; }
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    long x = n, y = m;
    for (long d = static_cast<long>(trace.size()) - 1; d >= 0; --d) {
        auto& vv = trace[static_cast<std::size_t>(d)];
        const long k = x - y;
        const long prevK = (k == -d || (k != d && at(vv, k - 1) < at(vv, k + 1))) ? k + 1 : k - 1;
        const long prevX = at(vv, prevK), prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            --x;
            --y;
            pairs.emplace_back(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
        }
        if (d > 0) { x = prevX; y = prevY; }
    }
    std::reverse(pairs.begin(), pairs.end());
    return pairs;
}

void diffArrayLcs(const Json& a, const Json& b, const std::vector<std::string>& ka, const std::vector<std::string>& kb,
                  const Json& path, Json& ops) {
    auto pairs = lcsPairs(ka, kb).value_or(std::vector<std::pair<std::size_t, std::size_t>>{});
    pairs.emplace_back(a.size(), b.size());
    struct Hunk { std::size_t a0, a1, b0, b1; };
    std::vector<Hunk> hunks;
    std::size_t pa = 0, pb = 0;
    for (const auto& [ia, ib] : pairs) {
        if (ia > pa || ib > pb) hunks.push_back({pa, ia, pb, ib});
        pa = ia + 1;
        pb = ib + 1;
    }
    // Right to left, so every index an op names is still the document's.
    for (auto h = hunks.rbegin(); h != hunks.rend(); ++h) {
        const std::size_t k = h->a1 - h->a0, mm = h->b1 - h->b0, paired = std::min(k, mm);
        for (std::size_t t = 0; t < paired; ++t) {
            Json sub = Json::array();
            diffInto(a[h->a0 + t], b[h->b0 + t], withStep(path, h->a0 + t), sub);
            Json whole = Json::array({"s", withStep(path, h->a0 + t), b[h->b0 + t]});
            if (jsLength(sub) < jsLength(whole)) {
                for (auto& op : sub) ops.push_back(std::move(op));
            } else {
                ops.push_back(std::move(whole));
            }
        }
        for (std::size_t t = k; t-- > paired;) ops.push_back(Json::array({"r", withStep(path, h->a0 + t)}));
        for (std::size_t t = paired; t < mm; ++t) ops.push_back(Json::array({"i", withStep(path, h->a0 + t), b[h->b0 + t]}));
    }
}

void diffArray(const Json& a, const Json& b, const Json& path, Json& ops) {
    std::vector<std::string> ka, kb;
    for (const auto& e : a) ka.push_back(canonicalJson(e));
    for (const auto& e : b) kb.push_back(canonicalJson(e));
    if (a.size() == b.size()) {
        Json positional = Json::array();
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (ka[i] != kb[i]) diffInto(a[i], b[i], withStep(path, i), positional);
        }
        Json lcs = Json::array();
        diffArrayLcs(a, b, ka, kb, path, lcs);
        const auto& chosen = jsLength(positional) <= jsLength(lcs) ? positional : lcs;
        for (const auto& op : chosen) ops.push_back(op);
        return;
    }
    diffArrayLcs(a, b, ka, kb, path, ops);
}

void diffInto(const Json& a, const Json& b, const Json& path, Json& ops) {
    if (a.is_object() && b.is_object()) {
        for (const auto& [k, _] : a.items()) {
            if (!b.contains(k)) ops.push_back(Json::array({"d", withStep(path, k)}));
        }
        for (const auto& [k, v] : b.items()) {
            if (!a.contains(k)) ops.push_back(Json::array({"s", withStep(path, k), v}));
            else diffInto(a[k], v, withStep(path, k), ops);
        }
        return;
    }
    if (a.is_array() && b.is_array()) {
        diffArray(a, b, path, ops);
        return;
    }
    if (canonicalJson(a) != canonicalJson(b)) ops.push_back(Json::array({"s", path, b}));
}

} // namespace

Json diffJson(const Json& from, const Json& to) {
    Json ops = Json::array();
    diffInto(from, to, Json::array(), ops);
    return ops;
}

Result<Json> makeBlockPatch(const formats::BsaArchive& blocks, std::size_t baseIndex, const Json& edited,
                            const std::string& rebuilds) {
    auto base = classicBlockJson(blocks, baseIndex);
    if (!base) return std::unexpected(base.error());
    Json patch = {
        {"format", kPatchFormat},
        {"rebuilds", rebuilds},
        {"base", {{"kind", "block"}, {"block", blocks.entry(baseIndex).name}, {"index", baseIndex}}},
        {"sha256", canonicalSha256(edited)},
        {"ops", diffJson(*base, edited)},
    };
    // Prove it: the patch must rebuild the edited block exactly.
    auto rebuilt = rebuildBlockPatch(patch, blocks);
    if (!rebuilt) return fail("the patch does not apply: " + rebuilt.error().message);
    if (canonicalSha256(*rebuilt) != patch["sha256"].get<std::string>()) {
        return fail("the patch does not rebuild the block it was made from");
    }
    return patch;
}

} // namespace de::exporter
