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

/************************************************************************
 *  Function for communications transactions.
 *
 *  Basic specification: rfc7252
 *
 *  Transaction implements processing of several communication dialogs specified in the
 *  above specification.
 *  The caller registers a callback function, which is called, when either the result is
 *  received or a timeout occurs.
 *
 *  Supported dialogs:
 *  Requests (GET - DELETE):
 *  - CON with mid, without token => regular finished with corresponding ACK.MID
 *  - CON with mid, with token => regular finished with corresponding ACK.MID and response containing
 *                  the token. Supports both versions, with piggybacked ACK and separate ACK/response.
 *  - NON without token => no transaction, no result expected!
 *  - NON with token => regular finished with response containing the token.
 *  Responses (COAP_201_CREATED - ?):
 *  - CON with mid => regular finished with corresponding ACK.MID
 */

#include "internals.h"

/*
 * Modulo mask (+1 and +0.5 for rounding) for a random number to get the tick number for the random
 * retransmission time between COAP_RESPONSE_TIMEOUT and COAP_RESPONSE_TIMEOUT*COAP_RESPONSE_RANDOM_FACTOR.
 */
#define COAP_RESPONSE_TIMEOUT_TICKS         (CLOCK_SECOND * COAP_RESPONSE_TIMEOUT)
#define COAP_RESPONSE_TIMEOUT_BACKOFF_MASK  ((CLOCK_SECOND * COAP_RESPONSE_TIMEOUT * (COAP_RESPONSE_RANDOM_FACTOR - 1)) + 1.5)


#define TRANSACTION_OBSERVE_OPTION 0x80000000

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

static uint16_t prv_adjust_blocksize(lwm2m_transaction_t * transacP, uint16_t blocksize, int result)
{
    uint16_t* blocksizeP = NULL;
    switch(transacP->peerType)
    {
        case ENDPOINT_CLIENT:
            blocksizeP = &(((lwm2m_client_t*)transacP->peerP)->blocksize);
            break;

        case ENDPOINT_SERVER:
            blocksizeP = &(((lwm2m_server_t*)transacP->peerP)->blocksize);
            break;
        case ENDPOINT_UNKNOWN:
            break;
    }
    if (result) {
        blocksize = MIN(REST_MAX_CHUNK_SIZE, blocksize);
        if (NULL != blocksizeP) {
            *blocksizeP = blocksize;
        }
    }
    else {
        if (NULL != blocksizeP) {
            blocksize = *blocksizeP;
        }
        else {
            blocksize = REST_MAX_CHUNK_SIZE;
        }
    }
    return blocksize;
}

static int prv_transaction_check_finished(lwm2m_transaction_t * transacP,
        coap_packet_t * receivedMessage)
{
    coap_packet_t * transactionMessage = transacP->message;

    // check send CON message for ack
    if (transactionMessage->type == COAP_TYPE_CON && !transacP->ack_received) return 0;

    // check send message for token, if none, the transaction is already finished
    if (!IS_OPTION(transactionMessage, COAP_OPTION_TOKEN)) return 1;

    // check send message for method/status-code, if a status code is found, the transaction is finished
    if (COAP_DELETE < transactionMessage->code) return 1;

    // so a request with token was send, wait for response with matching token.
    if (IS_OPTION(receivedMessage, COAP_OPTION_TOKEN)) {
        const uint8_t* token;
        int len = coap_get_header_token(receivedMessage, &token);
        if (transactionMessage->token_len == len) {
            if (memcmp(transactionMessage->token, token, len)==0) return 1;
        }
    }

    return 0;
}

static void prv_transaction_reset(lwm2m_transaction_t * transacP)
{
    transacP->buffer_len = 0;
    transacP->ack_received = 0;
    transacP->mID = ((coap_packet_t*) transacP->message)->mid;
    transacP->retrans_counter = 0;
}

static int prv_transaction_send_next_block(lwm2m_context_t * contextP, coap_packet_t * message, lwm2m_transaction_t * transacP)
{
    int result;
    uint8_t more = 0;
    uint32_t block_num = 0;
    uint32_t block_offset = 0;
    uint16_t block_size = REST_MAX_CHUNK_SIZE;
    coap_status_t code = message->code;
    coap_packet_t * transactionMessage = (coap_packet_t *) transacP->message;
    large_buffer_t * blockwiseP = transacP->blockwise1;

    result = coap_get_header_block1(message, &block_num, &more, &block_size, &block_offset);

    block_size = prv_adjust_blocksize(transacP, block_size, result);

    if (COAP_413_ENTITY_TOO_LARGE == code) {
        if (result && 0 == block_num && block_size < transacP->blocksize) {
            if (NULL == transacP->blockwise1) {
                transacP->blockwise1 = blockwise_new_large_buffer(transactionMessage, transactionMessage->payload_len);
                if (NULL == transacP->blockwise1) {
                    // transaction must be finished with error COAP_413_ENTITY_TOO_LARGE
                    return 0;
                }
            }
            more = 1;
            transacP->blocksize = block_size;
            coap_set_header_size1(transactionMessage, transacP->blockwise1->length);
            coap_set_payload(transactionMessage, transacP->blockwise1->buffer, block_size);
        }
        else {
            // transaction must be finished with error COAP_413_ENTITY_TOO_LARGE
            return 0;
        }
    }
    else if (NULL == blockwiseP) {
        // no blockwise transfer, just adjust the preferred block size
        return 0;
    }
    else if (0 == result) {
        // no block1 option in response
        transacP->error = ERROR_BLOCK1_IGNORED;
        return 0;
    }
    else {
        if (block_size < transacP->blocksize) {
            // adjust block_num & block_offset
            uint16_t oldSize;
            if ((0 == block_num) && coap_get_header_block1(transactionMessage, NULL, NULL, &oldSize, NULL)) {
                block_offset = oldSize;
                transacP->blocksize = block_size;
                block_num = oldSize / block_size;
            }
            else {
                transacP->error = ERROR_CHANGING_BLOCKSIZE;
                return 0;
            }
        }
        else {
            block_offset += block_size;
            ++block_num;
        }
        if (block_offset < blockwiseP->length) {
            // next block
            uint16_t length = block_size;
            RESET_OPTION(transactionMessage, COAP_OPTION_SIZE1);
            more = block_offset + block_size < blockwiseP->length;
            if (!more) length = blockwiseP->length - block_offset;
            coap_set_payload(transactionMessage, blockwiseP->buffer + block_offset, length);
        }
        else {
            // transfer finished. process response.
            return 0;
        }
    }

    transactionMessage->mid = contextP->nextMID++;
    coap_set_header_block1(transactionMessage, block_num, more, block_size);
    prv_transaction_reset(transacP);
    transaction_send(contextP, transacP);
    return 1;
}

static int prv_transaction_request_next_block(lwm2m_context_t * contextP, coap_packet_t * message, lwm2m_transaction_t * transacP)
{
    int result;
    uint8_t more = 0;
    uint32_t resource_size = 0;
    uint32_t block_num = 0;
    uint32_t block_offset = 0;
    uint16_t block_size = REST_MAX_CHUNK_SIZE;
    large_buffer_t * blockwiseP = (large_buffer_t *) transacP->blockwise2;

    coap_get_header_size2(message, &resource_size);

    result = coap_get_header_block2(message, &block_num, &more, &block_size, &block_offset);
    block_size = prv_adjust_blocksize(transacP, block_size, result);

    LOG("Blockwise: response %u %u %s @ %u bytes\n", block_num, block_size, more ? "more..." : "last", block_offset);

    if (NULL == blockwiseP) {
        blockwiseP = blockwise_new_large_buffer(message, resource_size);
        if (NULL == blockwiseP) {
            transacP->error = ERROR_OUT_OF_MEMORY;
        }
        else {
            transacP->blockwise2 = blockwiseP;
        }
    }
    else {
        int appendResult = blockwise_append_large_buffer(blockwiseP, block_offset, message);
        switch(appendResult) {
        case COAP_408_ENTITY_INCOMPLETE :
            transacP->error = ERROR_RESPONSE_INCOMPLETE;
            break;
        case COAP_413_ENTITY_TOO_LARGE :
            transacP->error = ERROR_OUT_OF_MEMORY;
            break;
        }
    }
    if (more && transacP->error == NO_ERROR && COAP_400_BAD_REQUEST > message->code) {
        coap_packet_t * transactionMessage = (coap_packet_t *) transacP->message;
        if (coap_get_header_observe(message, &(transacP->observe))) {
            // save observe option
            LOG("Blockwise: save observe %lu\n", (unsigned long) transacP->observe);
            transacP->observe |= TRANSACTION_OBSERVE_OPTION;
        }
        // request next block
        transactionMessage->mid = contextP->nextMID++;
        transactionMessage->payload_len = 0;
        RESET_OPTION(transactionMessage, COAP_OPTION_OBSERVE);
        // on request the more bit in option2 must be zero
        coap_set_header_block2(transactionMessage, block_num + 1, 0, block_size);
        prv_transaction_reset(transacP);
        transaction_send(contextP, transacP);
        return 1;
    }
    else {
        // set accumulated payload for callback
        message->payload = blockwiseP->buffer;
        message->payload_len = blockwiseP->length;
    }
    return 0;
}

lwm2m_transaction_t * transaction_new(coap_message_type_t type,
                                      coap_method_t method,
                                      lwm2m_uri_t * uriP,
                                      uint16_t mID,
                                      uint8_t token_len,
                                      uint8_t* token,
                                      lwm2m_endpoint_type_t peerType,
                                      void * peerP)
{
    lwm2m_transaction_t * transacP;
    int result;

    // no transactions for ack or rst
    if (COAP_TYPE_ACK == type || COAP_TYPE_RST == type) return NULL;

    if (COAP_TYPE_NON == type) {
        // no transactions for NON responses
        if (COAP_DELETE < method) return NULL;
        // no transactions for NON request without token
        if (0 == token_len) return NULL;
    }

    transacP = (lwm2m_transaction_t *)lwm2m_malloc(sizeof(lwm2m_transaction_t));

    if (NULL == transacP) return NULL;
    memset(transacP, 0, sizeof(lwm2m_transaction_t));

    transacP->message = lwm2m_malloc(sizeof(coap_packet_t));
    if (NULL == transacP->message) goto error;

    coap_init_message(transacP->message, type, method, mID);

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
    if (0 < token_len)
    {
        if (NULL != token)
        {
            coap_set_header_token(transacP->message, token, token_len);
        }
        else {
            // generate a token
            uint8_t temp_token[COAP_TOKEN_LEN];
            struct timeval tv;

            lwm2m_gettimeofday(&tv, NULL);

            // initialize first 6 bytes, leave the last 2 random
            temp_token[0] = mID;
            temp_token[1] = mID >> 8;
            temp_token[2] = tv.tv_sec;
            temp_token[3] = tv.tv_sec >> 8;
            temp_token[4] = tv.tv_sec >> 16;
            temp_token[5] = tv.tv_sec >> 24;
            // use just the provided amount of bytes
            coap_set_header_token(transacP->message, temp_token, token_len);
        }
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
    if (transacP->buffer) {
        lwm2m_free(transacP->buffer);
    }
    if (transacP->blockwise1) {
        blockwise_free_large_buffer(transacP->blockwise1);
    }
    if (transacP->blockwise2) {
        blockwise_free_large_buffer(transacP->blockwise2);
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

    while (NULL != transacP)
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

            if (prv_transaction_check_finished(transacP, message))
            {
                // save original payload, maybe replaced by blockwise read
                void * messageData = message->payload;
                size_t messageDataLength =  message->payload_len;
                coap_status_t code = message->code;

                if (IS_OPTION(message, COAP_OPTION_BLOCK1) || IS_OPTION((coap_packet_t*)transacP->message, COAP_OPTION_BLOCK1)) {
                    if ((COAP_400_BAD_REQUEST > code) || (COAP_413_ENTITY_TOO_LARGE == code)) {
                        if (prv_transaction_send_next_block(contextP, message, transacP))
                            return;
                    }
                }

                // HACK: If a message is sent from the monitor callback,
                // it will arrive before the registration ACK.
                // So we resend transaction that were denied for authentication reason.
                if ((COAP_401_UNAUTHORIZED == code) && (COAP_MAX_RETRANSMIT < transacP->retrans_counter))
                {
                    transacP->ack_received = false;
                    transacP->retrans_time += COAP_RESPONSE_TIMEOUT;
                    return;
                }

                if (REST_MAX_CHUNK_SIZE < message->payload_len) {
                    transacP->error = ERROR_RECEIVED_CHUNK_TOO_LARGE;
                }
                else if ((COAP_400_BAD_REQUEST > code) && IS_OPTION(message, COAP_OPTION_BLOCK2)) {
                    if (prv_transaction_request_next_block(contextP, message, transacP))
                        return;
                }

                if (transacP->callback != NULL)
                {
                    if (!IS_OPTION(message, COAP_OPTION_OBSERVE) && (transacP->observe & TRANSACTION_OBSERVE_OPTION)) {
                        // restore observe option
                        transacP->observe &= ~TRANSACTION_OBSERVE_OPTION;
                        LOG("Blockwise: restore observe %lu\n", (long unsigned) transacP->observe);
                        coap_set_header_observe(message, transacP->observe);
                    }
                    transacP->callback(transacP, message);
                }
                transaction_remove(contextP, transacP);
                // restore old payload
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
    if (NULL == transacP->buffer || 0 == transacP->buffer_len)
    {
        uint8_t tempBuffer[LWM2M_MAX_PACKET_SIZE];
        int length;
        uint16_t block_size = transacP->blocksize;
        coap_packet_t* message = (coap_packet_t*) transacP->message;

        if (block_size < message->payload_len) {
            if (NULL == transacP->blockwise1) {
                transacP->blockwise1 = blockwise_new_large_buffer(message, message->payload_len);
                if (NULL == transacP->blockwise1) {
                    transacP->error = ERROR_OUT_OF_MEMORY;
                    return COAP_500_INTERNAL_SERVER_ERROR;
                }
                else {
                    coap_set_header_block1(message, 0, 1, block_size);
                    coap_set_header_size1(message, message->payload_len);
                    coap_set_payload(message, message->payload, block_size);
                }
            }
        }

        length = coap_serialize_message(message, tempBuffer);
        if (length <= 0) return COAP_500_INTERNAL_SERVER_ERROR;

        if (NULL != transacP->buffer && length > transacP->buffer_size) {
            // old buffer too small
            lwm2m_free(transacP->buffer);
        }

        if (NULL == transacP->buffer) {
            transacP->buffer = (uint8_t*)lwm2m_malloc(length);
            if (transacP->buffer == NULL) {
                transacP->error = ERROR_OUT_OF_MEMORY;
                return COAP_500_INTERNAL_SERVER_ERROR;
            }
            transacP->buffer_size = length;
        }

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

