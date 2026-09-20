#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace dht11 {
using Frame = std::array<std::uint8_t, 5>;

bool checksum_valid(const Frame & frame);

std::optional<std::uint8_t> decode_byte(const std::array<std::uint8_t, 8> durations_us);

enum class DecodeError {
    none,
    invalid_pulse,
    checksum_mismatch
};

struct DecodeResult {
    Frame frame{};
    DecodeError error{DecodeError::none};
};

DecodeResult decode_frame(const std::array<std::uint8_t, 40> & durations_us);

struct sensorOutput {
    double RH;
    double Temperature;
};

sensorOutput frame2data(const Frame & frame);
}