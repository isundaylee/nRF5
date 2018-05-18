#include "lsm9ds1.h"

const uint8_t LSM9DS1_ADDR_GYRO_ACCEL = 107;

void lsm9ds1_read(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr, uint8_t *data, uint8_t length);
void lsm9ds1_write(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr, uint8_t *data, uint8_t length);
void lsm9ds1_write_byte(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr, uint8_t data);

void lsm9ds1_init(lsm9ds1_t *dev, nrf_drv_twi_t const* twi) {
    dev->twi = twi;

    // Initialize accelerometer
    lsm9ds1_write_byte(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x1F, 0b00111000);
    lsm9ds1_write_byte(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x20, 0b10000000);
}

void lsm9ds1_read(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr, uint8_t *data, uint8_t length)
{
    int ret;
    uint8_t tx_data[] = {reg_addr};

    ret = nrf_drv_twi_tx(dev->twi, twi_addr, tx_data, sizeof(tx_data), true);
    APP_ERROR_CHECK(ret);

    ret = nrf_drv_twi_rx(dev->twi, twi_addr, data, length);
    APP_ERROR_CHECK(ret);
}

void lsm9ds1_write(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr, uint8_t *data, uint8_t length)
{
    int ret;
    uint8_t tx_data[length + 1];

    tx_data[0] = reg_addr;
    for (int i=0; i<length; i++) {
        tx_data[i + 1] = data[i];
    }

    ret = nrf_drv_twi_tx(dev->twi, twi_addr, tx_data, sizeof(tx_data), true);
    APP_ERROR_CHECK(ret);
}

void lsm9ds1_write_byte(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr, uint8_t data)
{
    uint8_t data_arr[1] = {data};
    lsm9ds1_write(dev, twi_addr, reg_addr, data_arr, 1);
}

void lsm9ds1_accel_read_all(lsm9ds1_t *dev, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t data[6];

    lsm9ds1_read(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x28, data, 6);

    *x = (int16_t) (data[0] + (data[1] << 8));
    *y = (int16_t) (data[2] + (data[3] << 8));
    *z = (int16_t) (data[4] + (data[5] << 8));
}
