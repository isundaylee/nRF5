// fall detection algorithm
#include "fall_detection.h"

#include <madgwick.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_config.h"
#include "custom_log.h"
#include "vector.h"

// Use the LSM9DS1 class to create an object. [imu] can be
// named anything, we'll refer to that throught the sketch.

// bias parameter of the imu reading
#define FALL_DETECTION_GX_SCALE 1
#define FALL_DETECTION_GY_SCALE 1
#define FALL_DETECTION_GZ_SCALE 1
#define FALL_DETECTION_AX_SCALE 1
#define FALL_DETECTION_AY_SCALE 1
#define FALL_DETECTION_AZ_SCALE 1

#define FALL_DETECTION_FALLING_THRESHOLD (1.0 * APP_IMU_FREQUENCY)

typedef struct {
  fall_detection_state_t state;

  int falling_count;

  vector_t falling_direction;

  madgwick_t madgwick_filter;
} fall_detector_t;

fall_detector_t fall_detector;

float t_threshold = 1000;

void fall_detection_init() { madgwick_init(&fall_detector.madgwick_filter); }

vector_t fall_detection_get_direction(float yaw, float pitch) {
  return vector_make(cos(yaw) * cos(pitch), sin(yaw) * cos(pitch), sin(pitch));
}

float sq(float b) {
  float z;
  z = b * b;
  return z;
}

bool fall_detection_update(float imu_ax, float imu_ay, float imu_az,
                           float imu_gx, float imu_gy, float imu_gz,
                           float imu_mx, float imu_my, float imu_mz) {
  float ax = (imu_ax)*FALL_DETECTION_AX_SCALE;
  float ay = (imu_ay)*FALL_DETECTION_AY_SCALE;
  float az = (imu_az)*FALL_DETECTION_AZ_SCALE;
  float gx = (imu_gx)*FALL_DETECTION_GX_SCALE;
  float gy = (imu_gy)*FALL_DETECTION_GY_SCALE;
  float gz = (imu_gz)*FALL_DETECTION_GZ_SCALE;

  madgwick_update(&fall_detector.madgwick_filter, gx, gy, gz, ax, ay, az,
                  imu_mx, imu_my, imu_mz);

  float acc_value = sqrt(sq(ax) + sq(ay) + sq(az));
  // LOG_INFO("READING acc_value " NRF_LOG_FLOAT_MARKER,
  // NRF_LOG_FLOAT(acc_value));

  static vector_t initial_direction;

  static int t = 0;
  static bool inited = false;
  t += 1;
  if (inited) {
    float yaw = madgwick_get_yaw(&fall_detector.madgwick_filter);
    float pitch = madgwick_get_pitch(&fall_detector.madgwick_filter);
    vector_t direction = fall_detection_get_direction(yaw, pitch);
    float angle = vector_angle_between(initial_direction, direction);
    if (t % 10 == 0) {
      LOG_INFO("READING angle " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(angle));

      LOG_READING_FLOAT("yaw", yaw);
      LOG_READING_FLOAT("pitch", pitch);
    }
  } else {
    float yaw = madgwick_get_yaw(&fall_detector.madgwick_filter);
    float pitch = madgwick_get_pitch(&fall_detector.madgwick_filter);
    initial_direction = fall_detection_get_direction(yaw, pitch);
    inited = true;
  }

  switch (fall_detector.state) {
  case FALL_DETECTION_STATE_NORMAL: //
  {
    if (acc_value > 2.0) {
      fall_detector.state = FALL_DETECTION_STATE_FALLING;
      fall_detector.falling_count = 0;

      float yaw = madgwick_get_yaw(&fall_detector.madgwick_filter);
      float pitch = madgwick_get_pitch(&fall_detector.madgwick_filter);
      fall_detector.falling_direction =
          fall_detection_get_direction(yaw, pitch);
    }

    break;
  }

  case FALL_DETECTION_STATE_FALLING: //
  {
    if (fall_detector.falling_count >= FALL_DETECTION_FALLING_THRESHOLD) {
      ax = imu_ax * FALL_DETECTION_AX_SCALE;
      ay = (imu_ay)*FALL_DETECTION_AY_SCALE;
      az = (imu_az)*FALL_DETECTION_AZ_SCALE;
      gx = (imu_gx)*FALL_DETECTION_GX_SCALE;
      gy = (imu_gy)*FALL_DETECTION_GY_SCALE;
      gz = (imu_gz)*FALL_DETECTION_GZ_SCALE;

      float pitch = madgwick_get_pitch(&fall_detector.madgwick_filter); // y
      float yaw = madgwick_get_yaw(&fall_detector.madgwick_filter);     // z

      vector_t direction = fall_detection_get_direction(yaw, pitch);
      float angle =
          vector_angle_between(fall_detector.falling_direction, direction);
      float abs_angle_deg = fabs(angle) * 57.295;

      LOG_INFO("abs_angle_deg: " NRF_LOG_FLOAT_MARKER,
               NRF_LOG_FLOAT(abs_angle_deg));

      if (abs_angle_deg > 60 && abs_angle_deg < 120) {
        fall_detector.state = FALL_DETECTION_STATE_LYING;
      } else {
        fall_detector.state = FALL_DETECTION_STATE_NORMAL;
      }
    } else {
      fall_detector.falling_count++;
    }

    break;
  }

  case FALL_DETECTION_STATE_LYING: //
  {
    break;
  }
  }

  return (fall_detector.state == FALL_DETECTION_STATE_LYING);
}
