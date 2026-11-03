#pragma once

#include <cstdint>

namespace de::formats {

/// Daggerfall's own random generator (the C runtime's LCG). Classic data that "randomises" (dungeon texture tables,
/// random enemy lists) is seeded with it, so the same seed must give the same numbers as the game.
class ClassicRandom {
public:
    explicit ClassicRandom(std::uint32_t seed) : next_(seed) {}

    /// 0..32767
    int next() {
        next_ = next_ * 1103515245u + 12345u;
        return static_cast<int>((next_ >> 16) & 0x7fff);
    }
    /// min..max, both included.
    int rangeInclusive(int min, int max) { return next() % (max - min + 1) + min; }

private:
    std::uint32_t next_;
};

} // namespace de::formats
