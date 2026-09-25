#include "DHT11/driver.h"
#include "driver/gptimer.h"
#include <Arduino.h>

// Board adapter -- to communicate on the 1-wire
const int pin_number = 5;


std::size_t edge_count{};



portMUX_TYPE edge_mutex = portMUX_INITIALIZER_UNLOCKED;

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
    public:
    std::uint32_t now_us() const override {
        uint64_t count;
        ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &count));
        return static_cast<std::uint32_t>(count);;
    }

    void delay_us(std::uint32_t duration) const override {
        delayMicroseconds(duration);
    }

    void begin(){
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
        .direction = GPTIMER_COUNT_UP,      // Counting direction is up
        .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
    };
    // Create a timer instance
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    // Enable the timer
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    // Start the timer
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    }
    private:
    gptimer_handle_t gptimer = NULL;

};

ESP32ComPin pin = ESP32ComPin(pin_number);
ESP32Clock sensor_clock;


namespace dht11 {
void ARDUINO_ISR_ATTR onEdge()
{
    const auto now = sensor_clock.now_us();
    const bool high = digitalRead(pin_number) == HIGH;

    portENTER_CRITICAL_ISR(&edge_mutex);

    if (capturing) {
        if (edge_count < capacity) {
            edges[edge_count++] = {now, high};
        } else {
            completeWrite = true;
            capturing = false;
            edge_count=0;
        }
    }

    portEXIT_CRITICAL_ISR(&edge_mutex);
}

void ARDUINO_ISR_ATTR offEdge()
{
    const auto now = sensor_clock.now_us();
    const bool high = digitalRead(pin_number) == HIGH;

    portENTER_CRITICAL_ISR(&edge_mutex);

    if (capturing) {
        if (edge_count < capacity) {
            edges[edge_count++] = {now, low};
        } else {
            completeWrite = true;
            capturing = false;
        }
    }

    portEXIT_CRITICAL_ISR(&edge_mutex);
}
}


void setup() {
    Serial.begin(115200);
    sensor_clock.begin();
    attachInterrupt(digitalPinToInterrupt(pin_number), dht11::onEdge, CHANGE);
    //attachInterrupt(digitalPinToInterrupt(pin_number), dht11::offEdge, FALLING);
    pin.release();
    delay(2000);




    




}

void loop() {

    dht11::request_sensor_reading(pin, sensor_clock);

    //Waiting for the pulses to be recorded
    if (dht11::completeWrite) { 
        //dht11::ReadResult reading = dht11::get_sensor_reading(pin, sensor_clock);
        dht11::ReadResult reading = dht11::read_sensor_reading();
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
    }
    delay(1000);

}