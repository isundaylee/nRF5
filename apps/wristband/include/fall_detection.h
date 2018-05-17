#pragma once

#include <stdbool.h>

float sq(float b);

typedef enum {
  FALL_DETECTION_STATE_NORMAL,
  FALL_DETECTION_STATE_FALLING,
  FALL_DETECTION_STATE_LYING,
} fall_detection_state_t;

void fall_detection_init();

bool fall_detection_update(float imu_ax, float imu_ay, float imu_az,
                           float imu_gx, float imu_gy, float imu_gz,
                           float imu_mx, float imu_my, float imu_mz);
