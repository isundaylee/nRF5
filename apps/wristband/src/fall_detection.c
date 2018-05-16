// fall detection algorithm
#include <madgwick.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Use the LSM9DS1 class to create an object. [imu] can be
// named anything, we'll refer to that throught the sketch.

// bias parameter of the imu reading
float gx_bias = -0.146;
float gy_bias = -0.34;
float gz_bias = -2.14;
float gx_scale = 1.5;
float gy_scale = 1.5;
float gz_scale = 1.5;
float ax_bias = -0.004;
float ay_bias = 0.08;
float az_bias = -0.99;
float ax_scale = 1;
float ay_scale = 1;
float az_scale = 1;

// static float mx_bias = 0;
// static float my_bias = 0;
// static float mz_bias = 0;
// static float mx_scale = 1.0;
// static float my_scale = 1.0;
// static float mz_scale = 1.0;

float t_threshold = 1000;

madgwick_t madgwick_filter;

void fall_detection_get_orientation(float imu_ax, float imu_ay, float imu_az,
                                    float imu_gx, float imu_gy, float imu_gz,
                                    float *roll, float *pitch, float *yaw) {
  float ax = (imu_ax)*ax_scale + ax_bias;
  float ay = (imu_ay)*ay_scale + ay_bias;
  float az = (imu_az)*az_scale + az_bias;
  float gx = (imu_gx)*gx_scale + gx_bias;
  float gy = (imu_gy)*gy_scale + gy_bias;
  float gz = (imu_gz)*gz_scale + gz_bias;

  madgwick_update_imu(&madgwick_filter, gx, gy, gz, ax, ay, az);

  *roll = madgwick_get_roll(&madgwick_filter);
  *pitch = madgwick_get_pitch(&madgwick_filter);
  *yaw = madgwick_get_yaw(&madgwick_filter);
}

float sq(float b) {
  float z;
  z = b * b;
  return z;
}

bool fall_detection_update(float imu_ax, float imu_ay, float imu_az,
                           float imu_gx, float imu_gy, float imu_gz) {
  float ax = (imu_ax)*ax_scale + ax_bias;
  float ay = (imu_ay)*ay_scale + ay_bias;
  float az = (imu_az)*az_scale + az_bias;
  float gx = (imu_gx)*gx_scale + gx_bias;
  float gy = (imu_gy)*gy_scale + gy_bias;
  float gz = (imu_gz)*gz_scale + gz_bias;

  madgwick_update_imu(&madgwick_filter, gx, gy, gz, ax, ay, az);

  float pitch = madgwick_get_pitch(&madgwick_filter);
  float yaw = madgwick_get_yaw(&madgwick_filter);
  float x_1 = cos(yaw) * cos(pitch);
  float y_1 = sin(yaw) * cos(pitch);
  float z_1 = sin(pitch);
  float acc_value = sqrt(sq(ax) + sq(ay) + sq(az));

  if (acc_value > 2.0) {
    // set a time out
    for (int counter_1 = 1; counter_1 <= t_threshold; counter_1++) {
      for (int counter_2 = 1; counter_2 <= t_threshold; counter_2++) {
      }
    }
    ax = imu_ax * ax_scale + ax_bias;
    ay = (imu_ay)*ay_scale + ay_bias;
    az = (imu_az)*az_scale + az_bias;
    gx = (imu_gx)*gx_scale + gx_bias;
    gy = (imu_gy)*gy_scale + gy_bias;
    gz = (imu_gz)*gz_scale + gz_bias;

    madgwick_update_imu(&madgwick_filter, gx, gy, gz, ax, ay, az);

    pitch = madgwick_get_pitch(&madgwick_filter); // y
    yaw = madgwick_get_yaw(&madgwick_filter);     // z
    // convert orientaion to 3D verctor for current position
    float x_2 = cos(yaw) * cos(pitch);
    float y_2 = sin(yaw) * cos(pitch);
    float z_2 = sin(pitch);
    // compare two vector to get the angle
    float angle = acos((x_1 * x_2 + y_1 * y_2 + z_1 * z_2) /
                       (sqrt(sq(x_1) + sq(y_1) + sq(z_1)) *
                        sqrt(sq(x_2) + sq(y_2) + sq(z_2))));
    float abs_angle = abs(angle) * 57.295;
    if (abs_angle > 60 && abs_angle < 120) {
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

void fall_detection_init() { madgwick_init(&madgwick_filter); }
