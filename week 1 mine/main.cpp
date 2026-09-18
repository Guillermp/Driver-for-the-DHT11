#include <cstdint>
#include <cassert>
#include <array>
#include <algorithm>
#include <optional>

// Just for development
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

std::optional<std::uint8_t> decode_byte(const std::array<std::uint8_t, 8> durations_us){
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
        frame_idx += 1;
    }

    // Check if the frame is valid
    if (!checksum_valid(frame)) return {{}, DecodeError::checksum_mismatch};

    return {frame, DecodeError::none};

}

bool elapsed_at_least(std::uint32_t now,
                                std::uint32_t start,
                                std::uint32_t interval_us) noexcept {
    return static_cast<std::uint32_t>(now - start) >= interval_us;
}


// Board adapter -- to communicate on the 1-wire

class ComPin {
    public:
        virtual ~ComPin() = default;

        // Pull the wire to ground
        virtual void drive_low() = 0;
        // Relese the wire to high again
        virtual void release() = 0;
        // Read if the wire is high
        virtual bool is_high() const = 0;

};

struct Clock {
    virtual std::uint32_t now_us() const = 0;
};

// Hardware mocked ComPin and Clock

class MocKComPin : public ComPin {
    public:
        void drive_low() override {
            host_pulls_low_ = true;
        }

        void release() override {
            host_pulls_low_ = false;
        }

        bool is_high() const override {
            return !host_pulls_low_ && !sensor_pulls_low_;
        }

        // To control the virtual sensor

        void sensor_drive_low() {
            sensor_pulls_low_ = true;
        }

        void sensor_release() {
            sensor_pulls_low_ = false;
        }


    private:
        bool host_pulls_low_{};
        bool sensor_pulls_low_{};

};

class MockClock : public Clock {
    public:
        std::uint32_t now_us() const override {
            return time_us_;
            }   
        
        void advance_us(std::uint32_t duration) {
            time_us_ += duration;
        }

        void delay_us(std::uint32_t duration) {
            std::uint32_t start = now_us();
            std::uint32_t now = now_us();
            while (!elapsed_at_least(now, start, duration)) {
                advance_us(1);
                now = now_us();

    }

}

    private:
        std::uint32_t time_us_{};


};


bool fake_wait_for_level(const bool level, const std::uint32_t timeout, MockClock & clock, MocKComPin & pin) {

    std::uint32_t start = clock.now_us();
    std::uint32_t now = clock.now_us();
    while (!elapsed_at_least(now, start, timeout)) {
        if (pin.is_high() == level) return true;
        clock.advance_us(1);
        now = clock.now_us();
    } 

    return false;

}



// Communication ---------------------------------------------------------------------------------------

// Startup
bool fake_com_begin(MocKComPin & pin, MockClock & clock) {
    // Send start signal to the sensor
    pin.drive_low();
    clock.delay_us(18000);
    pin.release();

    // Wait for sensor response
    uint32_t timeout = 50; // Typical waiting time 20us-40us
    pin.sensor_drive_low();
    clock.advance_us(20);
    if(!fake_wait_for_level(false, timeout, clock, pin)) return false; // Pin low
    clock.advance_us(80);

    timeout = 90; // typically response of 80us
    pin.sensor_release();
    if(!fake_wait_for_level(true, timeout, clock, pin)) return false; // Pin high
    clock.advance_us(80);

    return true;
}






int main() {
    
    assert(checksum_valid(Frame{60, 0, 23, 0, 83}));
    assert(checksum_valid(Frame{60, 0, 24, 0, 84}));
    assert(decode_byte({25U, 70U, 26U, 0U, 9U, 8U , 9U , 6U})== std::nullopt);

    assert(decode_byte({75U, 76U, 78U, 79U, 75U, 76U , 76U , 76U}).value() == static_cast<std::uint8_t>(0xFF));
    assert(decode_byte({70U, 28U, 27U, 26U, 26U, 26U , 26U , 26U}).value() == static_cast<std::uint8_t>(0x80));
    assert(decode_byte({27U, 26U, 28U, 28U, 27U, 26U , 26U , 76U}).value() == static_cast<std::uint8_t>(0x01));

    // Full frame test: Frame{60, 0, 23, 0, 83}
    const std::array<std::uint8_t, 40> durations_us_frame{
        27, 27, 70, 70, 70, 70, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 70, 27, 70, 70, 70, 27, 27, 27, 27, 27, 27, 27, 27, 27, 70, 27, 70, 27, 27, 70, 70  // 60 = 00111100
    };

    assert((decode_frame(durations_us_frame).frame == Frame{60, 0, 23, 0, 83}));

    const auto result = decode_frame(durations_us_frame);
    assert(result.error == DecodeError::none);
    assert((result.frame == Frame{60, 0, 23, 0, 83}));

    auto invalid_pulse = durations_us_frame;
    invalid_pulse[10] = 45;
    assert(decode_frame(invalid_pulse).error == DecodeError::invalid_pulse);

    auto bad_checksum = durations_us_frame;
    bad_checksum[39] = 27; // Changes checksum byte from 83 to 82.
    assert(decode_frame(bad_checksum).error == DecodeError::checksum_mismatch);


    // Testing the mock hardware 

    MocKComPin pin;

    assert(pin.is_high());             // Both devices released.

    pin.drive_low();
    assert(!pin.is_high());            // MCU holds the line low.

    pin.sensor_drive_low();
    pin.release();
    assert(!pin.is_high());            // Sensor still holds it low.

    pin.sensor_release();
    assert(pin.is_high());             // Both released again.

    MockClock clock;
    const auto start = clock.now_us();
    clock.advance_us(70);

    assert(clock.now_us() - start == 70);
    clock.delay_us(70);
    assert(clock.now_us() - start == 140);



    assert(fake_com_begin(pin, clock));


    pin.release();
    pin.sensor_release();
    uint32_t timeout = 50; // Typical waiting time 20us-40us
    assert(!fake_wait_for_level(false, timeout, clock, pin)); // Timeout happens

    pin.release();
    pin.sensor_drive_low();
    timeout = 50; // Typical waiting time 20us-40us
    assert(fake_wait_for_level(false, timeout, clock, pin)); //Timeout does not happen
    




    return 0;
}