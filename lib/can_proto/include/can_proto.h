/* ==========================================================================
 * Filename    : can_proto.h
 * Author      : Hongyi Mei
 * Date        : 2026-08-10
 * Description : Wire format for the two-node CAN link. All multi-byte
 *               fields are little-endian to match the Cortex-M default,
 *               and every struct is asserted to its documented size so a
 *               silent padding change cannot corrupt the bus format.
 * ========================================================================== */

#ifndef CAN_PROTO_H
#define CAN_PROTO_H

#include <stdint.h>

/* Commander -> motor. CAN ID 0x100, DLC 8. */
typedef struct __attribute__((packed)) {
    int32_t  target_steps;   /* absolute position setpoint                */
    uint16_t max_speed_sps;  /* velocity clamp, steps/s                   */
    uint8_t  flags;          /* bit0 = enable, bit1 = rehome              */
    uint8_t  seq;            /* rolls over; used to detect dropped frames */
} can_setpoint_t;

/* Motor -> commander. CAN ID 0x200, DLC 8. */
typedef struct __attribute__((packed)) {
    int32_t  actual_steps;   /* encoder-corrected position                */
    int16_t  error_steps;    /* target - actual                           */
    uint8_t  state;          /* see can_motor_state_t                     */
    uint8_t  seq;
} can_telemetry_t;

typedef enum {
    MOTOR_STATE_IDLE = 0,
    MOTOR_STATE_MOVING,
    MOTOR_STATE_HOLDING,
    MOTOR_STATE_FAULT_STALL,
    MOTOR_STATE_FAULT_ENCODER,
    MOTOR_STATE_FAULT_TIMEOUT
} can_motor_state_t;

_Static_assert(sizeof(can_setpoint_t)  == 8, "setpoint frame must be 8 bytes");
_Static_assert(sizeof(can_telemetry_t) == 8, "telemetry frame must be 8 bytes");

#endif /* CAN_PROTO_H */
