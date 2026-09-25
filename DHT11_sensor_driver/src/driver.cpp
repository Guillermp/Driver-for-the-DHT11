#include "DHT11/driver.h"
//#include "Arduino.h"


namespace dht11 {

bool completeWrite{};
Edge edges[capacity]{};
bool capturing{};

bool elapsed_at_least(std::uint32_t now,
                                std::uint32_t start,
                                std::uint32_t interval_us) noexcept {
    return static_cast<std::uint32_t>(now - start) >= interval_us;
}

bool wait_for_level(const bool level, const std::uint32_t timeout, Clock & clock, ComPin & pin) {

    std::uint32_t start = clock.now_us();
    std::uint32_t now = start;
    while (!elapsed_at_least(now, start, timeout)) {
        if (pin.is_high() == level) return true;
        now = clock.now_us();
    } 

    return false;

}

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

PulseDurationResult read_pulse_durations_data() {
    if (completeWrite) {
        std::array<std::uint8_t, 40> pulse_durations_us{};
        std::uint8_t duration{};
        unsigned int j{};
        for (unsigned int i{}; i < 80-1; i++) {
            duration = static_cast<std::uint8_t>(edges[i+1].timestamp_us - edges[i].timestamp_us);
            if (!(duration > 45 && duration < 69)) { // Just capture the width of the pulses that encode the data
                
                            pulse_durations_us[j] = static_cast<std::uint8_t>(edges[i+1].timestamp_us - edges[i].timestamp_us);
                            j++;
                
            }
        }

        return {false, pulse_durations_us};

        

    }
    return {true, {}}; // The timeout is meaningless here now (Change it)
}



// Function to get data ------------------------------------------------------
ReadResult get_sensor_reading(ComPin & pin, Clock & clock) {

    if (!com_begin(pin, clock)) {
        return {{}, ReadError::handshake_timeout};
    }

    PulseDurationResult durationsframe = read_pulse_durations_data();

    if (durationsframe.timeout) {
        return {{}, ReadError::data_timeout};
    }

    DecodeResult result_decode = decode_frame(durationsframe.pulse_durations_us);

    /*
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
        */
       
    

    if (result_decode.error != DecodeError::none) {
        if (result_decode.error == DecodeError::checksum_mismatch) {
            return {{}, ReadError::checksum_mismatch};

        } else {
            return {{}, ReadError::invalid_pulse};
        }
    }


    return {frame2data(result_decode.frame), ReadError::none};
}

ReadError request_sensor_reading(ComPin & pin, Clock & clock) {

    if (!com_begin(pin, clock)) {
        return ReadError::handshake_timeout;
    }

    capturing = true;
    return ReadError::none;
}

ReadResult read_sensor_reading() {
    PulseDurationResult durationsframe = read_pulse_durations_data();

    if (durationsframe.timeout) {
        return {{}, ReadError::data_timeout};
    }

    DecodeResult result_decode = decode_frame(durationsframe.pulse_durations_us);

    /*
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
        */
       
    

    if (result_decode.error != DecodeError::none) {
        if (result_decode.error == DecodeError::checksum_mismatch) {
            return {{}, ReadError::checksum_mismatch};

        } else {
            return {{}, ReadError::invalid_pulse};
        }
    }

    completeWrite = false;


    return {frame2data(result_decode.frame), ReadError::none};
}
}