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
#include <stdio.h>


#ifdef LWM2M_CLIENT_MODE
struct _handle_result_ handle_dm_request(lwm2m_context_t * contextP,
                                lwm2m_uri_t * uriP,
                                void * fromSessionH,
                                coap_packet_t * message,
                                coap_packet_t * response)
{
    struct _handle_result_ result = { .responseCode = NO_ERROR, .flags = 0};

    switch (message->code)
    {
    case COAP_GET:
        {
            char * buffer = NULL;
            int length = 0;

            result.responseCode = object_read(contextP, uriP, &buffer, &length);
            /* always set buffer as payload to omit memory leaks! */
            coap_set_payload(response, buffer, length);
            result.freePayload = 1;
            // lwm2m_handle_packet will free buffer
            if (result.responseCode == COAP_205_CONTENT)
            {
                if (IS_OPTION(message, COAP_OPTION_OBSERVE))
                {
                    result.responseCode = handle_observe_request(contextP, uriP, fromSessionH, message, response);
                }
            }
        }
        break;
    case COAP_POST:
        {
            if (!LWM2M_URI_IS_SET_INSTANCE(uriP))
            {
                result.responseCode = object_create(contextP, uriP, (char*) message->payload, message->payload_len);
                if (result.responseCode == COAP_201_CREATED)
                {
                    //longest uri is /65535/65535 = 12 + 1 (null) chars
                    char location_path[13] = "";
                    //instanceId expected
                    if ((uriP->flag & LWM2M_URI_FLAG_INSTANCE_ID) == 0)
                    {
                        result.responseCode = COAP_500_INTERNAL_SERVER_ERROR;
                        break;
                    }

                    if (sprintf(location_path, "/%d/%d", uriP->objectId, uriP->instanceId) < 0)
                    {
                        result.responseCode = COAP_500_INTERNAL_SERVER_ERROR;
                        break;
                    }
                    coap_set_header_location_path(response, location_path);
                }
            }
            else if (!LWM2M_URI_IS_SET_RESOURCE(uriP))
            {
                if (object_isInstanceNew(contextP, uriP->objectId, uriP->instanceId))
                {
                    result.responseCode = object_create(contextP, uriP, (char*) message->payload, message->payload_len);
                    if (COAP_204_CHANGED == result.responseCode)
                    {
                        result.valueChanged = 1;
                    }
                }
                else
                {
                    result.responseCode = object_write(contextP, uriP, (char*)message->payload, message->payload_len);
                    if (COAP_204_CHANGED == result.responseCode)
                    {
                        result.valueChanged = 1;
                    }
                }
            }
            else
            {
                result.responseCode = object_execute(contextP, uriP, (char*)message->payload, message->payload_len);
            }
        }
        break;
    case COAP_PUT:
        {
            if(message->payload != NULL) {
              if (LWM2M_URI_IS_SET_INSTANCE(uriP))
              {
                result.responseCode = object_write(contextP, uriP, (char *)message->payload, message->payload_len);
                if (COAP_204_CHANGED == result.responseCode)
                {
                    result.valueChanged = 1;
                }
              }
            }
            else if(message->uri_query != NULL) {
                result.responseCode = object_attrib(contextP, uriP, message->uri_query, fromSessionH);
            }
            else {
                result.responseCode = COAP_400_BAD_REQUEST;
            }
        }
        break;
    case COAP_DELETE:
        {
            if (LWM2M_URI_IS_SET_INSTANCE(uriP) && !LWM2M_URI_IS_SET_RESOURCE(uriP))
            {
                result.responseCode = object_delete(contextP, uriP);
            }
            else
            {
                result.responseCode = BAD_REQUEST_4_00;
            }
        }
        break;
    default:
        result.responseCode = BAD_REQUEST_4_00;
        break;
    }

    return result;
}
#endif

#ifdef LWM2M_SERVER_MODE

#define ID_AS_STRING_MAX_LEN 8

static void dm_result_callback(lwm2m_transaction_t * transacP,
                               void * message)
{
    dm_data_t * dataP = (dm_data_t *)transacP->userData;

    if (message == NULL)
    {
        dataP->callback(((lwm2m_client_t*)transacP->peerP)->internalID,
                        &dataP->uri,
                        COAP_503_SERVICE_UNAVAILABLE,
                        NULL, 0,
                        dataP->userData);
    }
    else
    {
        coap_packet_t * packet = (coap_packet_t *)message;

        //if packet is a CREATE response and the instanceId was assigned by the client
        if (packet->code == COAP_201_CREATED
         && packet->location_path != NULL)
        {
            char * locationString = NULL;
            int result = 0;
            lwm2m_uri_t locationUri;

            locationString = coap_get_multi_option_as_string(packet->location_path);
            if (locationString == NULL)
            {
                LOG("Error: coap_get_multi_option_as_string() failed for Location_path option in dm_result_callback()\n");
                return;
            }

            result = lwm2m_stringToUri(locationString, strlen(locationString), &locationUri);
            if (result == 0)
            {
                LOG("Error: lwm2m_stringToUri() failed for Location_path option in dm_result_callback()\n");
                lwm2m_free(locationString);
                return;
            }

            ((dm_data_t*)transacP->userData)->uri.instanceId = locationUri.instanceId;
            ((dm_data_t*)transacP->userData)->uri.flag = locationUri.flag;

            lwm2m_free(locationString);
        }

        dataP->callback(((lwm2m_client_t*)transacP->peerP)->internalID,
                        &dataP->uri,
                        packet->code,
                        packet->payload,
                        packet->payload_len,
                        dataP->userData);
    }
    lwm2m_free(dataP);
}

static int prv_make_operation(lwm2m_context_t * contextP,
                              uint16_t clientID,
                              lwm2m_uri_t * uriP,
                              coap_method_t method,
                              char * buffer,
                              int length,
                              char * uriQuery,
                              lwm2m_result_callback_t callback,
                              void * userData)
{
    int result;
    lwm2m_client_t * clientP;
    lwm2m_transaction_t * transaction;
    dm_data_t * dataP;

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;

    transaction = transaction_new(COAP_TYPE_CON, method, uriP, contextP->nextMID++, 4, NULL, ENDPOINT_CLIENT, (void *)clientP);
    if (transaction == NULL) return INTERNAL_SERVER_ERROR_5_00;

    if (buffer != NULL)
    {
        // TODO: Take care of fragmentation
        coap_set_payload(transaction->message, buffer, length);
    }

    if (uriQuery != NULL)
    {
        coap_set_header_uri_query(transaction->message, uriQuery);
    }

    if (callback != NULL)
    {
        dataP = (dm_data_t *)lwm2m_malloc(sizeof(dm_data_t));
        if (dataP == NULL)
        {
            transaction_free(transaction);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        memcpy(&dataP->uri, uriP, sizeof(lwm2m_uri_t));
        dataP->callback = callback;
        dataP->userData = userData;

        transaction->callback = dm_result_callback;
        transaction->userData = (void *)dataP;
    }

    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transaction);

    result = transaction_send(contextP, transaction);

    transaction_recover_payload(transaction);

    return result;
}

int lwm2m_dm_read(lwm2m_context_t * contextP,
                  uint16_t clientID,
                  lwm2m_uri_t * uriP,
                  lwm2m_result_callback_t callback,
                  void * userData)
{
    return prv_make_operation(contextP, clientID, uriP,
                              COAP_GET, NULL, 0, NULL,
                              callback, userData);
}

int lwm2m_dm_write(lwm2m_context_t * contextP,
                   uint16_t clientID,
                   lwm2m_uri_t * uriP,
                   char * buffer,
                   int length,
                   lwm2m_result_callback_t callback,
                   void * userData)
{
    if (!LWM2M_URI_IS_SET_INSTANCE(uriP)
     || length == 0)
    {
        return COAP_400_BAD_REQUEST;
    }

    if (LWM2M_URI_IS_SET_RESOURCE(uriP))
    {
        return prv_make_operation(contextP, clientID, uriP,
                                  COAP_PUT, buffer, length, NULL,
                                  callback, userData);
    }
    else
    {
        return prv_make_operation(contextP, clientID, uriP,
                                  COAP_POST, buffer, length, NULL,
                                  callback, userData);
    }
}

int lwm2m_dm_attribute(lwm2m_context_t * contextP,
                   uint16_t clientID,
                   lwm2m_uri_t * uriP,
                   char * buffer,
                   int length,
                   lwm2m_result_callback_t callback,
                   void * userData)
{
/// TODO (wa20341#1#): allow attribute write for object/instance/resource
    if (!LWM2M_URI_IS_SET_INSTANCE(uriP)
     || length == 0)
    {
        return COAP_400_BAD_REQUEST;
    }

    return prv_make_operation(contextP, clientID, uriP,
                              COAP_PUT, NULL, 0, buffer,
                              callback, userData);
}

int lwm2m_dm_execute(lwm2m_context_t * contextP,
                     uint16_t clientID,
                     lwm2m_uri_t * uriP,
                     char * buffer,
                     int length,
                     lwm2m_result_callback_t callback,
                     void * userData)
{
    if (!LWM2M_URI_IS_SET_RESOURCE(uriP))
    {
        return COAP_400_BAD_REQUEST;
    }

    return prv_make_operation(contextP, clientID, uriP,
                              COAP_POST, buffer, length, NULL,
                              callback, userData);
}

int lwm2m_dm_create(lwm2m_context_t * contextP,
                    uint16_t clientID,
                    lwm2m_uri_t * uriP,
                    char * buffer,
                    int length,
                    lwm2m_result_callback_t callback,
                    void * userData)
{
    if (LWM2M_URI_IS_SET_RESOURCE(uriP)
     || length == 0)
    {
        return COAP_400_BAD_REQUEST;
    }

    return prv_make_operation(contextP, clientID, uriP,
                              COAP_POST, buffer, length, NULL,
                              callback, userData);
}

int lwm2m_dm_delete(lwm2m_context_t * contextP,
                    uint16_t clientID,
                    lwm2m_uri_t * uriP,
                    lwm2m_result_callback_t callback,
                    void * userData)
{
    if (!LWM2M_URI_IS_SET_INSTANCE(uriP)
     || LWM2M_URI_IS_SET_RESOURCE(uriP))
    {
        return COAP_400_BAD_REQUEST;
    }

    return prv_make_operation(contextP, clientID, uriP,
                              COAP_DELETE, NULL, 0, NULL,
                              callback, userData);
}
#endif
