#include "ring_packet_queue.h"

#include <nrf_assert.h>

#include "custom_log.h"

#define PACKET_MEMORY_SIZE_ALIGN(size) ((((size)-1) & 0xFFFFFFFC) + 4)
#define PACKET_MEMORY_SIZE(data_len)                                           \
  PACKET_MEMORY_SIZE_ALIGN(sizeof(ring_packet_queue_packet_t) + (data_len))

#define PACKET_BEGIN(packet) ((uint8_t *)(packet))
#define PACKET_END(packet)                                                     \
  (((uint8_t *)(packet)) + PACKET_MEMORY_SIZE((packet)->len))

#define QUEUE_MEM_BEGIN(queue) ((queue)->mem)
#define QUEUE_MEM_END(queue) ((queue)->mem + (queue)->size)

ring_packet_queue_packet_t *
ring_packet_queue_alloc_packet(ring_packet_queue_t *queue, size_t len) {
  // If the queue is currently empty.
  if (queue->head == NULL) {
    ASSERT(queue->tail == NULL);

    ring_packet_queue_packet_t *packet =
        (ring_packet_queue_packet_t *)queue->mem;
    packet->len = len;
    packet->next = NULL;

    queue->head = packet;
    queue->tail = packet;

    return packet;
  }

  ring_packet_queue_packet_t *packet = NULL;

  // If we have wrap-around.
  if (PACKET_END(queue->tail) <= PACKET_BEGIN(queue->head)) {
    // Try allocating following tail.
    if (PACKET_END(queue->tail) + PACKET_MEMORY_SIZE(len) <=
        PACKET_BEGIN(queue->head)) {
      packet = (ring_packet_queue_packet_t *)PACKET_END(queue->tail);
    }
  } else {
    // Try allocating following tail.
    if (PACKET_END(queue->tail) + PACKET_MEMORY_SIZE(len) <=
        QUEUE_MEM_END(queue)) {
      packet = (ring_packet_queue_packet_t *)PACKET_END(queue->tail);
    }

    // Try allocating at the start.
    if (QUEUE_MEM_BEGIN(queue) + PACKET_MEMORY_SIZE(len) <=
        PACKET_BEGIN(queue->head)) {
      packet = (ring_packet_queue_packet_t *)QUEUE_MEM_BEGIN(queue);
    }
  }

  if (packet != NULL) {
    packet->len = len;
    packet->next = NULL;

    queue->tail->next = packet;
    queue->tail = packet;
  }

  return packet;
}

void ring_packet_queue_free_packet(ring_packet_queue_t *queue,
                                   ring_packet_queue_packet_t *packet) {
  ASSERT(packet == queue->head);

  queue->head = queue->head->next;
  if (queue->head == NULL) {
    queue->tail = NULL;
  }
}

bool ring_packet_queue_is_empty(ring_packet_queue_t *queue) {
  return (queue->head == NULL);
}

ring_packet_queue_packet_t *
ring_packet_queue_get_head(ring_packet_queue_t *queue) {
  return queue->head;
}
