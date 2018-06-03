/* Copyright (c) 2010 - 2018, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if TIMER_USE_RTC2

#include <stddef.h>

#include "timer.h"

#include "bearer_event.h"
#include "toolchain.h"
#include "nrf_mesh_assert.h"

#include "nrf_soc.h"
#include "nrf.h"

#include "log.h"

#define TIMER_COMPARE_COUNT     (3)

////////////////////////////////////////////////////////////////////////////////
// Static globals
////////////////////////////////////////////////////////////////////////////////

// Bitfield for the attributes given to the timer.
static timer_attr_t     m_attributes[TIMER_COMPARE_COUNT];
// Array of function pointers for callbacks for each timer.
static timer_callback_t m_callbacks[TIMER_COMPARE_COUNT];
// Timestamps set for each timeout.
static timestamp_t      m_timeouts[TIMER_COMPARE_COUNT];

// Timer mutex.
static uint32_t         m_timer_mut;

////////////////////////////////////////////////////////////////////////////////
// Static functions
////////////////////////////////////////////////////////////////////////////////

void RTC2_IRQHandler(void)
{
    for (uint32_t i = 0; i < TIMER_COMPARE_COUNT; ++i)
    {
        if (NRF_RTC2->EVENTS_COMPARE[i] && (NRF_RTC2->INTENSET & (1u << (TIMER_INTENCLR_COMPARE0_Pos + i))))
        {
            timer_callback_t cb = m_callbacks[i];
            NRF_MESH_ASSERT(cb != NULL);

            m_callbacks[i] = NULL;
            NRF_RTC2->INTENCLR = (1u << (TIMER_INTENCLR_COMPARE0_Pos + i));

            timestamp_t time_now = timer_now();
            if (m_attributes[i] & TIMER_ATTR_SYNCHRONOUS)
            {
                cb(time_now);
            }
            else
            {
                NRF_MESH_ASSERT(bearer_event_timer_post(cb, time_now) == NRF_SUCCESS);
            }

            NRF_RTC2->EVENTS_COMPARE[i] = 0;
        }
    }
}

static timestamp_t timer_tick_to_timestamp(uint32_t tick)
{
    return (tick / 33) * 1000;
}

static uint32_t timer_timestamp_to_tick(timestamp_t timestamp)
{
    return (timestamp / 1000) * 33;
}

static void timer_set(uint8_t timer, timestamp_t timeout)
{
    uint32_t ticks = timer_timestamp_to_tick(timeout);

    NRF_RTC2->EVENTS_COMPARE[timer] = 0;
    NRF_RTC2->INTENSET = (1u << (RTC_INTENSET_COMPARE0_Pos + timer));
    NRF_RTC2->CC[timer] = ticks;
}

// Implement mutex lock, the SD-mut is hidden behind SVC, and cannot be used in
// IRQ level <= 1. While the mutex is locked, the timer is unable to receive
// timer interrupts, and the timers may safely be changed.
static inline void timer_mut_lock(void)
{
    _DISABLE_IRQS(m_timer_mut);
}

// Implement mutex unlock, the SD-mut is hidden behind SVC, and cannot be used
// in IRQ level <= 1.
static inline void timer_mut_unlock(void)
{
    _ENABLE_IRQS(m_timer_mut);
}

////////////////////////////////////////////////////////////////////////////////
// Interface functions
////////////////////////////////////////////////////////////////////////////////
void timer_init(void)
{
    NRF_RTC2->PRESCALER = 0;
    NRF_RTC2->TASKS_START = 1;

    NVIC_EnableIRQ(RTC2_IRQn);
}

void timer_event_handler(void)
{
}

uint32_t timer_order_cb(uint8_t timer,
                        timestamp_t time,
                        timer_callback_t callback,
                        timer_attr_t attributes)
{
    if (timer >= TIMER_COMPARE_COUNT)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    if ((attributes & (TIMER_ATTR_SYNCHRONOUS | TIMER_ATTR_TIMESLOT_LOCAL)) != attributes)
    {
        return NRF_ERROR_INVALID_FLAGS;
    }

    timer_mut_lock();

    m_callbacks[timer] = callback;
    m_timeouts[timer] = time;
    m_attributes[timer] = attributes;

    timer_set(timer, time);

    timer_mut_unlock();

    return NRF_SUCCESS;
}

uint32_t timer_order_cb_ppi(uint8_t timer,
                            timestamp_t time,
                            timer_callback_t callback,
                            uint32_t* p_task,
                            timer_attr_t attributes)
{
    // Do not support PPI

    NRF_MESH_ASSERT(false);
    return NRF_ERROR_INTERNAL;
}

uint32_t timer_order_ppi(uint8_t timer, timestamp_t time, uint32_t* p_task, timer_attr_t attributes)
{
    // Do not support PPI

    NRF_MESH_ASSERT(false);
    return NRF_ERROR_INTERNAL;
}

uint32_t timer_abort(uint8_t timer)
{
    if (timer >= TIMER_COMPARE_COUNT)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    if (m_callbacks[timer] == NULL)
    {
        return NRF_ERROR_NOT_FOUND;
    }

    timer_mut_lock();

    m_callbacks[timer] = NULL;
    m_attributes[timer] = TIMER_ATTR_NONE;
    NRF_RTC2->INTENCLR = (1 << (RTC_INTENCLR_COMPARE0_Pos + timer));

    timer_mut_unlock();

    return NRF_SUCCESS;
}

timestamp_t timer_now(void)
{
    timer_mut_lock();
    timestamp_t time = timer_tick_to_timestamp(NRF_RTC2->COUNTER);
    timer_mut_unlock();

    return time;
}

void timer_on_ts_begin(timestamp_t timeslot_start_time)
{
    // Executed in STACK_LOW priority level

    // Although this is a timer implementation based on RTC2, we still need to
    // enable TIMER0 interrupt so that bearer handler processing can be
    // triggered. We enable the interrupt iff we're not locked.

    if (!m_timer_mut)
    {
        (void) NVIC_EnableIRQ(TIMER0_IRQn);
    }
}

void timer_on_ts_end(timestamp_t timeslot_end_time)
{
    // Executed in STACK_LOW priority level

    // Purge all timeslot-local timers
    for (uint32_t i = 0; i < TIMER_COMPARE_COUNT; ++i)
    {
        if (m_attributes[i] & TIMER_ATTR_TIMESLOT_LOCAL)
        {
            m_attributes[i] = TIMER_ATTR_NONE;
            m_callbacks[i] = NULL;

            NRF_RTC2->INTENCLR = (1 << (RTC_INTENCLR_COMPARE0_Pos + i));
        }
    }

    // Kill any pending interrupts, to avoid softdevice triggering
    NRF_TIMER0->INTENCLR = 0xFFFFFFFF;
    (void) NVIC_ClearPendingIRQ(TIMER0_IRQn);
}

#endif // TIMER_USE_RTC2
