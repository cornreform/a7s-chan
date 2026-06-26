#ifndef A7S_IR_CONTROL_H
#define A7S_IR_CONTROL_H

#include <cstdint>
#include <driver/rmt_tx.h>

#ifdef __cplusplus
extern "C" {
#endif

// NEC protocol timings (in microseconds)
#define NEC_LEADING_PULSE_US    9000
#define NEC_LEADING_SPACE_US    4500
#define NEC_PULSE_US            560
#define NEC_ZERO_SPACE_US       560
#define NEC_ONE_SPACE_US        1690
#define NEC_REPEAT_SPACE_US     2250
#define NEC_END_TRANSMISSION_US 40000
#define NEC_CARRIER_FREQ_HZ     38000

typedef struct {
    rmt_channel_handle_t tx_chan;
    rmt_encoder_handle_t encoder;
    int gpio_pin;
    bool initialized;
} ir_controller_t;

// Initialize IR transmitter on given GPIO pin using RMT
// Returns 0 on success
int ir_init(ir_controller_t* ir, int gpio_pin);

// Send an NEC infrared frame
// address: 8-bit address
// command: 8-bit command
// repeat_count: number of repeat frames to send after the initial frame (0 = single frame)
// Returns 0 on success
int ir_send_nec(ir_controller_t* ir, uint16_t address, uint16_t command, int repeat_count);

// Send raw IR data with custom timings
int ir_send_raw(ir_controller_t* ir, const uint32_t* durations_us, int num_durations);

// Deinitialize and free RMT resources
void ir_deinit(ir_controller_t* ir);

#ifdef __cplusplus
}
#endif

#endif // A7S_IR_CONTROL_H
