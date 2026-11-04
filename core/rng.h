#pragma once
// Small deterministic PRNG (splitmix64). No allocation, safe on the audio thread.

#include <cstdint>

namespace tetrad {

inline uint64_t mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

class Rng {
public:
    explicit Rng(uint64_t seed = 1) : state_(seed) {}

    uint64_t next()
    {
        state_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Uniform integer in [0, n).
    uint32_t below(uint32_t n) { return n ? uint32_t(next() % n) : 0; }
    int percent() { return int(below(100)); }

private:
    uint64_t state_;
};

} // namespace tetrad
