#include <unity.h>
#include "DHT11/decode.h"
#include "DHT11/driver.h"
#include "mock_hardware.h"

using namespace dht11;

void setUp() {}
void tearDown() {}

namespace {
const std::array<std::uint8_t, 40> valid_pulses{
    27, 27, 70, 70, 70, 70, 27, 27, // 60
    27, 27, 27, 27, 27, 27, 27, 27, // 0
    27, 27, 27, 70, 27, 70, 70, 70, // 23
    27, 27, 27, 27, 27, 27, 27, 27, // 0
    27, 70, 27, 70, 27, 27, 70, 70  // 83
};

void test_valid_checksums() {
    TEST_ASSERT_TRUE(checksum_valid(Frame{60, 0, 23, 0, 83}));
    TEST_ASSERT_TRUE(checksum_valid(Frame{60, 0, 24, 0, 84}));
}
void test_corrupt_and_overflow_checksums() {
    TEST_ASSERT_FALSE(checksum_valid(Frame{60, 0, 23, 0, 1}));
    TEST_ASSERT_TRUE(checksum_valid(Frame{240, 0, 240, 0, 224}));
    TEST_ASSERT_TRUE(checksum_valid(Frame{}));
}
void check_byte(const std::array<std::uint8_t, 8>& pulses, std::uint8_t expected) {
    const auto decoded = decode_byte(pulses);
    TEST_ASSERT_TRUE(decoded.has_value());
    TEST_ASSERT_EQUAL_UINT8(expected, decoded.value());
}
void test_byte_zero() {
    check_byte({27, 27, 27, 27, 27, 27, 27, 27}, 0x00);
}
void test_byte_all_ones() {
    check_byte({75, 76, 78, 79, 75, 76, 76, 76}, 0xFF);
}
void test_byte_msb_first() {
    check_byte({70, 28, 27, 26, 26, 26, 26, 26}, 0x80);
    check_byte({27, 26, 28, 28, 27, 26, 26, 76}, 0x01);
}
void test_invalid_byte_pulses() {
    TEST_ASSERT_FALSE(decode_byte({25, 70, 26, 0, 9, 8, 9, 6}).has_value());
}
void test_full_frame() {
    const auto result = decode_frame(valid_pulses);
    const Frame expected{60, 0, 23, 0, 83};
    TEST_ASSERT_TRUE(result.error == DecodeError::none);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), result.frame.data(), expected.size());
}
void test_frame_invalid_pulse() {
    auto pulses = valid_pulses;
    pulses[10] = 45;
    TEST_ASSERT_TRUE(decode_frame(pulses).error == DecodeError::invalid_pulse);
}
void test_frame_bad_checksum_retains_bytes() {
    auto pulses = valid_pulses;
    pulses[39] = 27;
    const auto result = decode_frame(pulses);
    const Frame expected{60, 0, 23, 0, 82};
    TEST_ASSERT_TRUE(result.error == DecodeError::checksum_mismatch);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), result.frame.data(), expected.size());
}
void test_measurement_conversion() {
    const auto a = frame2data(Frame{255, 9, 255, 9, 0});
    TEST_ASSERT_DOUBLE_WITHIN(0.000001, 255.9, a.RH);
    TEST_ASSERT_DOUBLE_WITHIN(0.000001, 255.9, a.Temperature);
    TEST_ASSERT_DOUBLE_WITHIN(0.000001, 255.8, frame2data(Frame{255, 8, 255, 9, 0}).RH);
    TEST_ASSERT_DOUBLE_WITHIN(0.000001, 5.9, frame2data(Frame{5, 9, 5, 9, 0}).Temperature);
    TEST_ASSERT_DOUBLE_WITHIN(0.000001, 29.3, frame2data(Frame{29, 3, 4, 1, 0}).RH);
    TEST_ASSERT_DOUBLE_WITHIN(0.000001, 155.2, frame2data(Frame{15, 7, 155, 2, 0}).Temperature);
}
void test_mock_pin_shared_line() {
    MocKComPin pin;
    TEST_ASSERT_TRUE(pin.is_high());
    pin.drive_low();
    TEST_ASSERT_FALSE(pin.is_high());
    pin.sensor_drive_low();
    pin.release();
    TEST_ASSERT_FALSE(pin.is_high());
    pin.sensor_release();
    TEST_ASSERT_TRUE(pin.is_high());
}
void test_mock_clock_advances() {
    MocKComPin pin;
    MockClock clock(pin, ComStage::comBegin);
    const auto start = clock.now_us();
    clock.advance_us(70);
    TEST_ASSERT_EQUAL_UINT32(71, clock.now_us() - start);
}
void test_handshake() {
    MocKComPin pin;
    MockClock clock(pin, ComStage::comBegin);
    TEST_ASSERT_TRUE(com_begin(pin, clock));
}
void test_wait_timeout() {
    MocKComPin pin;
    MockClock clock(pin, ComStage::comBegin);
    TEST_ASSERT_FALSE(wait_for_level(PinLevel::low, 50, clock, pin));
}
void test_wait_already_at_level() {
    MocKComPin pin;
    pin.sensor_drive_low();
    MockClock clock(pin, ComStage::comBegin);
    TEST_ASSERT_TRUE(wait_for_level(PinLevel::low, 50, clock, pin));
}
void test_capture_simulated_decimal_zero_frame() {
    MocKComPin pin;
    pin.sensor_drive_low();
    MockClock clock(pin, ComStage::comData);
    const auto capture = read_pulse_durations_data(pin, clock);
    TEST_ASSERT_FALSE(capture.timeout);
    const auto decoded = decode_frame(capture.pulse_durations_us);
    const Frame expected{255, 0, 255, 0, 255};
    TEST_ASSERT_TRUE(decoded.error == DecodeError::checksum_mismatch);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), decoded.frame.data(), expected.size());
}
void test_capture_stuck_low_times_out() {
    MocKComPin pin;
    pin.sensor_drive_low();
    MockClock clock(pin, ComStage::comBegin); // No scheduled edge until 18020 us.
    TEST_ASSERT_TRUE(read_pulse_durations_data(pin, clock).timeout);
}
void test_capture_stuck_high_times_out() {
    MocKComPin pin;
    MockClock clock(pin, ComStage::comBegin);
    TEST_ASSERT_TRUE(read_pulse_durations_data(pin, clock).timeout);
}
void test_elapsed_across_clock_wrap() {
    TEST_ASSERT_FALSE(elapsed_at_least(3U, UINT32_MAX - 5U, 10U));
    TEST_ASSERT_TRUE(elapsed_at_least(4U, UINT32_MAX - 5U, 10U));
}
} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_checksums);
    RUN_TEST(test_corrupt_and_overflow_checksums);
    RUN_TEST(test_byte_zero);
    RUN_TEST(test_byte_all_ones);
    RUN_TEST(test_byte_msb_first);
    RUN_TEST(test_invalid_byte_pulses);
    RUN_TEST(test_full_frame);
    RUN_TEST(test_frame_invalid_pulse);
    RUN_TEST(test_frame_bad_checksum_retains_bytes);
    RUN_TEST(test_measurement_conversion);
    RUN_TEST(test_mock_pin_shared_line);
    RUN_TEST(test_mock_clock_advances);
    RUN_TEST(test_handshake);
    RUN_TEST(test_wait_timeout);
    RUN_TEST(test_wait_already_at_level);
    RUN_TEST(test_capture_simulated_decimal_zero_frame);
    RUN_TEST(test_capture_stuck_low_times_out);
    RUN_TEST(test_capture_stuck_high_times_out);
    RUN_TEST(test_elapsed_across_clock_wrap);
    return UNITY_END();
}
