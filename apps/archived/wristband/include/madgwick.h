//=============================================================================================
// MadgwickAHRS.h
//=============================================================================================
//
// Implementation of Madgwick's IMU and AHRS algorithms.
// See: http://www.x-io.co.uk/open-source-imu-and-ahrs-algorithms/
//
// From the x-io website "Open-source resources available on this website are
// provided under the GNU General Public Licence unless an alternative licence
// is provided in source."
//
// Date			Author          Notes
// 29/09/2011	SOH Madgwick    Initial release
// 02/10/2011	SOH Madgwick	Optimised for reduced CPU load
//
//=============================================================================================
#pragma once

#include <math.h>

#include <stdbool.h>

typedef struct {
  float beta;

  float q0;
  float q1;
  float q2;
  float q3;

  float inv_sample_freq;

  float roll;
  float pitch;
  float yaw;

  bool angles_computed;
} madgwick_t;

float inv_sqrt(float x);

void madgwick_init(madgwick_t *mad);
void madgwick_update(madgwick_t *mad, float gx, float gy, float gz, float ax,
                     float ay, float az, float mx, float my, float mz);
void madgwick_update_imu(madgwick_t *mad, float gx, float gy, float gz,
                         float ax, float ay, float az);
void madgwick_compute_angles(madgwick_t *mad);

float madgwick_get_roll(madgwick_t *mad);
float madgwick_get_pitch(madgwick_t *mad);
float madgwick_get_yaw(madgwick_t *mad);
