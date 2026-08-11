/* ==========================================================================
 * Filename    : app_config.h
 * Author      : Hongyi Mei
 * Date        : 2026-08-10
 * Description : Central compile-time configuration. Every magic number that
 *               describes the board, the motor, or the control loop lives
 *               here so that no literal constants appear in driver code.
 * ========================================================================== */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* --- Clock tree -------------------------------------------------------- */
#define SYSCLK_HZ                 (180000000UL)  /* PLL output, F446 max     */
#define APB1_TIMER_HZ             (90000000UL)   /* CAN1 + TIM2..5 live here */

/* --- Control loop timing ----------------------------------------------- */
#define CONTROL_LOOP_HZ           (1000U)        /* position PID rate        */
#define ENCODER_SAMPLE_HZ         (1000U)        /* AS5600 read rate         */
#define TELEMETRY_HZ              (50U)          /* CAN telemetry frame rate */

/* --- Stepper / mechanics ----------------------------------------------- */
#define MOTOR_FULL_STEPS_PER_REV  (200U)         /* 1.8 deg NEMA-17          */
#define TMC2209_MICROSTEPS        (16U)
#define STEPS_PER_REV             (MOTOR_FULL_STEPS_PER_REV * TMC2209_MICROSTEPS)

/* --- AS5600 magnetic encoder ------------------------------------------- */
#define AS5600_I2C_ADDR_7BIT      (0x36U)
#define AS5600_COUNTS_PER_REV     (4096U)        /* 12-bit absolute          */
#define AS5600_REG_RAW_ANGLE      (0x0CU)
#define AS5600_REG_STATUS         (0x0BU)

/* --- CAN bus ----------------------------------------------------------- */
#define CAN_BITRATE_HZ            (500000UL)
#define CAN_ID_SETPOINT           (0x100U)       /* commander -> motor       */
#define CAN_ID_TELEMETRY          (0x200U)       /* motor -> commander       */
#define CAN_ID_ESTOP              (0x010U)       /* lowest ID = top priority */

/* --- Position PID (retuned in Phase 3) --------------------------------- */
#define PID_KP_Q16                (0x00008000)   /* 0.5 in Q16.16            */
#define PID_KI_Q16                (0x00001000)
#define PID_KD_Q16                (0x00000800)
#define PID_OUTPUT_CLAMP_SPS      (20000)        /* max step rate, steps/s   */

/* --- Safety ------------------------------------------------------------ */
#define ENCODER_TIMEOUT_MS        (50U)          /* stale reading -> fault   */
#define CAN_SETPOINT_TIMEOUT_MS   (200U)         /* lost commander -> hold   */
#define MAX_POSITION_ERROR_STEPS  (400U)         /* stall detect threshold   */

#endif /* APP_CONFIG_H */
