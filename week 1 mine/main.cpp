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
    if (!checksum_valid(frame)) return {frame, DecodeError::checksum_mismatch};

    return {frame, DecodeError::none};

}

bool elapsed_at_least(std::uint32_t now,
                                std::uint32_t start,
                                std::uint32_t interval_us) noexcept {
    return static_cast<std::uint32_t>(now - start) >= interval_us;
}

struct sensorOutput
{
    double RH;
    double Temperature;
};


sensorOutput frame2data(const Frame & frame){
    double RHdecimal = frame[1]/10.0;
    double Tdecimal = frame[3]/10.0;
    return {frame[0]+RHdecimal, frame[2]+Tdecimal};

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
    virtual ~Clock() = default;
    virtual std::uint32_t now_us() const = 0;
    virtual void delay_us(std::uint32_t duration) const = 0;
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

enum ComStage {
    comBegin,
    comData,

};

class MockClock : public Clock {
    public:
        MockClock(MocKComPin & pin, const ComStage comStage) : pin_(pin), comStage_(comStage){}
        std::uint32_t now_us()const override {
            scheduled_sensor_sequence();
            time_us_++;
            return time_us_;
            }   
        
        void advance_us(std::uint32_t duration) {
            time_us_ += duration;
        }

        void delay_us(std::uint32_t duration) const override {
            std::uint32_t start = now_us();
            std::uint32_t now = start;
            while (!elapsed_at_least(now, start, duration)) {
                now = now_us();

            }

        }

    private:
        mutable std::uint32_t time_us_{};
        MocKComPin & pin_;
        const ComStage comStage_;
        

        void scheduled_sensor_sequence() const { // Scheduled sensor response
            if (comStage_ == ComStage::comBegin) {
                switch (time_us_)
                {
                case 18020:
                    pin_.sensor_drive_low();
                    break;
                case 18100:
                    pin_.sensor_release();
                    break;
                case 18180:
                    pin_.sensor_drive_low();
                    
                default:
                    break;
                }
            }

            if (comStage_ == ComStage::comData) { // Simulation of transferring all ones
                // Inside the comData branch:
                const auto position = time_us_ % 120U;

                switch (position) {
                case 0:
                    pin_.sensor_drive_low();
                    break;

                case 50:
                    pin_.sensor_release();
                    break;

                default:
                    break;
                }
                
            }

        }

};

enum PinLevel {
    low,
    high,
};

bool wait_for_level(const bool level, const std::uint32_t timeout, Clock & clock, ComPin & pin) {

    std::uint32_t start = clock.now_us();
    std::uint32_t now = start;
    while (!elapsed_at_least(now, start, timeout)) {
        if (pin.is_high() == level) return true;
        now = clock.now_us();
    } 

    return false;

}


// Communication ---------------------------------------------------------------------------------------

// Startup
bool com_begin(ComPin & pin, Clock & clock) {
    // Send start signal to the sensor
    pin.drive_low();
    clock.delay_us(18000);
    pin.release();

    // Wait for sensor response
    uint32_t timeout = 50; // Typical waiting time 20us-40us
    if(!wait_for_level(PinLevel::low, timeout, clock, pin)) return false; // Pin low

    timeout = 90; // typically response of 80us
    if(!wait_for_level(PinLevel::high, timeout, clock, pin)) return false; // Pin high

    return wait_for_level(PinLevel::low, timeout, clock, pin);
}


struct PulseDurationResult {
    bool timeout{};
    std::array<std::uint8_t, 40> pulse_durations_us{};
};

PulseDurationResult read_pulse_durations_data(ComPin & pin, Clock & clock) {
    std::array<std::uint8_t, 40> pulse_durations_us{};

    for (unsigned int i{}; i < 40; i++) {
        if(!wait_for_level(PinLevel::high, 80, clock, pin)) return {true, {}}; // Pin high
        std::uint32_t pulseStart = clock.now_us();
        if(!wait_for_level(PinLevel::low, 80, clock, pin)) return {true, {}}; // Pin low
        std::uint32_t pulseEnd = clock.now_us();
        pulse_durations_us[i] = static_cast<std::uint8_t>(pulseEnd - pulseStart);
    }

    return {false, pulse_durations_us};
    
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

    MockClock clock(pin, ComStage::comBegin);
    const auto start = clock.now_us();
    clock.advance_us(70);

    assert(clock.now_us() - start == 71);


    MockClock clock2(pin, ComStage::comBegin);
    assert(com_begin(pin, clock2));


    pin.release();
    pin.sensor_release();
    uint32_t timeout = 50; // Typical waiting time 20us-40us
    assert(!wait_for_level(PinLevel::low, timeout, clock, pin)); // Timeout happens

    pin.release();
    pin.sensor_drive_low();
    timeout = 50; // Typical waiting time 20us-40us
    assert(wait_for_level(PinLevel::low, timeout, clock, pin)); //Timeout does not happen

    MockClock clock3(pin, ComStage::comData);
    PulseDurationResult durationsframe = read_pulse_durations_data(pin, clock3);
    assert(durationsframe.timeout == false);
    DecodeResult result_decode = decode_frame(durationsframe.pulse_durations_us);
    assert(result_decode.error == DecodeError::checksum_mismatch);
    assert((result_decode.frame == Frame{255, 255, 255, 255, 255}));
    


    return 0;
}