#pragma once

typedef struct {
  float x;
  float y;
  float z;
} vector_t;

vector_t vector_make(float x, float y, float z);

float vector_dot(vector_t a, vector_t b);
float vector_norm(vector_t a);
float vector_angle_between(vector_t a, vector_t b);
