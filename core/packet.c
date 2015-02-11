/*******************************************************************************
 *
 * Copyright (c) 2013, 2014 Intel Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    David Navarro, Intel Corporation - initial API and implementation
 *    domedambrosio - Please refer to git log
 *    Fabien Fleutot - Please refer to git log
 *    Fabien Fleutot - Please refer to git log
 *    Simon Bernard - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Achim Kraus, Bosch Software Innovations GmbH - Please refer to git log
 *
 *******************************************************************************/

/*
 Copyright (c) 2013, 2014 Intel Corporation

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 THE POSSIBILITY OF SUCH DAMAGE.

 David Navarro <david.navarro@intel.com>

 */

/*
 Contains code snippets which are:

 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.

 */

#include "internals.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>


static void prv_handle_reset(lwm2m_context_t * contextP,
                         void * fromSessionH,
                         coap_packet_t * message)
{
#ifdef LWM2M_CLIENT_MODE
    cancel_observe(contextP, message->mid, fromSessionH);
#endif
}

static struct _handle_result_ prv_handle_request(lwm2m_context_t * contextP, void * fromSessionH,
        lwm2m_uri_t * uriP, coap_packet_t * message, coap_packet_t * response)
{
    struct _handle_result_ result = { .responseCode = NO_ERROR, .flags = 0};
    uint8_t  block_more = 0;
    uint32_t block_num = 0;
    uint16_t block_size = REST_MAX_CHUNK_SIZE;
    uint32_t block_offset = 0;
    lwm2m_blockwise_t * blockwiseIn = NULL;
    lwm2m_blockwise_t * blockwiseOut = NULL;

    if (coap_get_header_block1(message, &block_num, &block_more, &block_size, &block_offset))
    {
        LOG("Blockwise: request %u (%u/%u) @ %u bytes\n", block_num, block_size, REST_MAX_CHUNK_SIZE,
                block_offset);
        block_size = MIN(block_size, REST_MAX_CHUNK_SIZE);
        coap_set_header_block1(response, block_num, block_more, block_size);

        blockwiseIn = blockwise_get(contextP, fromSessionH, message->code, uriP);
        if (NULL == blockwiseIn) {
            if (0 < block_num) {
                result.responseCode = COAP_408_ENTITY_INCOMPLETE;
            }
            else {
                uint32_t resource_size = 0;
                coap_get_header_size1(message, &resource_size);
                blockwiseIn = blockwise_new(contextP, fromSessionH, message->code, uriP, message, true, resource_size);
                if (NULL == blockwiseIn)
                {
                    result.responseCode = COAP_413_ENTITY_TOO_LARGE;
                }
            }
        }
        else {
            result.responseCode = blockwise_append(blockwiseIn, block_offset, message);
            if (NO_ERROR != result.responseCode) {
                blockwise_remove(contextP, NULL, blockwiseIn);
            }
        }
        if (block_more && NO_ERROR == result.responseCode) {
            result.responseCode = COAP_231_CONTINUE;
        }
    }

    if (NO_ERROR != result.responseCode) {
        return result;
    }

    /* get offset for blockwise transfers */
    block_size = REST_MAX_CHUNK_SIZE;
    if (coap_get_header_block2(message, &block_num, NULL, &block_size, &block_offset))
    {
        LOG("Blockwise: response %u (%u/%u) @ %u bytes\n", block_num, block_size, REST_MAX_CHUNK_SIZE,
                block_offset);
        block_size = MIN(block_size, REST_MAX_CHUNK_SIZE);
    }

    /* observe requests (GET/OPTION(OBSERVE) have side effects and therefore must be always handled */
    if (message->code != COAP_GET || !IS_OPTION(message, COAP_OPTION_OBSERVE))
    {
        blockwiseOut = blockwise_get(contextP, NULL, message->code, uriP);
    }
    if (NULL == blockwiseOut)
    {
        // save original payload
        void* messageData = message->payload;
        size_t messageDataLength =  message->payload_len;
        if (NULL != blockwiseIn) {
            // replace payload with accumulated payload
            message->payload = blockwiseIn->buffer.buffer;
            message->payload_len = blockwiseIn->buffer.length;
        }
        switch (uriP->flag & LWM2M_URI_MASK_TYPE ) {
#ifdef LWM2M_CLIENT_MODE
        case LWM2M_URI_FLAG_DM :
            // TODO: Authentify server
            result = handle_dm_request(contextP, uriP, fromSessionH, message, response);
            break;

        case LWM2M_URI_FLAG_BOOTSTRAP :
            result.responseCode = NOT_IMPLEMENTED_5_01;
            break;
#endif

#ifdef LWM2M_SERVER_MODE
            case LWM2M_URI_FLAG_REGISTRATION:
                result.responseCode = handle_registration_request(contextP, uriP, fromSessionH, message, response);
                break;
#endif
            default:
                result.responseCode = BAD_REQUEST_4_00;
                break;
        }
        // restore original payload
        message->payload = messageData;
        message->payload_len = messageDataLength;

        if (result.responseCode < BAD_REQUEST_4_00 && block_size < response->payload_len)
        {
            blockwiseOut = blockwise_new(contextP, NULL, message->code, uriP, response, false, 0);
            if (NULL == blockwiseOut)
            {
                result.responseCode = INTERNAL_SERVER_ERROR_5_00;
            }
            else {
                result.freePayload = 0;
            }
        }
    }

    if (result.responseCode < BAD_REQUEST_4_00)
    {
        if (NULL != blockwiseOut)
        {
            blockwise_prepare(blockwiseOut, block_num, block_size, response);
        }
    }

    return result;
}

/* This function is an adaptation of function coap_receive() from Erbium's er-coap-13-engine.c.
 * Erbium is Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 */
void lwm2m_handle_packet(lwm2m_context_t * contextP,
                        uint8_t * buffer,
                        int length,
                        void * fromSessionH)
{
    struct _handle_result_ result = { .responseCode = NO_ERROR, .flags = 0};
    static coap_packet_t message[1];
    static coap_packet_t response[1];

    result.responseCode = coap_parse_message(message, buffer, (uint16_t) length);
    if (result.responseCode == NO_ERROR)
    {
        lwm2m_print_status("Recv", message, length);

        if (message->code >= COAP_GET && message->code <= COAP_DELETE)
        {
            lwm2m_uri_t * uriP = NULL;

            /* prepare response */
            if (message->type == COAP_TYPE_CON)
            {
                /* Reliable CON requests are answered with an ACK. */
                coap_init_message(response, COAP_TYPE_ACK, CONTENT_2_05, message->mid);
            }
            else
            {
                /* Unreliable NON requests are answered with a NON as well. */
                coap_init_message(response, COAP_TYPE_NON, CONTENT_2_05, contextP->nextMID++);
            }

            /* mirror token */
            if (message->token_len)
            {
                coap_set_header_token(response, message->token, message->token_len);
            }

            uriP = lwm2m_decode_uri(message->uri_path);
            if (uriP == NULL) {
                result.responseCode = BAD_REQUEST_4_00;
            }
            else {
                result = prv_handle_request(contextP, fromSessionH, uriP, message, response);
            }
            if (result.responseCode < BAD_REQUEST_4_00)
            {
                coap_set_status_code(response, result.responseCode);
                result.responseCode = message_send(contextP, response, fromSessionH);
#ifdef LWM2M_CLIENT_MODE
                if (result.valueChanged) {
                    lwm2m_resource_value_changed(contextP, uriP);
                }
#endif
            }
            if (result.freePayload) {
                lwm2m_free(response->payload);
            }
            lwm2m_free(uriP);
        }
        else
        {
            if (message->type == COAP_TYPE_NON || message->type == COAP_TYPE_CON ) {

#ifdef LWM2M_SERVER_MODE
                if ( (message->code == COAP_204_CHANGED || message->code == COAP_205_CONTENT)
                        && IS_OPTION(message, COAP_OPTION_OBSERVE))
                {
                    handle_observe_notify(contextP, fromSessionH, message);
                }
                else
#endif
#ifdef LWM2M_CLIENT_MODE
                transaction_handle_response(contextP, fromSessionH, message);
#endif

                if (message->type == COAP_TYPE_CON ) {
                    coap_init_message(response, COAP_TYPE_ACK, 0, message->mid);
                    result.responseCode = message_send(contextP, response, fromSessionH);
                }
            }
            else if (message->type == COAP_TYPE_ACK || message->type == COAP_TYPE_RST ) {
            /* Responses */
                if (message->type==COAP_TYPE_RST)
                {
                    LOG("Received RST\n");
                    /* Cancel possible subscriptions. */
                    prv_handle_reset(contextP, fromSessionH, message);
                }
                else {
                    LOG("Received ACK\n");
                }
                transaction_handle_response(contextP, fromSessionH, message);
            }
        } /* Request or Response */

    } /* if (parsed correctly) */
    else
    {
        LOG("Message parsing failed %d\r\n", result.responseCode);
    }

    if (result.responseCode != NO_ERROR)
    {
        LOG("ERROR %d.%02d %s%s", (result.responseCode&0xE0)>>5, result.responseCode&0x1F, lwm2m_statusToString(result.responseCode), coap_error_message);

        /* Set to sendable error code. */
        if (result.responseCode >= 192)
        {
            result.responseCode = INTERNAL_SERVER_ERROR_5_00;
        }
        /* Reuse input buffer for error message. */
        coap_init_message(message, COAP_TYPE_ACK, result.responseCode, message->mid);
        coap_set_payload(message, coap_error_message, strlen(coap_error_message));
        message_send(contextP, message, fromSessionH);
    }
}


coap_status_t message_send(lwm2m_context_t * contextP,
                           coap_packet_t * message,
                           void * sessionH)
{
    coap_status_t result = INTERNAL_SERVER_ERROR_5_00;
    uint8_t pktBuffer[COAP_MAX_PACKET_SIZE + 1];
    uint32_t pktBufferLen = 0;

    pktBufferLen = coap_serialize_message(message, pktBuffer);
    if (0 != pktBufferLen)
    {
        lwm2m_print_status("Send", message, pktBufferLen);
        result = contextP->bufferSendCallback(sessionH, pktBuffer, pktBufferLen, contextP->userData);
    }
    coap_free_header(message);

    return result;
}

