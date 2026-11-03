#pragma once

// Shared access to the real ARENA2 for data tests. Reads DE_ARENA2; tests skip when it is not there.

#include "formats/arena2.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <optional>

namespace de::test {

inline const formats::Arena2* arena2() {
    static const std::optional<formats::Arena2> data = []() -> std::optional<formats::Arena2> {
        const char* dir = std::getenv("DE_ARENA2");
        if (!dir || !std::filesystem::exists(dir)) return std::nullopt;
        auto a = formats::Arena2::open(dir);
        if (!a) FAIL("ARENA2 present but did not open: " << a.error().message);
        return std::move(*a);
    }();
    return data ? &*data : nullptr;
}

} // namespace de::test

#define DE_REQUIRE_ARENA2(var)                                            \
    const ::de::formats::Arena2* var = ::de::test::arena2();              \
    if (!var) {                                                           \
        MESSAGE("skipped: set DE_ARENA2 to an ARENA2 directory");         \
        return;                                                           \
    }
