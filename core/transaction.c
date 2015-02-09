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
 *    Simon Bernard - Please refer to git log
 *    Toby Jaffey - Please refer to git log
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

/*
 * lwm2m_blockwise_t is only valid for a COAP server, when the uri identifies the resource.
 * For COAP clients use large_buffer_t in the transaction!
 */
typedef struct {
    uint8_t * buffer;
    uint32_t  size;
    uint32_t  length;
} large_buffer_t;

/*
 * Modulo mask (+1 and +0.5 for rounding) for a random number to get the tick number for the random
 * retransmission time between COAP_RESPONSE_TIMEOUT and COAP_RESPONSE_TIMEOUT*COAP_RESPONSE_RANDOM_FACTOR.
 */
#define COAP_RESPONSE_TIMEOUT_TICKS         (CLOCK_SECOND * COAP_RESPONSE_TIMEOUT)
#define COAP_RESPONSE_TIMEOUT_BACKOFF_MASK  ((CLOCK_SECOND * COAP_RESPONSE_TIMEOUT * (COAP_RESPONSE_RANDOM_FACTOR - 1)) + 1.5)

static int prv_check_addr(void * leftSessionH,
                          void * rightSessionH)
{
    if ((leftSessionH == NULL)
     || (rightSessionH == NULL)
     || (leftSessionH != rightSessionH))
    {
        return 0;
    }

    return 1;
}

static int prv_check_token(lwm2m_transaction_t * transacP,
        coap_packet_t * message)
{
    if (transacP->token_len == 0) return 1;

    if (IS_OPTION(message, COAP_OPTION_TOKEN)) {
        const uint8_t* token;
        int len = coap_get_header_token(message, &token);
        if (transacP->token_len == len) {
            if (memcmp(transacP->token, token, len)==0) return 1;
        }
    }

    return 0;
}

static void prv_append_blockwise_data(large_buffer_t * blockwiseP, uint32_t block_offset, coap_packet_t * response)
{
    size_t length = blockwiseP->length;
    blockwiseP->length = block_offset + response->payload_len;
    if (blockwiseP->length > blockwiseP->size)
    {
        size_t newSize = blockwiseP->size * 2;
        uint8_t* newPayload = lwm2m_malloc(newSize);
        if (NULL == newPayload)
            return;
        memcpy(newPayload, blockwiseP->buffer, length);
        memset(newPayload + length, 0, newSize - blockwiseP->length);
        lwm2m_free(blockwiseP->buffer);
        blockwiseP->size = newSize;
        blockwiseP->buffer = newPayload;
    }
    memcpy(blockwiseP->buffer + block_offset, response->payload, response->payload_len);
    LOG("Blockwise: append %u bytes (at %u, %u bytes overall)\n",response->payload_len, block_offset, blockwiseP->length);
}

static large_buffer_t * prv_new_blockwise_data(coap_packet_t * response, uint32_t size)
{
    LOG("Blockwise: new transfer\n");
    large_buffer_t * result = lwm2m_malloc(sizeof(large_buffer_t));
    if (NULL == result) return NULL;
    result->size = (0 < size) ? size : response->payload_len * 4;
    result->length = 0;
    result->buffer = lwm2m_malloc(result->size);
    prv_append_blockwise_data(result, 0, response);
    return result;
}

static void prv_free_blockwise_data(large_buffer_t * blockwiseP)
{
    lwm2m_free(blockwiseP->buffer);
    lwm2m_free(blockwiseP);
}

static void prv_transaction_reset(lwm2m_transaction_t * transacP)
{
    lwm2m_free(transacP->buffer);
    transacP->buffer = NULL;
    transacP->ack_received = 0;
    transacP->mID = ((coap_packet_t*) transacP->message)->mid;
    transacP->retrans_counter = 0;
}


lwm2m_transaction_t * transaction_new(coap_method_t method,
                                      lwm2m_uri_t * uriP,
                                      uint16_t mID,
                                      uint8_t token_len,
                                      uint8_t* token,
                                      lwm2m_endpoint_type_t peerType,
                                      void * peerP)
{
    lwm2m_transaction_t * transacP;
    int result;

    transacP = (lwm2m_transaction_t *)lwm2m_malloc(sizeof(lwm2m_transaction_t));

    if (NULL == transacP) return NULL;
    memset(transacP, 0, sizeof(lwm2m_transaction_t));

    transacP->message = lwm2m_malloc(sizeof(coap_packet_t));
    if (NULL == transacP->message) goto error;

    coap_init_message(transacP->message, COAP_TYPE_CON, method, mID);

    transacP->mID = mID;
    transacP->peerType = peerType;
    transacP->peerP = peerP;

    switch(transacP->peerType)
    {
    case ENDPOINT_CLIENT:
        transacP->blocksize = ((lwm2m_client_t*)peerP)->blocksize;
        break;

    case ENDPOINT_SERVER:
        transacP->blocksize = ((lwm2m_server_t*)peerP)->blocksize;
        break;

    default:
        transacP->blocksize = REST_MAX_CHUNK_SIZE;
        break;
    }

    if (NULL != uriP)
    {
        result = snprintf(transacP->objStringID, LWM2M_STRING_ID_MAX_LEN, "%hu", uriP->objectId);
        if (result < 0 || result > LWM2M_STRING_ID_MAX_LEN) goto error;

        coap_set_header_uri_path_segment(transacP->message, transacP->objStringID);

        if (LWM2M_URI_IS_SET_INSTANCE(uriP))
        {
            result = snprintf(transacP->instanceStringID, LWM2M_STRING_ID_MAX_LEN, "%hu", uriP->instanceId);
            if (result < 0 || result > LWM2M_STRING_ID_MAX_LEN) goto error;
            coap_set_header_uri_path_segment(transacP->message, transacP->instanceStringID);
        }
        else
        {
            if (LWM2M_URI_IS_SET_RESOURCE(uriP))
            {
                coap_set_header_uri_path_segment(transacP->message, NULL);
            }
        }
        if (LWM2M_URI_IS_SET_RESOURCE(uriP))
        {
            result = snprintf(transacP->resourceStringID, LWM2M_STRING_ID_MAX_LEN, "%hu", uriP->resourceId);
            if (result < 0 || result > LWM2M_STRING_ID_MAX_LEN) goto error;
            coap_set_header_uri_path_segment(transacP->message, transacP->resourceStringID);
        }
    }
    transacP->token_len = token_len;
    if (0 < token_len)
    {
        if (NULL != token)
        {
            memcpy(transacP->token, token, token_len);
        }
        else {
            struct timeval tv;
            lwm2m_gettimeofday(&tv, NULL);
            transacP->token[0] = mID;
            transacP->token[1] = mID >> 8;
            transacP->token[2] = tv.tv_sec;
            transacP->token[3] = tv.tv_sec >> 8;
            transacP->token[4] = tv.tv_sec >> 16;
            transacP->token[5] = tv.tv_sec >> 24;
        }
        coap_set_header_token(transacP->message, transacP->token, token_len);
    }

    return transacP;

error:
    lwm2m_free(transacP);
    return NULL;
}

void transaction_free(lwm2m_transaction_t * transacP)
{
    if (transacP->message) {
        coap_free_header(transacP->message);
        lwm2m_free(transacP->message);
    }
    if (transacP->buffer) lwm2m_free(transacP->buffer);
    if (transacP->blockwise) {
        prv_free_blockwise_data(transacP->blockwise);
    }
    lwm2m_free(transacP);
}

void transaction_remove(lwm2m_context_t * contextP,
                        lwm2m_transaction_t * transacP)
{
    if (NULL != contextP->transactionList)
    {
        if (transacP == contextP->transactionList)
        {
            contextP->transactionList = contextP->transactionList->next;
        }
        else
        {
            lwm2m_transaction_t *previous = contextP->transactionList;
            while (previous->next && previous->next != transacP)
            {
                previous = previous->next;
            }
            if (NULL != previous->next)
            {
                previous->next = previous->next->next;
            }
        }
    }
    transaction_free(transacP);
}


void transaction_handle_response(lwm2m_context_t * contextP,
                                 void * fromSessionH,
                                 coap_packet_t * message)
{
    bool found = false;
    lwm2m_transaction_t * transacP;

    transacP = contextP->transactionList;

    while (transacP != NULL)
    {
        void * targetSessionH;

        targetSessionH = NULL;
        switch (transacP->peerType)
        {
    #ifdef LWM2M_SERVER_MODE
        case ENDPOINT_CLIENT:
            targetSessionH = ((lwm2m_client_t *)transacP->peerP)->sessionH;
            break;
    #endif

    #ifdef LWM2M_CLIENT_MODE
        case ENDPOINT_SERVER:
            targetSessionH = ((lwm2m_server_t *)transacP->peerP)->sessionH;
            break;
    #endif

        default:
            break;
        }

        if (prv_check_addr(fromSessionH, targetSessionH))
        {
            if (!transacP->ack_received)
            {
                if (transacP->mID == message->mid)
                {
                    found = true;
                    transacP->ack_received = true;
                }
            }
            if (transacP->ack_received && prv_check_token(transacP, message))
            {
                // HACK: If a message is sent from the monitor callback,
                // it will arrive before the registration ACK.
                // So we resend transaction that were denied for authentication reason.
                if ((message->code == COAP_401_UNAUTHORIZED) && (COAP_MAX_RETRANSMIT < transacP->retrans_counter))
                {
                    transacP->ack_received = false;
                    transacP->retrans_time += COAP_RESPONSE_TIMEOUT;
                    return;
                }
                void* messageData = message->payload;
                size_t messageDataLength =  message->payload_len;

                if (IS_OPTION(message, COAP_OPTION_BLOCK2)) {
                    uint8_t more = 0;
                    uint32_t resource_size = 0;
                    uint32_t block_num = 0;
                    uint32_t block_offset = 0;
                    uint16_t block_size = REST_MAX_CHUNK_SIZE;
                    large_buffer_t * blockwiseP = (large_buffer_t *) transacP->blockwise;
                    coap_packet_t* transRequest = (coap_packet_t*) transacP->message;

                    coap_get_header_size2(message, &resource_size);
                    coap_get_header_block2(message, &block_num, &more, &block_size, &block_offset);
                    LOG("Blockwise: block response %u (%u/%u) %s @ %u bytes\n", block_num, block_size, REST_MAX_CHUNK_SIZE,
                                more ? "more..." : "last", block_offset);
                    block_size = MIN(block_size, REST_MAX_CHUNK_SIZE);

                    if (NULL == blockwiseP) {
                        blockwiseP = prv_new_blockwise_data(message, resource_size);
                        transacP->blockwise = blockwiseP;
                    }
                    else {
                        prv_append_blockwise_data(blockwiseP, block_offset, message);
                    }
                    if (more) {
                        transRequest->mid = contextP->nextMID++;
                        transRequest->payload_len = 0;
                        coap_set_header_block2(transRequest, block_num + 1, 1, block_size);
                        prv_transaction_reset(transacP);
                        transaction_send(contextP, transacP);
                        return;
                    }
                    message->payload = blockwiseP->buffer;
                    message->payload_len = blockwiseP->length;
                }
                if (transacP->callback != NULL)
                {
                    transacP->callback(transacP, message);
                }
                transaction_remove(contextP, transacP);
                message->payload = messageData;
                message->payload_len = messageDataLength;
                return;
            }
            // if we found our guy, exit
            if (found) {
                struct timeval tv;
                if (0 == lwm2m_gettimeofday(&tv, NULL))
                {
                    transacP->retrans_time = tv.tv_sec;
                }
                if (transacP->response_timeout) {
                    transacP->retrans_time += transacP->response_timeout;
                }
                else {
                    transacP->retrans_time += COAP_RESPONSE_TIMEOUT * transacP->retrans_counter;
                }
                return;
            }
        }

        transacP = transacP->next;
    }
}

int transaction_send(lwm2m_context_t * contextP,
                     lwm2m_transaction_t * transacP)
{
    if (transacP->buffer == NULL)
    {
        uint8_t tempBuffer[LWM2M_MAX_PACKET_SIZE];
        int length;
        uint16_t block_size = transacP->blocksize;
        coap_packet_t* message = (coap_packet_t*) transacP->message;

        if (block_size < message->payload_len) {
            transacP->blockwise = prv_new_blockwise_data(message, message->payload_len);

        }

        length = coap_serialize_message(message, tempBuffer);
        if (length <= 0) return COAP_500_INTERNAL_SERVER_ERROR;

        transacP->buffer = (uint8_t*)lwm2m_malloc(length);
        if (transacP->buffer == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

        memcpy(transacP->buffer, tempBuffer, length);
        transacP->buffer_len = length;
    }

    if (!transacP->ack_received)
    {
        switch(transacP->peerType)
        {
        case ENDPOINT_CLIENT:
            lwm2m_print_status("Send",  transacP->message,  transacP->buffer_len);
            contextP->bufferSendCallback(((lwm2m_client_t*)transacP->peerP)->sessionH,
                                         transacP->buffer, transacP->buffer_len, contextP->userData);

            break;

        case ENDPOINT_SERVER:
            lwm2m_print_status("Send",  transacP->message,  transacP->buffer_len);
            contextP->bufferSendCallback(((lwm2m_server_t*)transacP->peerP)->sessionH,
                                         transacP->buffer, transacP->buffer_len, contextP->userData);
            break;

        default:
            return 0;
        }

        if (transacP->retrans_counter == 0)
        {
            struct timeval tv;

            if (0 == lwm2m_gettimeofday(&tv, NULL))
            {
                transacP->retrans_time = tv.tv_sec;
                transacP->retrans_counter = 1;
            }
            else
            {
                // crude error handling
                transacP->retrans_counter = COAP_MAX_RETRANSMIT;
            }
        }

        if (transacP->retrans_counter < COAP_MAX_RETRANSMIT)
        {
            transacP->retrans_time += COAP_RESPONSE_TIMEOUT * transacP->retrans_counter;
            transacP->retrans_counter++;
        }
    }
    if (transacP->ack_received || COAP_MAX_RETRANSMIT <= transacP->retrans_counter)
    {
        if (transacP->callback)
        {
            transacP->callback(transacP, NULL);
        }
        transaction_remove(contextP, transacP);
        return -1;
    }

    return 0;
}
/**
 * If payload gets invalid (e.g. original array gets out of scope when returning from function),
 * it should be recovered from the serialized message to get proper LOG.
 */
void transaction_recover_payload(lwm2m_transaction_t * transacP)
{
    if (NULL != transacP) {
        coap_packet_t *message = (coap_packet_t *) transacP->message;
        if (0 <  message->payload_len) {
            message->payload = transacP->buffer + transacP->buffer_len - message->payload_len;
        }
    }
}

