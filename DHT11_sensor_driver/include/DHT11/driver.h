#pragma once
#include <array>
#include <cstdint>
#include "DHT11/hardware.h"
#include "DHT11/decode.h"

// Communication ---------------------------------------------------------------------------------------
namespace dht11 {
bool elapsed_at_least(std::uint32_t now, std::uint32_t start, std::uint32_t interval_us) noexcept;

enum PinLevel {
    low,
    high,
};

bool wait_for_level(const bool level, const std::uint32_t timeout, Clock & clock, ComPin & pin);

// Startup
bool com_begin(ComPin & pin, Clock & clock);

// Data gathering stage

struct PulseDurationResult {
    bool timeout{};
    std::array<std::uint8_t, 40> pulse_durations_us{};
};

PulseDurationResult read_pulse_durations_data(ComPin & pin, Clock & clock);
PulseDurationResult read_pulse_durations_data(); // This one uses interrupts and HW timers



enum class ReadError {
    none,
    handshake_timeout,
    data_timeout,
    invalid_pulse,
    checksum_mismatch
};


struct ReadResult {
    sensorOutput measurement{};
    ReadError error{ReadError::none};
};

ReadError request_sensor_reading(ComPin & pin, Clock & clock);
ReadResult read_sensor_reading();

// Function to get data ------------------------------------------------------
ReadResult get_sensor_reading(ComPin & pin, Clock & clock);

// Releted to the Interrupt capture ------------------------------------------
extern bool completeWrite;
struct Edge {
    std::uint32_t timestamp_us;
    bool high;
};

constexpr std::size_t capacity = 80;
extern Edge edges[capacity];
extern bool capturing;
}

