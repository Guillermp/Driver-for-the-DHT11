#include "DHT11/decode.h"

namespace dht11 {

bool checksum_valid(const Frame & frame) {
    std::uint32_t sum{};
    for (unsigned int i{}; i < 4; i++) {
        sum += static_cast<std::uint32_t>(frame[i]);
    }
    return static_cast<std::uint8_t>(sum & 0xFF) == frame[4];


}

std::optional<std::uint8_t> decode_byte(const std::array<std::uint8_t, 8> durations_us){
    // Asumes MSB-first order
    std::uint8_t byte{};
    for (std::uint8_t duration : durations_us) {
        std::uint8_t bit{};
        if (duration >= 60 && duration < 80) bit = 1;
        else if (duration >= 23 && duration <=28) bit = 0; // 26-28us
        else return std::nullopt;
        byte = static_cast<std::uint8_t>((byte << 1U) | bit);
    }

    return byte;

}

DecodeResult decode_frame(const std::array<std::uint8_t, 40> & durations_us){
    // Asumes MSB-first order
    Frame frame{};
    unsigned int frame_idx{};
    for (unsigned int i{}; i<durations_us.size(); i+=8) {
        std::array<std::uint8_t, 8> durations_us_byte{};
        for (unsigned int j{}; j<8; j++) durations_us_byte[j] = durations_us[i+j];
        auto byte = decode_byte(durations_us_byte);
        if (!byte.has_value()) return {{}, DecodeError::invalid_pulse};
        frame[frame_idx] = byte.value();

        // Ensure that the decimal part is not larger than 9
        if (frame_idx == 1 || frame_idx == 3) {
            if (frame[frame_idx] > 9) return {{},DecodeError::invalid_pulse};
        }
        frame_idx += 1;
    }

    // Check if the frame is valid
    if (!checksum_valid(frame)) return {frame, DecodeError::checksum_mismatch};

    return {frame, DecodeError::none};

}

sensorOutput frame2data(const Frame & frame){
    double RHdecimal = frame[1]/10.0;
    double Tdecimal = frame[3]/10.0;
    return {frame[0]+RHdecimal, frame[2]+Tdecimal};

}
}