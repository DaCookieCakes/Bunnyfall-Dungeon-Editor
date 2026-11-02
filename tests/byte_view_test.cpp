#include "bytes_builder.h"
#include "core/byte_view.h"

#include <doctest/doctest.h>

using de::ByteView;
using de::OutOfBounds;
using de::test::Bytes;

TEST_CASE("ByteView reads little-endian values") {
    Bytes b;
    b.u8(0x7f).u16(0x1234).i32(-5).u32(0xdeadbeef);
    const ByteView v(b.data);
    CHECK(v.u8(0) == 0x7f);
    CHECK(v.u16(1) == 0x1234);
    CHECK(v.i32(3) == -5);
    CHECK(v.u32(7) == 0xdeadbeefu);
}

TEST_CASE("ByteView refuses reads past the end") {
    Bytes b;
    b.u16(1);
    const ByteView v(b.data);
    CHECK_THROWS_AS((void)v.u32(0), OutOfBounds);
    CHECK_THROWS_AS((void)v.u8(2), OutOfBounds);
    CHECK_THROWS_AS((void)v.sub(1, 2), OutOfBounds);
    CHECK_THROWS_AS((void)v.u8(SIZE_MAX), OutOfBounds);
}

TEST_CASE("fixedString stops at NUL or the field width") {
    Bytes b;
    b.text("AB", 5).text("WXYZ", 4);
    const ByteView v(b.data);
    CHECK(v.fixedString(0, 5) == "AB");
    CHECK(v.fixedString(5, 4) == "WXYZ");
    CHECK(v.fixedString(5, 2) == "WX");
}

TEST_CASE("sub views are relative to their start") {
    Bytes b;
    b.u8(1).u8(2).u8(3).u8(4);
    const auto sub = ByteView(b.data).sub(1, 2);
    CHECK(sub.size() == 2);
    CHECK(sub.u8(0) == 2);
    CHECK_THROWS_AS((void)sub.u8(2), OutOfBounds);
}
