#pragma once

#include <cstdint>

// xorshift32: fast, deterministic, no locks/allocation — safe to use freely
// on the real-time audio thread. Not cryptographic; purely for scheduling/
// randomization (ported from Jerrican, fully generic).
class FastRandom {
public:
    explicit FastRandom(std::uint32_t seed = 0x9e3779b9u) : state_(seed == 0 ? 1u : seed) {}

    float nextFloat01() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_) / static_cast<float>(UINT32_MAX);
    }

    float nextFloatRange(float low, float high) { return low + nextFloat01() * (high - low); }

private:
    std::uint32_t state_;
};
