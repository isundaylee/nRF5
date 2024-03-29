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

#include "health_messages.h"
#include "health_opcodes.h"
#include "health_server.h"

#include "nrf_mesh_config_app.h"
#include "nrf_mesh.h"

#include "bearer_event.h"
#include "nrf_mesh_assert.h"
#include "timer_scheduler.h"

#include "access_config.h"
#include "utils.h"

#define ATTENTION_TIMER_INTERVAL 1000000u
#define FAST_PERIOD_DIVISOR_MAX  15

/* Attention timer context: */
static timer_event_t     m_attention_timer;
/* List of health servers with a non-zero attention timer: */
static health_server_t * mp_attention_list = NULL;

static void send_fault_status(health_server_t * p_server, uint16_t opcode, const access_message_rx_t * p_message);

static void health_publish_timeout_handler(access_model_handle_t handle, void * p_args)
{
    health_server_t * p_server = p_args;
    send_fault_status(p_server, HEALTH_OPCODE_CURRENT_STATUS, NULL);

    if (p_server->publish_timeout_handler != NULL) {
        p_server->publish_timeout_handler(p_server);
    }
}

static void attention_timer_handler(timestamp_t timestamp, void * p_context)
{
    health_server_t * p_previous = NULL;
    health_server_t * p_current = mp_attention_list;

    while (p_current != NULL)
    {
        if (p_current->attention_timer > 0)
        {
            p_current->attention_timer--;
        }

        /* Remove the timer from the list and call the callback function if the timer reaches 0: */
        if (p_current->attention_timer == 0)
        {
            if (p_current == mp_attention_list)
            {
                mp_attention_list = p_current->p_next;
            }
            else
            {
                NRF_MESH_ASSERT(p_previous != NULL);
                p_previous->p_next = p_current->p_next;
            }

            p_current->attention_handler(p_current, false);
        }

        p_previous = p_current;
        p_current = p_current->p_next;
    }

    if (mp_attention_list == NULL)
    {
        timer_sch_abort(&m_attention_timer);
    }
}

static void attention_timer_add(health_server_t * p_server)
{
    bearer_event_critical_section_begin();
    NRF_MESH_ASSERT(p_server->attention_handler != NULL);
    NRF_MESH_ASSERT(p_server->attention_timer > 0);

    bool timer_running = mp_attention_list != NULL;

    p_server->p_next = mp_attention_list;
    mp_attention_list = p_server;

    if (!timer_running)
    {
        memset(&m_attention_timer, 0, sizeof(m_attention_timer));
        m_attention_timer.timestamp = timer_now() + ATTENTION_TIMER_INTERVAL;
        m_attention_timer.interval = ATTENTION_TIMER_INTERVAL;
        m_attention_timer.cb = attention_timer_handler;
        timer_sch_schedule(&m_attention_timer);
    }
    bearer_event_critical_section_end();
}

static void attention_timer_set(health_server_t * p_server, uint8_t new_attention_value)
{
    /* Only change the attention timer if it is supported: */
    if (p_server->attention_handler != NULL)
    {
        bearer_event_critical_section_begin();
        bool timer_scheduled = p_server->attention_timer > 0;

        p_server->attention_timer = new_attention_value;
        if (!timer_scheduled && new_attention_value > 0)
        {
            p_server->attention_handler(p_server, true);
            attention_timer_add(p_server);
        }
        bearer_event_critical_section_end();
    }
}

/* Sends the current fault status; if p_message is not NULL, the message is sent as a reply to that message. */
static void send_fault_status(health_server_t * p_server, uint16_t opcode, const access_message_rx_t * p_message)
{
    health_server_fault_array_t * p_fault_array;
    if (opcode == HEALTH_OPCODE_CURRENT_STATUS)
    {
        p_fault_array = &p_server->current_faults; /*lint !e545 Suspicious use of '&' */
    }
    else
    {
        p_fault_array = &p_server->registered_faults; /*lint !e545 Suspicious use of '&' */
    }

    uint8_t message_buffer[sizeof(health_msg_fault_status_t) + HEALTH_SERVER_FAULT_ARRAY_SIZE];
    health_msg_fault_status_t * p_pdu = (health_msg_fault_status_t *) message_buffer;

    p_pdu->test_id = p_server->previous_test_id;
    p_pdu->company_id = p_server->company_id;

    /* Create the list of faults from the fault array bitfield: */
    uint8_t current_index = 0;
    for (uint32_t i = bitfield_next_get(*p_fault_array, HEALTH_SERVER_FAULT_ARRAY_SIZE, 0); i != HEALTH_SERVER_FAULT_ARRAY_SIZE;
            i = bitfield_next_get(*p_fault_array, HEALTH_SERVER_FAULT_ARRAY_SIZE, i + 1))
    {
        p_pdu->fault_array[current_index++] = i;
    }

    const access_message_tx_t packet =
    {
        .opcode = ACCESS_OPCODE_SIG(opcode),
        .p_buffer = message_buffer,
        .length = ((uintptr_t) &p_pdu->fault_array[current_index] - (uintptr_t) &message_buffer[0]),
        .force_segmented = false,
        .transmic_size = NRF_MESH_TRANSMIC_SIZE_DEFAULT,
        .access_token = nrf_mesh_unique_token_get()
    }; /*lint !e446: side effect in initializer */

    if (p_message != NULL)
    {
        (void) access_model_reply(p_server->model_handle, p_message, &packet);
    }
    else
    {
        (void) access_model_publish(p_server->model_handle, &packet);
    }
}

static void send_attention_status(const health_server_t * p_server, const access_message_rx_t * p_message)
{
    const health_msg_attention_status_t reply_message = { .attention = p_server->attention_timer };
    const access_message_tx_t packet =
    {
        .opcode = ACCESS_OPCODE_SIG(HEALTH_OPCODE_ATTENTION_STATUS),
        .p_buffer = (const uint8_t *) &reply_message,
        .length = sizeof(health_msg_attention_status_t),
        .force_segmented = false,
        .transmic_size = NRF_MESH_TRANSMIC_SIZE_DEFAULT,
        .access_token = nrf_mesh_unique_token_get()
    }; /*lint !e446: side effect in initializer */

    (void) access_model_reply(p_server->model_handle, p_message, &packet);
}

static void send_period_status(const health_server_t * p_server, const access_message_rx_t * p_message)
{
    const health_msg_period_status_t reply_message = { .fast_period_divisor = p_server->fast_period_divisor };
    const access_message_tx_t packet =
    {
        .opcode = ACCESS_OPCODE_SIG(HEALTH_OPCODE_PERIOD_STATUS),
        .p_buffer = (const uint8_t *) &reply_message,
        .length = sizeof(health_msg_period_status_t),
        .force_segmented = false,
        .transmic_size = NRF_MESH_TRANSMIC_SIZE_DEFAULT,
        .access_token = nrf_mesh_unique_token_get()
    }; /*lint !e446: side effect in initializer */

    (void) access_model_reply(p_server->model_handle, p_message, &packet);
}

/********** Opcode handlers **********/

static void handle_fault_get(access_model_handle_t handle, const access_message_rx_t * p_message, void * p_args)
{
    if (p_message->length != sizeof(health_msg_fault_get_t))
    {
        return;
    }

    health_server_t * p_server = (health_server_t *) p_args;
    NRF_MESH_ASSERT(p_server->model_handle == handle);

    const health_msg_fault_get_t * p_pdu = (const health_msg_fault_get_t *) p_message->p_data;
    if (p_pdu->company_id != p_server->company_id)
    {
        return;
    }

    send_fault_status(p_server, HEALTH_OPCODE_FAULT_STATUS, p_message);
}

static void handle_fault_clear(access_model_handle_t handle, const access_message_rx_t * p_message, void * p_args)
{
    if (p_message->length != sizeof(health_msg_fault_clear_t))
    {
        return;
    }

    health_server_t * p_server = (health_server_t *) p_args;
    NRF_MESH_ASSERT(p_server->model_handle == handle);

    const health_msg_fault_clear_t * p_pdu = (const health_msg_fault_clear_t *) p_message->p_data;
    if (p_pdu->company_id != p_server->company_id)
    {
        return;
    }

    bitfield_clear_all(p_server->registered_faults, HEALTH_SERVER_FAULT_ARRAY_SIZE);
    if (p_message->opcode.opcode == HEALTH_OPCODE_FAULT_CLEAR)
    {
        send_fault_status(p_server, HEALTH_OPCODE_FAULT_STATUS, p_message);
    }
}

static void handle_fault_test(access_model_handle_t handle, const access_message_rx_t * p_message, void * p_args)
{
    if (p_message->length != sizeof(health_msg_fault_test_t))
    {
        return;
    }

    health_server_t * p_server = (health_server_t *) p_args;
    NRF_MESH_ASSERT(p_server->model_handle == handle);

    const health_msg_fault_test_t * p_pdu = (const health_msg_fault_test_t *) p_message->p_data;
    if (p_pdu->company_id != p_server->company_id)
    {
        return;
    }

    /* Find and run the specified self-test, ignoring invalid test IDs: */
    bool test_found = false;
    for (uint8_t i = 0; i < p_server->num_selftests; ++i)
    {
        if (p_server->p_selftests[i].test_id == p_pdu->test_id)
        {
            p_server->p_selftests[i].selftest_function(p_server, p_server->company_id, p_pdu->test_id);
            p_server->previous_test_id = p_pdu->test_id;
            test_found = true;
            break;
        }
    }

    /* Respond if the opcode of the incoming message matches the acknowledged Fault Test message: */
    if (test_found && p_message->opcode.opcode == HEALTH_OPCODE_FAULT_TEST)
    {
        send_fault_status(p_server, HEALTH_OPCODE_FAULT_STATUS, p_message);
    }
}

static void handle_period_get(access_model_handle_t handle, const access_message_rx_t * p_message, void * p_args)
{
    if (p_message->length == 0)
    {
        send_period_status((health_server_t *) p_args, p_message);
    }
}

static void handle_period_set(access_model_handle_t handle, const access_message_rx_t * p_message, void * p_args)
{
    if (p_message->length != sizeof(health_msg_period_set_t))
    {
        return;
    }

    const health_msg_period_set_t * p_pdu = (const health_msg_period_set_t *) p_message->p_data;
    health_server_t * p_server = p_args;

    if (p_pdu->fast_period_divisor < FAST_PERIOD_DIVISOR_MAX)
    {
        p_server->fast_period_divisor = p_pdu->fast_period_divisor;
    }

    /* Respond if the opcode of the incoming message matches the acknowledged Period Set message: */
    if (p_message->opcode.opcode == HEALTH_OPCODE_PERIOD_SET)
    {
        send_period_status(p_server, p_message);
    }
}

static void handle_attention_set(access_model_handle_t handle, const access_message_rx_t * p_message, void * p_args)
{
    if (p_message->length != sizeof(health_msg_attention_set_t))
    {
        return;
    }

    health_server_t * p_server = p_args;
    const health_msg_attention_set_t * p_pdu = (const health_msg_attention_set_t *) p_message->p_data;

    attention_timer_set(p_server, p_pdu->attention);

    /* Respond if the opcode of the incoming message is an Attention Set (unacknowledged) */
    if (p_message->opcode.opcode == HEALTH_OPCODE_ATTENTION_SET)
    {
        send_attention_status((health_server_t *) p_args, p_message);
    }
}

static void handle_attention_get(access_model_handle_t handle, const access_message_rx_t * p_message, void * p_args)
{
    if (p_message->length == 0)
    {
        send_attention_status((health_server_t *) p_args, p_message);
    }
}

static const access_opcode_handler_t m_opcode_handlers[] =
{
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_FAULT_GET),             handle_fault_get },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_FAULT_CLEAR),           handle_fault_clear },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_FAULT_CLEAR_UNACKED),   handle_fault_clear },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_FAULT_TEST),            handle_fault_test },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_FAULT_TEST_UNACKED),    handle_fault_test },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_PERIOD_GET),            handle_period_get },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_PERIOD_SET),            handle_period_set },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_PERIOD_SET_UNACKED),    handle_period_set },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_ATTENTION_GET),         handle_attention_get },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_ATTENTION_SET),         handle_attention_set },
    { ACCESS_OPCODE_SIG(HEALTH_OPCODE_ATTENTION_SET_UNACKED), handle_attention_set },
};

/* Calculates the value of the fast publish interval. */
static void divide_publish_interval(access_publish_resolution_t input_resolution, uint8_t input_steps,
        access_publish_resolution_t * p_output_resolution, uint8_t * p_output_steps, uint8_t divisor)
{
    uint32_t ms100_value = input_steps;

    /* Convert the step value to 100 ms steps depending on the resolution: */
    switch (input_resolution)
    {
        case ACCESS_PUBLISH_RESOLUTION_100MS:
            break;
        case ACCESS_PUBLISH_RESOLUTION_1S:
            ms100_value *= 10;
            break;
        case ACCESS_PUBLISH_RESOLUTION_10S:
            ms100_value *= 100;
            break;
        case ACCESS_PUBLISH_RESOLUTION_10MIN:
            ms100_value *= 6000;
            break;
        default:
            NRF_MESH_ASSERT(false);
    }

    /* Divide the 100 ms value with the fast divisor: */
    ms100_value /= 1u << divisor;

    /* Get the divisor corresponding to the new number of 100 ms steps: */
    if (ms100_value < 10)
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_100MS;
        if (input_steps == 0)
        {
            *p_output_steps = 0;
        }
        else
        {
            /* Avoid accidentally disabling publishing by dividing by too high a number */
            *p_output_steps = MAX(1, ms100_value);
        }
    }
    else if (ms100_value < 100)
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_1S;
        *p_output_steps = ms100_value / 10;
    }
    else if (ms100_value < 6000)
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_10S;
        *p_output_steps = ms100_value / 100;
    }
    else
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_10MIN;
        *p_output_steps = ms100_value / 6000;
    }

}

/********** Interface functions **********/

void health_server_fault_register(health_server_t * p_server, uint8_t fault_code)
{
    bool faults_present = health_server_fault_count_get(p_server) > 0;

    bitfield_set(p_server->current_faults, fault_code);
    bitfield_set(p_server->registered_faults, fault_code);

    /* If no faults were present before registering the new fault, set the publish interval to the fast interval: */
    if (!faults_present)
    {
        access_publish_resolution_t fast_res;
        uint8_t fast_steps;

        NRF_MESH_ASSERT(access_model_publish_period_get(p_server->model_handle, &p_server->regular_publish_res, &p_server->regular_publish_steps) == NRF_SUCCESS);
        divide_publish_interval(p_server->regular_publish_res, p_server->regular_publish_steps, &fast_res, &fast_steps, p_server->fast_period_divisor);
        NRF_MESH_ASSERT(access_model_publish_period_set(p_server->model_handle, fast_res, fast_steps) == NRF_SUCCESS);
    }
}

void health_server_fault_clear(health_server_t * p_server, uint8_t fault_code)
{
    bool faults_present_before = health_server_fault_count_get(p_server) > 0;

    bitfield_clear(p_server->current_faults, fault_code);

    /* If faults were present before clearing the array and not after, reset the publish interval: */
    if (faults_present_before && health_server_fault_count_get(p_server) == 0)
    {
        NRF_MESH_ASSERT(access_model_publish_period_set(p_server->model_handle, p_server->regular_publish_res, p_server->regular_publish_steps) == NRF_SUCCESS);
    }
}

bool health_server_fault_is_set(health_server_t * p_server, uint8_t fault_code)
{
    return bitfield_get(p_server->current_faults, fault_code);
}

uint8_t health_server_fault_count_get(const health_server_t * p_server)
{
    return bitfield_popcount(p_server->current_faults, HEALTH_SERVER_FAULT_ARRAY_SIZE);
}

uint8_t health_server_attention_get(const health_server_t * p_server)
{
    return p_server->attention_timer;
}

void health_server_attention_set(health_server_t * p_server, uint8_t attention)
{
    attention_timer_set(p_server, attention);
}

void health_server_send_fault_status(health_server_t * p_server)
{
    send_fault_status(p_server, HEALTH_OPCODE_CURRENT_STATUS, NULL);
}

uint32_t health_server_init(health_server_t * p_server, uint16_t element_index, uint16_t company_id,
        health_server_attention_cb_t attention_cb,
        const health_server_selftest_t * p_selftests, uint8_t num_selftests,
        health_server_publish_timeout_cb_t publish_timeout_handler)
{
    p_server->attention_handler = attention_cb;
    p_server->p_selftests = p_selftests;
    p_server->num_selftests = num_selftests;
    p_server->previous_test_id = 0;
    p_server->company_id = company_id;
    p_server->attention_timer = 0;
    p_server->fast_period_divisor = 0;
    p_server->p_next = NULL;
    p_server->publish_timeout_handler = publish_timeout_handler;

    /* Clear fault arrays: */
    bitfield_clear_all(p_server->registered_faults, HEALTH_SERVER_FAULT_ARRAY_SIZE);
    bitfield_clear_all(p_server->current_faults, HEALTH_SERVER_FAULT_ARRAY_SIZE);

    access_model_add_params_t add_params =
    {
        .element_index = element_index,
        .model_id = ACCESS_MODEL_SIG( HEALTH_SERVER_MODEL_ID), /*lint !e64 Type Mismatch */
        .p_opcode_handlers = m_opcode_handlers,
        .opcode_count = sizeof(m_opcode_handlers) / sizeof(m_opcode_handlers[0]),
        .publish_timeout_cb = health_publish_timeout_handler,
        .p_args = p_server
    };
    return access_model_add(&add_params, &p_server->model_handle);
}
