#include <cstdint>
#include <cassert>
#include <array>
#include <algorithm>
#include <optional>
#include <Arduino.h>

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
        else if (duration >= 23 && duration <=28) bit = 0; // 26-28us
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

struct sensorOutput {
    double RH;
    double Temperature;
};

sensorOutput frame2data(const Frame & frame){
    double RHdecimal = frame[1]/10.0;
    double Tdecimal = frame[3]/10.0;
    return {frame[0]+RHdecimal, frame[2]+Tdecimal};

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

// ESP32 pin
class ESP32ComPin : public ComPin {
    public:
        ESP32ComPin(const int pin) : pin_(pin) {}
        void drive_low() override {
            digitalWrite(pin_, LOW);
            pinMode(pin_, OUTPUT);
        }

        void release() override {
            pinMode(pin_, INPUT);
        }

        bool is_high() const override {
            return digitalRead(pin_) == HIGH;
        }
    private:
        const int pin_;

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

struct Clock {
    virtual ~Clock() = default;
    virtual std::uint32_t now_us() const = 0;
    virtual void delay_us(std::uint32_t duration) const = 0;
};

// ESP32
class ESP32Clock : public Clock {
    std::uint32_t now_us() const override {
        return micros();
    }

    void delay_us(std::uint32_t duration) const override {
        delayMicroseconds(duration);
    }

};

// Mock clock

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
    uint32_t timeout = 60; // Typical waiting time 20us-40us
    if(!wait_for_level(PinLevel::low, timeout, clock, pin)) return false; // Pin low

    timeout = 100; // typically response of 80us
    if(!wait_for_level(PinLevel::high, timeout, clock, pin)) return false; // Pin high

    return wait_for_level(PinLevel::low, timeout, clock, pin);
}

// Data gathering stage

struct PulseDurationResult {
    bool timeout{};
    std::array<std::uint8_t, 40> pulse_durations_us{};
};

PulseDurationResult read_pulse_durations_data(ComPin & pin, Clock & clock) {
    std::array<std::uint8_t, 40> pulse_durations_us{};

    for (unsigned int i{}; i < 40; i++) {
        if(!wait_for_level(PinLevel::high, 90, clock, pin)) return {true, {}}; // Pin high
        std::uint32_t pulseStart = clock.now_us();
        if(!wait_for_level(PinLevel::low, 90, clock, pin)) return {true, {}}; // Pin low
        std::uint32_t pulseEnd = clock.now_us();
        pulse_durations_us[i] = static_cast<std::uint8_t>(pulseEnd - pulseStart);
    }

    return {false, pulse_durations_us};
    
}

// Function to get data ------------------------------------------------------
sensorOutput get_sensor_reading(ComPin & pin, Clock & clock, bool verbose) {

    if (!com_begin(pin, clock)) {
        if (verbose) Serial.println("Handshake timeout");
        return {0, 0};
    }

    PulseDurationResult durationsframe = read_pulse_durations_data(pin, clock);

    if (durationsframe.timeout) {
        if (verbose) Serial.println("Timeout happened reading data");
        return {0,0};
    }

    DecodeResult result_decode = decode_frame(durationsframe.pulse_durations_us);

    /*
    if (verbose) {
    Serial.println("Measured HIGH pulses:");

    for (unsigned int i = 0;
         i < durationsframe.pulse_durations_us.size(); ++i) {
        const auto duration = durationsframe.pulse_durations_us[i];

        const bool accepted =
            (duration >= 23 && duration <= 28) ||
            (duration >= 60 && duration < 80);

        Serial.print(i);
        Serial.print(": ");
        Serial.print(static_cast<unsigned int>(duration));
        Serial.println(accepted ? " us" : " us <- rejected");
    }
       
} */
    

    if (result_decode.error != DecodeError::none) {
        if (result_decode.error == DecodeError::checksum_mismatch) {
            if (verbose) Serial.println("Checksum missmatch");

        } else {
            if (verbose) Serial.println("Invalid pulse detected");
        }
        
        return {0,0};
    }


    return frame2data(result_decode.frame);
}

ESP32ComPin pin = ESP32ComPin(5);
ESP32Clock sensor_clock;

void setup() {
    Serial.begin(115200);
    pin.release();
    delay(2000);
    




}

void loop() {
    sensorOutput reading = get_sensor_reading(pin, sensor_clock, 1);
    Serial.print("RH: ");
    Serial.print(reading.RH);
    Serial.print(", ");
    Serial.print("T:");
    Serial.println(reading.Temperature);
    delay(2000);

}