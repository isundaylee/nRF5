#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <nordic_common.h>

typedef struct __ring_packet_queue_packet ring_packet_queue_packet_t;

struct __ring_packet_queue_packet {
  ring_packet_queue_packet_t *next;
  size_t len;
  uint8_t data[];
};

typedef struct {
  ring_packet_queue_packet_t *head;
  ring_packet_queue_packet_t *tail;

  size_t size;
  uint8_t *mem;
} ring_packet_queue_t;

#define RING_PACKET_QUEUE_DEFINE(name, size)                                   \
  _RING_PACKET_QUEUE_DEFINE(name, size)

#define _RING_PACKET_QUEUE_DEFINE(name, queue_size)                            \
  static uint8_t CONCAT_2(name, _mem)[queue_size];                             \
  static ring_packet_queue_t name = {.head = NULL,                             \
                                     .tail = NULL,                             \
                                     .size = queue_size,                       \
                                     .mem = CONCAT_2(name, _mem)};

ring_packet_queue_packet_t *
ring_packet_queue_alloc_packet(ring_packet_queue_t *queue, size_t len);
void ring_packet_queue_free_packet(ring_packet_queue_t *queue,
                                   ring_packet_queue_packet_t *packet);

bool ring_packet_queue_is_empty(ring_packet_queue_t *queue);
ring_packet_queue_packet_t *
ring_packet_queue_get_head(ring_packet_queue_t *queue);
