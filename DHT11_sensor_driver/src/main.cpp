#include "DHT11/driver.h"
#include <Arduino.h>

// Board adapter -- to communicate on the 1-wire

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

// ESP32
class ESP32Clock : public Clock {
    std::uint32_t now_us() const override {
        return micros();
    }

    void delay_us(std::uint32_t duration) const override {
        delayMicroseconds(duration);
    }

};





ESP32ComPin pin = ESP32ComPin(5);
ESP32Clock sensor_clock;

void setup() {
    Serial.begin(115200);
    pin.release();
    delay(2000);
    




}

void loop() {
    dht11::ReadResult reading = dht11::get_sensor_reading(pin, sensor_clock);
    if (reading.error == dht11::ReadError::none) {
        Serial.print("RH: ");
        Serial.print(reading.measurement.RH);
        Serial.print(", ");
        Serial.print("T:");
        Serial.println(reading.measurement.Temperature);
    } else {
        // Print the error
        if (reading.error == dht11::ReadError::handshake_timeout) Serial.println("Handshake Timeout - Check the sensor's connection");
        else if (reading.error == dht11::ReadError::checksum_mismatch) Serial.println("Checksum Mismatch");
        else if (reading.error == dht11::ReadError::data_timeout) Serial.println("Data Timeout");
        else if (reading.error == dht11::ReadError::invalid_pulse) Serial.println("Invalid Pulse");

    }
    delay(2000);

}