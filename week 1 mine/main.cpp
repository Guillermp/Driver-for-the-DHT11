#include <cstdint>
#include <cassert>
#include <array>
#include <algorithm>
#include <optional>
#include <iostream>

using Frame = std::array<std::uint8_t, 5>;

bool checksum_valid(const Frame & frame) {
    std::uint32_t sum{};
    for (unsigned int i{}; i < 4; i++) {
        sum += static_cast<std::uint32_t>(frame[i]);
    }
    return static_cast<std::uint8_t>(sum & 0xFF) == frame[4];


}

// Decoding

std::optional<std::uint8_t> decodeByte(const std::array<std::uint8_t, 8> durations_us){
    // Asumes MSB-first order
    std::uint8_t byte{};
    for (std::uint8_t duration : durations_us) {
        std::uint8_t bit{};
        if (duration >= 60 && duration < 80) bit = 1;
        else if (duration >= 26 && duration <=28) bit = 0;
        else return std::nullopt;
        byte = static_cast<std::uint8_t>((byte << 1U) | bit);
    }

    return byte;

}

enum class DecodeError {
    none,
    invalid_pulse,
    checksum_mismatch
};

struct DecodeResult {
    Frame frame{};
    DecodeError error{DecodeError::none};
};

DecodeResult decodeFrame(const std::array<std::uint8_t, 40> & durations_us){
    // Asumes MSB-first order
    Frame frame{};
    unsigned int frame_idx{};
    for (unsigned int i{}; i<durations_us.size(); i+=8) {
        std::array<std::uint8_t, 8> durations_us_byte{};
        for (unsigned int j{}; j<8; j++) durations_us_byte[j] = durations_us[i+j];
        auto byte = decodeByte(durations_us_byte);
        if (!byte.has_value()) return {{}, DecodeError::invalid_pulse};
        frame[frame_idx] = byte.value();
        frame_idx += 1;
    }

    // Check if the frame is valid
    if (!checksum_valid(frame)) return {{}, DecodeError::checksum_mismatch};

    return {frame, DecodeError::none};

}



int main() {
    
    assert(checksum_valid(Frame{60, 0, 23, 0, 83}));
    assert(checksum_valid(Frame{60, 0, 24, 0, 84}));
    assert(decodeByte({25U, 70U, 26U, 0U, 9U, 8U , 9U , 6U})== std::nullopt);

    assert(decodeByte({75U, 76U, 78U, 79U, 75U, 76U , 76U , 76U}).value() == static_cast<std::uint8_t>(0xFF));
    assert(decodeByte({70U, 28U, 27U, 26U, 26U, 26U , 26U , 26U}).value() == static_cast<std::uint8_t>(0x80));
    assert(decodeByte({27U, 26U, 28U, 28U, 27U, 26U , 26U , 76U}).value() == static_cast<std::uint8_t>(0x01));

    // Full frame test: Frame{60, 0, 23, 0, 83}
    const std::array<std::uint8_t, 40> durations_us_frame{
        27, 27, 70, 70, 70, 70, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 70, 27, 70, 70, 70, 27, 27, 27, 27, 27, 27, 27, 27, 27, 70, 27, 70, 27, 27, 70, 70  // 60 = 00111100
    };

    assert((decodeFrame(durations_us_frame).frame == Frame{60, 0, 23, 0, 83}));

    const auto result = decodeFrame(durations_us_frame);
    assert(result.error == DecodeError::none);
    assert((result.frame == Frame{60, 0, 23, 0, 83}));

    auto invalid_pulse = durations_us_frame;
    invalid_pulse[10] = 45;
    assert(decodeFrame(invalid_pulse).error == DecodeError::invalid_pulse);

    auto bad_checksum = durations_us_frame;
    bad_checksum[39] = 27; // Changes checksum byte from 83 to 82.
    assert(decodeFrame(bad_checksum).error == DecodeError::checksum_mismatch);




    return 0;
}