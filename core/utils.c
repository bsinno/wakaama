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

#include "internals.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>


int lwm2m_PlainTextToInt64(char * buffer,
                           int length,
                           int64_t * dataP)
{
    uint64_t result = 0;
    int sign = 1;
    int mul = 0;
    int i = 0;

    if (0 == length) return 0;

    if (buffer[0] == '-')
    {
        sign = -1;
        i = 1;
    }

    while (i < length)
    {
        if ('0' <= buffer[i] && buffer[i] <= '9')
        {
            if (0 == mul)
            {
                mul = 10;
            }
            else
            {
                result *= mul;
            }
            result += buffer[i] - '0';
        }
        else
        {
            return 0;
        }
        i++;
    }

    *dataP = result * sign;
    return 1;
}

int lwm2m_int8ToPlainText(int8_t data,
                          char ** bufferP)
{
    return lwm2m_int64ToPlainText((int64_t)data, bufferP);
}

int lwm2m_int16ToPlainText(int16_t data,
                           char ** bufferP)
{
    return lwm2m_int64ToPlainText((int64_t)data, bufferP);
}

int lwm2m_int32ToPlainText(int32_t data,
                           char ** bufferP)
{
    return lwm2m_int64ToPlainText((int64_t)data, bufferP);
}

int lwm2m_int64ToPlainText(int64_t data,
                           char ** bufferP)
{
    char string[32];
    int len;

    len = snprintf(string, 32, "%" PRId64, data);
    if (len > 0)
    {
        *bufferP = (char *)lwm2m_malloc(len);

        if (NULL != *bufferP)
        {
            strncpy(*bufferP, string, len);
        }
        else
        {
            len = 0;
        }
    }

    return len;
}


int lwm2m_float32ToPlainText(float data,
                             char ** bufferP)
{
    return lwm2m_float64ToPlainText((double)data, bufferP);
}


int lwm2m_float64ToPlainText(double data,
                             char ** bufferP)
{
    char string[64];
    int len;

    len = snprintf(string, 64, "%lf", data);
    if (len > 0)
    {
        *bufferP = (char *)lwm2m_malloc(len);

        if (NULL != *bufferP)
        {
            strncpy(*bufferP, string, len);
        }
        else
        {
            len = 0;
        }
    }

    return len;
}


int lwm2m_boolToPlainText(bool data,
                          char ** bufferP)
{
    return lwm2m_int64ToPlainText((int64_t)(data?1:0), bufferP);
}

lwm2m_binding_t lwm2m_stringToBinding(const char *buffer,
                                      size_t length)
{
    // test order is important
    if (strncmp(buffer, "U", length) == 0)
    {
        return BINDING_U;
    }
    if (strncmp(buffer, "S", length) == 0)
    {
        return BINDING_S;
    }
    if (strncmp(buffer, "UQ", length) == 0)
    {
        return BINDING_UQ;
    }
    if (strncmp(buffer, "SQ", length) == 0)
    {
        return BINDING_SQ;
    }
    if (strncmp(buffer, "US", length) == 0)
    {
        return BINDING_UQ;
    }
    if (strncmp(buffer, "UQS", length) == 0)
    {
        return BINDING_UQ;
    }

    return BINDING_UNKNOWN;
}

#define CODE_TO_STRING(X)   case X : return "(" #X ") "

const char* lwm2m_statusToString(int status)
{
    switch(status) {
    CODE_TO_STRING(COAP_NO_ERROR);
    CODE_TO_STRING(COAP_GET);
    CODE_TO_STRING(COAP_POST);
    CODE_TO_STRING(COAP_PUT);
    CODE_TO_STRING(COAP_DELETE);
    CODE_TO_STRING(COAP_201_CREATED);
    CODE_TO_STRING(COAP_202_DELETED);
    CODE_TO_STRING(COAP_204_CHANGED);
    CODE_TO_STRING(COAP_205_CONTENT);
    CODE_TO_STRING(COAP_231_CONTINUE);
    CODE_TO_STRING(COAP_400_BAD_REQUEST);
    CODE_TO_STRING(COAP_401_UNAUTHORIZED);
    CODE_TO_STRING(COAP_402_BAD_OPTION);
    CODE_TO_STRING(COAP_404_NOT_FOUND);
    CODE_TO_STRING(COAP_405_METHOD_NOT_ALLOWED);
    CODE_TO_STRING(COAP_406_NOT_ACCEPTABLE);
    CODE_TO_STRING(COAP_408_ENTITY_INCOMPLETE);
    CODE_TO_STRING(COAP_413_ENTITY_TOO_LARGE);
    CODE_TO_STRING(COAP_500_INTERNAL_SERVER_ERROR);
    CODE_TO_STRING(COAP_501_NOT_IMPLEMENTED);
    CODE_TO_STRING(COAP_503_SERVICE_UNAVAILABLE);
    CODE_TO_STRING(COAP_505_PROXYING_NOT_SUPPORTED);
    default: return "";
    }
}

#define TYPE_TO_STRING(X)   case X : return #X

const char* lwm2m_typeToString(int type)
{
    switch(type) {
    TYPE_TO_STRING(COAP_TYPE_CON);
    TYPE_TO_STRING(COAP_TYPE_NON);
    TYPE_TO_STRING(COAP_TYPE_ACK);
    TYPE_TO_STRING(COAP_TYPE_RST);
    default: return "";
    }
}


void lwm2m_output_buffer(FILE * stream,
                   const uint8_t * buffer,
                   int length)
{
    int i;
    int j;

    i = 0;
    while (i < length)
    {

        fprintf(stream, "    ");
        for (j = 0 ; j < 16 && i+j < length; j++)
        {
            fprintf(stream, "%02X ", buffer[i+j]);
        }
        if (i != 0)
        {
            while (j < 16)
            {
                fprintf(stream, "   ");
                j++;
            }
        }
        fprintf(stream, "  ");
        for (j = 0 ; j < 16 && i+j < length; j++)
        {
            if (isprint(buffer[i+j]))
            {
                fprintf(stream, "%c ", buffer[i+j]);
            }
            else
            {
                fprintf(stream, ". ");
            }
        }
        fprintf(stream, "\n");

        i += 16;
    }
    fflush(stream);
}

#ifdef WITH_LOGS
static void prv_print_hex(const char* head, int len, const uint8_t* data)
{
    if (0 < len) {
        fprintf(stdout, "  %s: %u\n", head, len);
        lwm2m_output_buffer(stdout, data, len);
    }
}

static void prv_print_multi_option(const char* head, multi_option_t* multi)
{
    int index = 0;
    while (NULL != multi) {
        if (0 < multi->len) {
            fprintf(stdout, "  %s.%d: %u\n", head, index, multi->len);
            lwm2m_output_buffer(stdout, (uint8_t*) multi->data, multi->len);
        }
        ++index;
        multi = multi->next;
    }
}

static void prv_print_option_block(const char* head, coap_packet_t* message, int num)
{
    uint8_t  block_more = 0;
    uint32_t block_num = 0;
    uint16_t block_size = REST_MAX_CHUNK_SIZE;
    uint32_t block_offset = 0;
    int result = 0;
    if (1 == num) {
        result = coap_get_header_block1(message, &block_num, &block_more, &block_size, &block_offset);
    }
    else if (2 == num) {
        result = coap_get_header_block2(message, &block_num, &block_more, &block_size, &block_offset);
    }
    if (result) {
        fprintf(stdout, "  %s%d: %lu %u %s\n", head, num, (unsigned long)block_num, (unsigned int)block_size, block_more?"more...":"ready");
    }
}

static void prv_print_option_size(const char* head, coap_packet_t* message, int num)
{
    uint32_t size = 0;
    int result = 0;
    if (1 == num) {
        result = coap_get_header_size1(message, &size);
    }
    else if (2 == num) {
        result = coap_get_header_size2(message, &size);
    }
    if (result) {
        fprintf(stdout, "  %s%d: %lu\n", head, num, (unsigned long)size);
    }
}

static void prv_print_option_observe(const char* head, coap_packet_t* message)
{
    uint32_t value = 0;
    if (coap_get_header_observe(message, &value)) {
        fprintf(stdout, "  %s: %lu\n", head, (unsigned long)value);
    }
}


#endif

void lwm2m_print_status(const char* head, coap_packet_t* message, int size)
{
#ifdef WITH_LOGS
    uint8_t code = message->code;
    fprintf(stdout, "%s: ver. %u, mid %u, %u bytes, %s, code %d.%02d %s\r\n", head, message->version, message->mid, size, lwm2m_typeToString(message->type), (code&0xE0)>>5, code&0x1F, lwm2m_statusToString(code));
    prv_print_hex("Token", message->token_len, message->token);
    prv_print_multi_option("URI", message->uri_path);
    prv_print_multi_option("Query", message->uri_query);
    prv_print_multi_option("Location", message->location_path);
    prv_print_hex("ETag", message->etag_len, message->etag);
    prv_print_option_observe("Observe", message);
    prv_print_option_block("Block", message, 1);
    prv_print_option_block("Block", message, 2);
    prv_print_option_size("Size", message, 1);
    prv_print_option_size("Size", message, 2);
    prv_print_hex("Payload", message->payload_len, message->payload);
#endif
}

int lwm2m_adjustTimeout(time_t nextTime, time_t currentTime, struct timeval* timeoutP)
{
    int left = 0; 
    time_t interval;

    if (nextTime > currentTime)
    {
        interval = nextTime - currentTime;
        left = interval;
    }
    else
    {
        interval = 1;
    }

    if (timeoutP->tv_sec > interval)
    {
        
        timeoutP->tv_sec = interval;
    }
    // LOG("time %ld, next %ld, timeout %ld, left %d\n", currentTime, nextTime, timeoutP->tv_sec, left);

    return left;
}

#ifdef MEMORY_TRACE

#undef malloc
#undef free
#undef strdup

typedef struct MemoryEntry {
    struct MemoryEntry* next;
    const char *file;
    const char *function;
    int         lineno;
    size_t      size;
    int         count;
    uint32_t    data[1];
} memory_entry_t;

static memory_entry_t prv_memory_malloc_list = { .next = NULL, .file = "head", .function="malloc", .lineno = 0, .size = 0, .count = 0};
static memory_entry_t prv_memory_free_list = { .next = NULL, .file = "head", .function="free", .lineno = 0, .size = 0, .count = 0};

static memory_entry_t* prv_memory_find_previous(memory_entry_t* list, void* memory) {
    while (NULL != list->next) {
        if (list->next->data == memory) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

static void prv_trace_add_free_list(memory_entry_t* remove, const char* file, const char* function, int lineno)
{
    remove->next = prv_memory_free_list.next;
    prv_memory_free_list.next = remove;
    remove->file = file;
    remove->function = function;
    remove->lineno = lineno;

    if (prv_memory_free_list.count < 200) {
        ++prv_memory_free_list.count;
    }
    else if (NULL != remove->next) {
        while (NULL != remove->next->next) {
            remove = remove->next;
        }
        free(remove->next);
        remove->next = NULL;
    }
}

char* trace_strdup(const char* str, const char* file, const char* function, int lineno) {
    size_t length = strlen(str);
    char* result = trace_malloc(length +1, file, function, lineno);
    memcpy(result, str, length);
    result[length] = 0;
    return result;
}

void* trace_malloc(size_t size, const char* file, const char* function, int lineno)
{
    static int counter = 0;
    memory_entry_t* entry = malloc(size + sizeof(memory_entry_t));
    entry->next = prv_memory_malloc_list.next;
    prv_memory_malloc_list.next = entry;
    ++prv_memory_malloc_list.count;
    prv_memory_malloc_list.size += size;
    prv_memory_malloc_list.lineno = 1;

    entry->file = file;
    entry->function = function;
    entry->lineno = lineno;
    entry->size = size;
    entry->count = ++counter;

    return &(entry->data);
}

void trace_free(void* mem, const char* file, const char* function, int lineno)
{
    if (NULL != mem) {
        memory_entry_t* entry = prv_memory_find_previous(&prv_memory_malloc_list, mem);
        if (NULL != entry) {
            memory_entry_t* remove = entry->next;
            entry->next = remove->next;
            --prv_memory_malloc_list.count;
            prv_memory_malloc_list.size -= remove->size;
            prv_memory_malloc_list.lineno = 1;
            prv_trace_add_free_list(remove, file, function, lineno);
        }
        else {
            fprintf(stderr, "memory: free error %s, %d, %s\n", file, lineno, function);
            memory_entry_t* entry = prv_memory_find_previous(&prv_memory_free_list, mem);
            if (NULL != entry) {
                entry = entry->next;
                fprintf(stderr, "memory: already frees at %s, %d, %s\n", entry->file, entry->lineno, entry->function);
            }
        }
    }
}

void trace_print(int loops, int level) {
    static int counter = 0;
    if (0 == loops) {
        counter = 0;
    }
    else {
        ++counter;
    }
    if (0 == loops || (((counter % loops) == 0) && prv_memory_malloc_list.lineno)) {
        prv_memory_malloc_list.lineno = 0;
        if (1 == level) {
            size_t total = 0;
            int entries = 0;
            memory_entry_t* entry = prv_memory_malloc_list.next;
            while (NULL != entry) {
                fprintf(stdout,"memory: #%d, %lu bytes, %s, %d, %s\n", entry->count, (unsigned long) entry->size, entry->file, entry->lineno, entry->function);
                ++entries;
                total += entry->size;
                entry = entry->next;
            }
            if (entries != prv_memory_malloc_list.count) {
                fprintf(stderr,"memory: error %d entries != %d\n", prv_memory_malloc_list.count, entries);
            }
            if (total != prv_memory_malloc_list.size) {
                fprintf(stdout,"memory: error %lu total bytes != %lu\n", (unsigned long) prv_memory_malloc_list.size, (unsigned long) total);
            }
        }
        fprintf(stdout,"memory: %d entries, %lu total bytes\n", prv_memory_malloc_list.count, (unsigned long) prv_memory_malloc_list.size);
    }
}
#endif
