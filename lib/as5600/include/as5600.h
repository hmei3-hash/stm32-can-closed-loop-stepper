/* ==========================================================================
 * Filename    : as5600.h
 * Author      : Hongyi Mei
 * Date        : 2026-08-10
 * Description : Public interface for the AS5600 12-bit magnetic rotary
 *               encoder. The driver is transport-agnostic: the caller
 *               supplies I2C read/write function pointers so the same code
 *               can run against HAL, register-level, or a unit-test mock.
 * ========================================================================== */

#ifndef AS5600_H
#define AS5600_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool (*i2c_read)(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len);
    bool (*i2c_write)(uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len);
    uint8_t addr;          /* 7-bit device address                         */
    uint16_t zero_offset;  /* raw counts subtracted to define "home"       */
} as5600_t;

typedef enum {
    AS5600_OK = 0,
    AS5600_ERR_BUS,
    AS5600_ERR_NO_MAGNET,
    AS5600_ERR_MAGNET_WEAK,
    AS5600_ERR_MAGNET_STRONG
} as5600_status_t;

as5600_status_t as5600_init(as5600_t *dev);
as5600_status_t as5600_read_raw(as5600_t *dev, uint16_t *out_counts);
as5600_status_t as5600_check_magnet(as5600_t *dev);

#endif /* AS5600_H */
