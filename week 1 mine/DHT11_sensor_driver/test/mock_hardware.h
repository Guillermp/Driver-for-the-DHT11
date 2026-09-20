#pragma once
#include "DHT11/driver.h"
// Hardware mocked ComPin and Clock

namespace dht11 {
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
        mutable unsigned int n_bits_sent_in_byte{1};
        mutable unsigned int n_bytes{1};
        mutable std::uint32_t bit_start_us_{};
        

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
                const std::uint32_t elapsed = time_us_ - bit_start_us_;

                // After 40 bits, hold low for 50 us, then release.
                if (n_bytes > 5) {
                    if (elapsed >= 50U) {
                        pin_.sensor_release();
                    }
                    return;
                }

                const bool decimal_byte = (n_bytes == 2 || n_bytes == 4);
                const std::uint32_t high_duration = decimal_byte ? 27U : 70U; // For the decimal bytes I choose a zero bit

                // Write the bit
                if (elapsed == 0U) {
                    pin_.sensor_drive_low();
                } else if (elapsed == 50U) {
                    pin_.sensor_release();
                } else if (elapsed == 50U + high_duration) {
                    // Finish this bit and begin the next bit's low phase.
                    pin_.sensor_drive_low();
                    bit_start_us_ = time_us_;

                    ++n_bits_sent_in_byte;
                    if (n_bits_sent_in_byte > 8U) {
                        n_bits_sent_in_byte = 1;
                        ++n_bytes;
                    }
                }

            }

        }

};
}