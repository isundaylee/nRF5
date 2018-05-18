#pragma once

#include "nrf_drv_twi.h"

typedef struct {
    nrf_drv_twi_t const* twi;
} lsm9ds1_t;

void lsm9ds1_init(lsm9ds1_t *dev, nrf_drv_twi_t const*twi);

void lsm9ds1_accel_read_all(lsm9ds1_t *dev, int16_t *x, int16_t *y, int16_t *z);
