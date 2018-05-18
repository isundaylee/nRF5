// fall detection algorithm
#include "fall_detection.h"

#include <math.h>
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
} fall_detector_t;

fall_detector_t fall_detector;

float t_threshold = 1000;

void fall_detection_init() {}

vector_t fall_detection_get_direction(float yaw, float pitch) {
  return vector_make(cos(yaw) * cos(pitch), sin(yaw) * cos(pitch), sin(pitch));
}

float sq(float b) {
  float z;
  z = b * b;
  return z;
}

bool fall_detection_update(float ax, float ay, float az) {
  static int t = 0;
  static float avg_ax = 0.0, avg_ay = 0.0, avg_az = 0.0;
  static float past_avg_ax[APP_IMU_LOOKBACK_WINDOW],
      past_avg_ay[APP_IMU_LOOKBACK_WINDOW],
      past_avg_az[APP_IMU_LOOKBACK_WINDOW];

  t += 1;
  float acc_value =
      vector_norm(vector_make(ax - avg_ax, ay - avg_ay, az - avg_az));

  avg_ax = 0.95 * avg_ax + 0.05 * ax;
  avg_ay = 0.95 * avg_ay + 0.05 * ay;
  avg_az = 0.95 * avg_az + 0.05 * az;

  int past_avg_index = t % APP_IMU_LOOKBACK_WINDOW;

  switch (fall_detector.state) {
  case FALL_DETECTION_STATE_NORMAL: //
  {
    if (t >= APP_IMU_LOOKBACK_WINDOW && acc_value > 2.0) {
      LOG_READING_FLOAT("acc_value", acc_value);

      fall_detector.state = FALL_DETECTION_STATE_FALLING;
      fall_detector.falling_count = 0;
      fall_detector.falling_direction =
          vector_make(past_avg_ax[past_avg_index], past_avg_ay[past_avg_index],
                      past_avg_az[past_avg_index]);
    }

    break;
  }

  case FALL_DETECTION_STATE_FALLING: //
  {
    if (fall_detector.falling_count >= FALL_DETECTION_FALLING_THRESHOLD) {
      vector_t direction = vector_make(avg_ax, avg_ay, avg_az);
      float angle =
          vector_angle_between(fall_detector.falling_direction, direction);
      float abs_angle_deg = fabs(angle) * 57.295;

      LOG_READING_FLOAT("fx", fall_detector.falling_direction.x);
      LOG_READING_FLOAT("fy", fall_detector.falling_direction.y);
      LOG_READING_FLOAT("fz", fall_detector.falling_direction.z);

      LOG_READING_FLOAT("x", direction.x);
      LOG_READING_FLOAT("y", direction.y);
      LOG_READING_FLOAT("z", direction.z);

      LOG_READING_FLOAT("angle", abs_angle_deg);

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

  past_avg_ax[past_avg_index] = avg_ax;
  past_avg_ay[past_avg_index] = avg_ay;
  past_avg_az[past_avg_index] = avg_az;

  return (fall_detector.state == FALL_DETECTION_STATE_LYING);
}
