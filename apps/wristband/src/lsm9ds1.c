#include "lsm9ds1.h"

#include <math.h>

#include "custom_log.h"

#define LSM9DS1_FULL_SCALE_LIMIT (1 << 15)
#define LSM9DS1_FULL_SCALE_ACCEL_G 4.0
#define LSM9DS1_FULL_SCALE_GYRO_DPS (500.0 * M_PI / 180.0)
#define LSM9DS1_FULL_SCALE_MAG_GS 8.0

#define LSM9DS1_ADDR_GYRO_ACCEL 107
#define LSM9DS1_ADDR_MAG 30

void lsm9ds1_read(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr,
                  uint8_t *data, uint8_t length);
void lsm9ds1_write(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr,
                   uint8_t *data, uint8_t length);
void lsm9ds1_write_byte(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr,
                        uint8_t data);

void lsm9ds1_init(lsm9ds1_t *dev, nrf_drv_twi_t const *twi) {
  dev->twi = twi;

  // Initialize accelerometer
  lsm9ds1_write_byte(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x1F, 0b00111000);
  lsm9ds1_write_byte(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x20, 0b10010000);

  // Initialize gyroscope
  lsm9ds1_write_byte(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x10, 0b10101000);
  lsm9ds1_write_byte(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x1E, 0b00111000);

  // Initialize mag
  lsm9ds1_write_byte(dev, LSM9DS1_ADDR_MAG, 0x21, 0b00100000);
  lsm9ds1_write_byte(dev, LSM9DS1_ADDR_MAG, 0x22, 0b00000000);
}

void lsm9ds1_read(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr,
                  uint8_t *data, uint8_t length) {
  int ret;
  uint8_t tx_data[] = {reg_addr};

  ret = nrf_drv_twi_tx(dev->twi, twi_addr, tx_data, sizeof(tx_data), true);
  APP_ERROR_CHECK(ret);

  ret = nrf_drv_twi_rx(dev->twi, twi_addr, data, length);
  APP_ERROR_CHECK(ret);
}

void lsm9ds1_write(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr,
                   uint8_t *data, uint8_t length) {
  int ret;
  uint8_t tx_data[length + 1];

  tx_data[0] = reg_addr;
  for (int i = 0; i < length; i++) {
    tx_data[i + 1] = data[i];
  }

  ret = nrf_drv_twi_tx(dev->twi, twi_addr, tx_data, sizeof(tx_data), true);
  APP_ERROR_CHECK(ret);
}

void lsm9ds1_write_byte(lsm9ds1_t *dev, uint8_t twi_addr, uint8_t reg_addr,
                        uint8_t data) {
  uint8_t data_arr[1] = {data};
  lsm9ds1_write(dev, twi_addr, reg_addr, data_arr, 1);
}

void lsm9ds1_accel_read_all(lsm9ds1_t *dev, float *x, float *y, float *z) {
  uint8_t data[6];

  lsm9ds1_read(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x28, data, 6);

  *x = LSM9DS1_FULL_SCALE_ACCEL_G * (int16_t)(data[0] + (data[1] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
  *y = LSM9DS1_FULL_SCALE_ACCEL_G * (int16_t)(data[2] + (data[3] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
  *z = LSM9DS1_FULL_SCALE_ACCEL_G * (int16_t)(data[4] + (data[5] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
}

void lsm9ds1_gyro_read_all(lsm9ds1_t *dev, float *x, float *y, float *z) {
  uint8_t data[6];

  lsm9ds1_read(dev, LSM9DS1_ADDR_GYRO_ACCEL, 0x18, data, 6);

  *x = LSM9DS1_FULL_SCALE_GYRO_DPS * (int16_t)(data[0] + (data[1] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
  *y = LSM9DS1_FULL_SCALE_GYRO_DPS * (int16_t)(data[2] + (data[3] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
  *z = LSM9DS1_FULL_SCALE_GYRO_DPS * (int16_t)(data[4] + (data[5] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
}

void lsm9ds1_mag_read_all(lsm9ds1_t *dev, float *x, float *y, float *z) {
  uint8_t data[6];

  lsm9ds1_read(dev, LSM9DS1_ADDR_MAG, 0x28, data, 6);

  *x = LSM9DS1_FULL_SCALE_MAG_GS * (int16_t)(data[0] + (data[1] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
  *y = LSM9DS1_FULL_SCALE_MAG_GS * (int16_t)(data[2] + (data[3] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
  *z = LSM9DS1_FULL_SCALE_MAG_GS * (int16_t)(data[4] + (data[5] << 8)) /
       LSM9DS1_FULL_SCALE_LIMIT;
}
