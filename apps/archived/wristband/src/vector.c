#include "vector.h"

#include <math.h>

vector_t vector_make(float x, float y, float z) {
  vector_t vec;

  vec.x = x;
  vec.y = y;
  vec.z = z;

  return vec;
}

float vector_dot(vector_t a, vector_t b) {
  return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

float vector_norm(vector_t a) {
  return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

float vector_angle_between(vector_t a, vector_t b) {
  return acos(vector_dot(a, b) / (vector_norm(a) * vector_norm(b)));
}
