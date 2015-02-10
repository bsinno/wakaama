/*******************************************************************************
 *
 * Copyright (c) 2014 Bosch Software Innovations GmbH, Germany.
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
 *    Bosch Software Innovations GmbH - Please refer to git log
 *
 *******************************************************************************/
/*
 *  blockwise.c
 *  basic functions for blockwise data transfer.
 *
 *  Created on: 19.12.2014
 *  Author: Achim Kraus
 *  Copyright (c) 2014 Bosch Software Innovations GmbH, Germany. All rights reserved.
 */

#include <stdlib.h>
#include "liblwm2m.h"
#include "internals.h"

//#define USE_ETAG
//#define TEST_ETAG

#ifdef WITH_LOGS
static void prv_logUri(const char* desc, const lwm2m_uri_t * uriP)
{
    uint8_t flag = uriP->flag & LWM2M_URI_MASK_ID;
    LOG("%s flag 0x%x %d/%d/%d\n", desc, flag, uriP->objectId, uriP->instanceId, uriP->resourceId);
}

#define LOG_URI(D, U) prv_logUri(D, U)
#else
#define LOG_URI(D, U)
#endif

static int prv_blockwise_compare_uri(const lwm2m_uri_t * uri1P, const lwm2m_uri_t * uri2P)
{
    uint8_t flag = uri1P->flag & LWM2M_URI_MASK_ID;
#if 0
    LOG_URI("URI1:", uri1P);
    LOG_URI("URI2:", uri2P);
#endif
    if (flag != (uri2P->flag & LWM2M_URI_MASK_ID ))
        return 1;
    if ((flag & LWM2M_URI_FLAG_OBJECT_ID ) && (uri1P->objectId != uri2P->objectId))
        return 2;
    if ((flag & LWM2M_URI_FLAG_INSTANCE_ID ) && (uri1P->instanceId != uri2P->instanceId))
        return 3;
    if ((flag & LWM2M_URI_FLAG_RESOURCE_ID ) && (uri1P->resourceId != uri2P->resourceId))
        return 4;
    return 0;
}

static void prv_blockwise_set_time(lwm2m_blockwise_t* current)
{
    struct timeval tv;
    if (0 == lwm2m_gettimeofday(&tv, NULL))
    {
        current->time = tv.tv_sec;
    }
}

#ifdef USE_ETAG
static void prv_blockwise_etag(lwm2m_blockwise_t* current)
{
    static uint16_t counter = 0;
    uint16_t tag;
    uint32_t secs;
    int pos;
    struct timeval tv;

    if (sizeof(secs) <= sizeof(current->etag) && 0 == lwm2m_gettimeofday(&tv, NULL))
    {
        uint32_t secs = tv.tv_sec;
        memcpy(current->etag, &secs, sizeof(secs));
        current->etag_len = sizeof(secs);
    }
    pos = MIN(sizeof(current->etag) - sizeof(tag), current->etag_len);
    if (0 <= pos)
    {
        tag = ++counter;
        memcpy(current->etag + pos, &tag, sizeof(tag));
        current->etag_len += sizeof(tag);
    }
}
#endif

static coap_status_t prv_init_large_buffer(large_buffer_t * large_buffer, coap_packet_t * response, uint32_t size)
{
    coap_status_t result = NO_ERROR;
    large_buffer->size = (0 < size) ? size : response->payload_len * 4;
    large_buffer->length = 0;
    large_buffer->buffer = lwm2m_malloc(large_buffer->size);
    if (NULL == large_buffer->buffer) return COAP_500_INTERNAL_SERVER_ERROR;
    memset(large_buffer->buffer, 0, large_buffer->size);
    result = blockwise_append_large_buffer(large_buffer, 0, response);
    if (NO_ERROR != result) {
        lwm2m_free(large_buffer->buffer);
    }
    return result;
}


lwm2m_blockwise_t* blockwise_get(lwm2m_context_t * contextP, void * fromSessionH, coap_method_t method, const lwm2m_uri_t * uriP)
{
    lwm2m_blockwise_t* current = contextP->blockwiseList;
    while (NULL != current)
    {
        if ((current->method == method) &&
            (current->fromSessionH == fromSessionH) &&
            (0 == prv_blockwise_compare_uri(&current->uri, uriP)))
        {
            LOG("Found blockwise %d bytes for %d/%d/%d\n", current->buffer.length, current->uri.objectId,
                    current->uri.instanceId, current->uri.resourceId);
            return current;
        }
        current = current->next;
    }
    return NULL;
}

lwm2m_blockwise_t * blockwise_new(lwm2m_context_t * contextP, void * fromSessionH, coap_method_t method, const lwm2m_uri_t * uriP, coap_packet_t * messageP, bool detach, uint32_t size)
{
    lwm2m_blockwise_t* result = (lwm2m_blockwise_t *) lwm2m_malloc(sizeof(lwm2m_blockwise_t));
    if (NULL == result)
        return NULL;
    memset(result, 0, sizeof(lwm2m_blockwise_t));

    if (detach)
    {
	    if (NO_ERROR != prv_init_large_buffer(&(result->buffer), messageP, size))
	    {
	        lwm2m_free(result);
	        return NULL;
	    }
	    prv_blockwise_set_time(result);
	}
	else 
	{
        result->buffer.length = messageP->payload_len;
        result->buffer.size = messageP->payload_len;
        result->buffer.buffer = messageP->payload;
	}
	
    result->next = contextP->blockwiseList;
    contextP->blockwiseList = result;

    result->fromSessionH = fromSessionH;
    result->method = method;
    result->uri = *uriP;
    result->etag_len = messageP->etag_len;
    if (0 < result->etag_len)
    {
        memcpy(result->etag, messageP->etag, messageP->etag_len);
    }
#ifdef USE_ETAG
    else
    {
        prv_blockwise_etag(result);
    }
#endif

    LOG_URI("URI:", uriP);
    LOG("New blockwise %d bytes for %d/%d/%d\n", result->buffer.length, result->uri.objectId, result->uri.instanceId,
            result->uri.resourceId);
    return result;
}

void blockwise_prepare(lwm2m_blockwise_t * blockwiseP, uint32_t block_num, uint16_t block_size,
        coap_packet_t * response)
{
    uint32_t block_offset = block_num * block_size;
    int packet_payload_length = MIN(blockwiseP->buffer.length - block_offset, block_size);
    int more = block_offset + packet_payload_length < blockwiseP->buffer.length;
    if (0 == block_num)
    {
        coap_set_header_size2(response, blockwiseP->buffer.length);
    }
    coap_set_header_block2(response, block_num, more, block_size);
    if (0 < blockwiseP->etag_len)
    {
#ifdef TEST_ETAG
        // change etag to test detection by server.
        // Currently ignored by server (cal 1.0.0 / leshan 0.1.9).
        blockwiseP->etag[0]++;
#endif
        coap_set_header_etag(response, blockwiseP->etag, blockwiseP->etag_len);
    }
    coap_set_payload(response, blockwiseP->buffer.buffer + block_offset, packet_payload_length);
    prv_blockwise_set_time(blockwiseP);
    LOG("  Blockwise: prepare bs %d, index %d, offset %d (%s)\r\n", block_size, block_num, block_offset,
            more ? "more..." : "last");
}

coap_status_t blockwise_append(lwm2m_blockwise_t * blockwiseP, uint32_t block_offset, coap_packet_t * response)
{
    prv_blockwise_set_time(blockwiseP);
    return blockwise_append_large_buffer(&(blockwiseP->buffer), block_offset, response);
}

void blockwise_remove(lwm2m_context_t * contextP, const lwm2m_uri_t * uriP, lwm2m_blockwise_t* remove)
{
    lwm2m_blockwise_t* current = contextP->blockwiseList;
    lwm2m_blockwise_t* prev = NULL;
    while (NULL != current)
    {
        if (current == remove || (NULL != uriP && 0 == prv_blockwise_compare_uri(&current->uri, uriP)))
        {
            if (NULL == prev)
            {
                contextP->blockwiseList = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            LOG("Remove blockwise %d bytes for %d/%d/%d\n", current->buffer.length, current->uri.objectId,
                    current->uri.instanceId, current->uri.resourceId);
            lwm2m_free(current->buffer.buffer);
            lwm2m_free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

void blockwise_free(lwm2m_context_t * contextP, uint32_t time)
{
    uint32_t timeout;
    int removed = 0;
    int pending = 0;
    lwm2m_blockwise_t* current = contextP->blockwiseList;
    lwm2m_blockwise_t* rem = NULL;
    lwm2m_blockwise_t* prev = NULL;
    while (NULL != current)
    {
        timeout = current->fromSessionH != NULL ? COAP_DEFAULT_MAX_AGE * 2 : COAP_DEFAULT_MAX_AGE;
        if ((time - current->time) > timeout)
        {
            rem = current;
            if (NULL == prev)
            {
                contextP->blockwiseList = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            ++removed;
        }
        else
        {
            ++pending;
            prev = current;
        }
        current = current->next;
        if (NULL != rem)
        {
            LOG("Free blockwise %d bytes for %d/%d/%d\n", rem->buffer.length, rem->uri.objectId, rem->uri.instanceId,
                    rem->uri.resourceId);
            lwm2m_free(rem->buffer.buffer);
            lwm2m_free(rem);
            rem = NULL;
        }
    }
    if (pending || removed)
    {
        LOG("Blockwise %lu time, %d pending, %d removed\n", (unsigned long) time, pending, removed);
    }
}

coap_status_t blockwise_append_large_buffer(large_buffer_t * large_buffer, uint32_t block_offset, coap_packet_t * response)
{
    size_t length = large_buffer->length;

    LOG("Blockwise: append %u bytes (at %lu, %lu bytes before)\n", (unsigned int) response->payload_len, (unsigned long)block_offset, (unsigned long)large_buffer->length);

    if (large_buffer->length < block_offset) return COAP_408_ENTITY_INCOMPLETE;

    large_buffer->length = block_offset + response->payload_len;
    if (large_buffer->length > large_buffer->size)
    {
        size_t newSize = large_buffer->size * 2;
        uint8_t* newPayload = lwm2m_malloc(newSize);
        if (NULL == newPayload)
            return COAP_413_ENTITY_TOO_LARGE;
        memcpy(newPayload, large_buffer->buffer, length);
        memset(newPayload + length, 0, newSize - large_buffer->length);
        lwm2m_free(large_buffer->buffer);
        large_buffer->size = newSize;
        large_buffer->buffer = newPayload;
    }
    memcpy(large_buffer->buffer + block_offset, response->payload, response->payload_len);
    return NO_ERROR;
}

large_buffer_t * blockwise_new_large_buffer(coap_packet_t * response, uint32_t size)
{
    LOG("Blockwise: new transfer %lu bytes\n", (unsigned long) size);
    large_buffer_t * result = lwm2m_malloc(sizeof(large_buffer_t));
    if (NULL == result) return NULL;
    if (NO_ERROR != prv_init_large_buffer(result, response, size))
    {
        lwm2m_free(result);
        result = NULL;
    }
    return result;
}

void blockwise_free_large_buffer(large_buffer_t * large_buffer)
{
    lwm2m_free(large_buffer->buffer);
    lwm2m_free(large_buffer);
}
