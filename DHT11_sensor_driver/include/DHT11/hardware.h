#pragma once
#include <cstdint>



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


enum ComStage {
    comBegin,
    comData,

};

struct Clock {
    virtual ~Clock() = default;
    virtual std::uint32_t now_us() const = 0;
    virtual void delay_us(std::uint32_t duration) const = 0;
};

