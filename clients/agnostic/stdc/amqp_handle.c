/*---------------------------------------------------------------------------
 *  amqp_handle.c - Definition of HANDLE object and QUERY object
 *
 *  Copyright (c) 2004-2005 JPMorgan
 *  Copyright (c) 1991-2005 iMatix Corporation
 *---------------------------------------------------------------------------*/

#include "amqp_handle.h"
#include "handle_fsm.d"
#include "query_fsm.d"

/*****************************************************************************/
/* State machine definitions                                                 */

#define QUERY_OBJECT_ID context->index_tag

DEFINE_QUERY_CONTEXT_BEGIN
    connection_t
        connection;
    apr_uint16_t
        index_tag;                     /*  Id of lock that's used for        */
                                       /*  waiting for HANDLE INDEX          */
    char
        *results;                      /*  Result string returned by server  */
    apr_uint16_t
        current;                       /*  Current position within results   */
    apr_uint32_t
        range_current;                 /*  Actual message number within the  */
                                       /*  range; to be sent to client       */
    apr_uint32_t
        range_end;                     /*  Last message number within range  */
DEFINE_QUERY_CONTEXT_END

DEFINE_QUERY_CONSTRUCTOR_BEGIN
DEFINE_QUERY_CONSTRUCTOR_MIDDLE
    context->results = NULL;
DEFINE_QUERY_CONSTRUCTOR_END

DEFINE_QUERY_DESTRUCTOR_BEGIN
DEFINE_QUERY_DESTRUCTOR_MIDDLE
    if (context->results)
        free ((void*) context->results);
DEFINE_QUERY_DESTRUCTOR_END

typedef struct tag_message_list_item_t
{
    char
        buffer [BUFFER_SIZE];
    amqp_frame_t
        *frame;
    struct tag_message_list_item_t
        *prev;
    struct tag_message_list_item_t
        *next;
} message_list_item_t;

typedef struct tag_lock_list_item_t
{
    apr_uint16_t
        lock_id;
    struct tag_lock_list_item_t
        *prev;
    struct tag_lock_list_item_t
        *next;
} lock_list_item_t;

typedef struct tag_browse_list_item_t
{
    apr_uint32_t
        message_nbr;
    apr_uint16_t
        lock_id;
    struct tag_browse_list_item_t
        *next;
} browse_list_item_t;

#define HANDLE_OBJECT_ID context->id

DEFINE_HANDLE_CONTEXT_BEGIN
    apr_socket_t
        *socket;                    /*  Socket (same as connection socket)   */
    apr_uint16_t
        id;                         /*  Handle id                            */
    connection_t
        connection;                 /*  Connection handle belongs to         */
    channel_t
        channel;                    /*  Channel handle belongs to            */
    apr_uint16_t
        channel_id;                 /*  Id of channel handle belongs to      */
    apr_uint16_t
        created_tag;                /*  Id of lock that's used for waiting   */
                                    /*  for HANDLE CREATED                   */
    apr_uint16_t
        shutdown_tag;               /*  Id of lock that's used for waiting   */
                                    /*  for HANDLE CLOSE                     */
    apr_thread_mutex_t
        *query_sync;                /*  Critical section allowing only       */
                                    /*  single thread to perform             */
                                    /*  HANDLE QUERY at the same time        */ 
    query_context_t
        *query;                     /*  Query actualy waiting for HANDLE     */
                                    /*  INDEX                                */
    char
        *dest_name;                 /*  Name of temporary destination        */
    message_list_item_t
        *first_message;             /*  First item of list of received       */
                                    /*  messages                             */
    message_list_item_t
        *last_message;              /*  Last item of list of received        */
                                    /*  messages                             */
    lock_list_item_t
        *first_lock;                /*  First lock waiting for message       */
    lock_list_item_t
        *last_lock;                 /*  Last lock waiting for message        */
    browse_list_item_t
        *browse_requests;           /*  Linked list of requests for          */
                                    /*  synchronous HANDLE BROWSEs           */
DEFINE_HANDLE_CONTEXT_END

DEFINE_HANDLE_CONSTRUCTOR_BEGIN
DEFINE_HANDLE_CONSTRUCTOR_MIDDLE
    context->first_message = NULL;
    context->last_message = NULL;
    context->first_lock = NULL;
    context->last_lock = NULL;
    context->browse_requests = NULL;
    result = apr_thread_mutex_create (&(context->query_sync),
        APR_THREAD_MUTEX_UNNESTED, pool);
    TEST(result, apr_thread_mutex_create, buffer)
DEFINE_HANDLE_CONSTRUCTOR_END

DEFINE_HANDLE_DESTRUCTOR_BEGIN
DEFINE_HANDLE_DESTRUCTOR_MIDDLE
    /*  Remove handle from channel's list                                    */
    result = channel_remove_handle (context->channel,
        (handle_t) context);
    TEST(result, channel_remove_handle, buffer)
DEFINE_HANDLE_DESTRUCTOR_END

/*****************************************************************************/
/*  Helper functions (private)                                               */

static apr_status_t pair_lock_and_message (handle_context_t *context)
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    lock_list_item_t
        *lock;
    message_list_item_t
        *message;

    /*  This function is called from event hadlers where context is already  */
    /*  locked, so no need to lock again                                     */
    if (context->first_lock && context->first_message) {
        /*  There's a thread waiting for a message and a message present     */
        lock = context->first_lock;
        message = context->first_message;        
        /*  Remove lock from lock list                                       */
        context->first_lock = lock->next;
        if (lock->next)
            lock->next->prev = NULL;
        else
            context->last_lock = NULL;
        /*  Remove message from message list                                 */
        context->first_message = message->next;
        if (message->next)
            message->next->prev = NULL;
        else
            context->last_message = NULL;
        /*  Resume thread waiting for the message                            */
        result = release_lock (context->connection, lock->lock_id,
            (void*) message->frame);
        TEST(result, release_lock, buffer);
        /*  Deallocate lock list item                                        */
        /*  Message list item will be deallocated when client destroys the   */
        /*  message (amqp_destroy_message)                                   */
        free ((void*) lock);
    }
    return APR_SUCCESS;
}

/*****************************************************************************/
/*  Helper functions (public)                                                */

apr_status_t get_exclusive_access_to_query_dialogue (
    handle_t *ctx
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    handle_context_t
        *context = (handle_context_t*) ctx;

    /*  Wait for exclusive access to query/index dialogue                    */
    result = apr_thread_mutex_lock (context->query_sync);
    TEST(result, apr_thread_mutex_lock, buffer);
#   ifdef AMQTRACE_LOCKS
        printf ("# Exclusive access to QUERY/INDEX dialogue gained\n");
#   endif
    return APR_SUCCESS;
}

/*****************************************************************************/
/*  State machine actions (Handle)                                           */

inline static apr_status_t do_init (
    handle_context_t *context,
    connection_t connection,
    channel_t channel,
    apr_socket_t *socket,
    apr_uint16_t channel_id,
    apr_uint16_t handle_id,
    apr_uint16_t service_type,
    apr_byte_t producer,
    apr_byte_t consumer,
    apr_byte_t browser,
    const char* dest_name,
    const char* mime_type,
    const char* encoding,
    apr_byte_t async,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_uint16_t
        confirm_tag;

    /*  Save values that will be needed in future                            */
    context->connection = connection;
    context->channel = channel;
    context->channel_id = channel_id;
    context->socket = socket;
    context->id = handle_id;
    /*  Register that we will be waiting for handle open completion          */
    if (lock)
        *lock = NULL;
    confirm_tag = 0;
    if (!async) {
        result = register_lock (context->connection, channel_id, handle_id,
            &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    /*  Send handle open                                                     */
    result = amqp_handle_open (context->socket, buffer, BUFFER_SIZE,
        channel_id, handle_id, service_type, confirm_tag, producer,
        consumer, browser, 0, dest_name, mime_type, encoding,
        0, "");
    TEST(result, amqp_handle_open, buffer);
    return APR_SUCCESS;
}

inline static apr_status_t do_init_temporary (
    handle_context_t *context,
    connection_t connection,
    channel_t channel,
    apr_socket_t *socket,
    apr_uint16_t channel_id,
    apr_uint16_t handle_id,
    apr_uint16_t service_type,
    apr_byte_t producer,
    apr_byte_t consumer,
    apr_byte_t browser,
    const char* dest_name,
    const char* mime_type,
    const char* encoding,
    apr_byte_t async,
    amqp_lock_t *created_lock,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_uint16_t
        confirm_tag;
    
    /*  Save values that will be needed in future                            */
    context->connection = connection;
    context->channel = channel;
    context->channel_id = channel_id;
    context->socket = socket;
    context->id = handle_id;
    /*  Register that we will be waiting for handle open completion          */
    if (lock)
        *lock = NULL;
    confirm_tag = 0;
    if (!async) {
        result = register_lock (context->connection, channel_id, handle_id,
             &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    /*  We have to wait for HANDLE CREATED                                   */
    result = register_lock (context->connection, channel_id, handle_id,
        &(context->created_tag), created_lock);
    /*  Send handle open                                                     */
    result = amqp_handle_open (context->socket, buffer, BUFFER_SIZE,
        channel_id, handle_id, service_type, confirm_tag, producer,
        consumer, browser, 1, dest_name, mime_type, encoding,
        0, "");
    TEST(result, amqp_handle_open, buffer);
    return APR_SUCCESS;
}

inline static apr_status_t do_created (
    handle_context_t *context,
    const char *dest_name
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];

    /*  Store temporary destination name                                     */
    context->dest_name = (char*) apr_palloc (context->pool,
        strlen (dest_name) + 1);
    strcpy (context->dest_name, dest_name);
    /*  Unsuspend thread waiting for temporary queue creation                */
    result = release_lock (context->connection, context->created_tag,
        (void*) context->dest_name);
    TEST(result, release_lock, buffer)
    return APR_SUCCESS; 
}

inline static apr_status_t do_consume (
    handle_context_t *context,
    apr_uint16_t prefetch,
    apr_byte_t no_local,
    apr_byte_t unreliable,
    const char *dest_name,
    const char *identifier,
    const char *mime_type,
    apr_byte_t async,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_uint16_t
        confirm_tag;

    confirm_tag = 0;
    if (lock)
        *lock = NULL;
    if (!async) {
        result = register_lock (context->connection, context->channel_id,
            context->id, &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    result = amqp_handle_consume (context->socket, buffer,
        BUFFER_SIZE, context->id, confirm_tag, prefetch, no_local,
        unreliable, dest_name, identifier, 0, "", mime_type);
    TEST(result, amqp_handle_consume, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_send_message (
    handle_context_t *context,
    apr_byte_t out_of_band,
    apr_byte_t recovery,
    apr_byte_t streaming,
    const char* dest_name,
    apr_byte_t persistent,
    apr_byte_t priority,
    apr_uint32_t expiration,
    const char* mime_type,
    const char* encoding,
    const char* identifier,
    apr_size_t data_size,
    void *data,
    apr_byte_t async,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_uint16_t
        confirm_tag;
    
    confirm_tag = 0;
    if (lock)
        *lock = NULL;
    if (!async) {
        result = register_lock (context->connection, context->channel_id,
            context->id, &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    result = amqp_handle_send (context->socket, buffer,
        BUFFER_SIZE, context->id, confirm_tag, 0, out_of_band, recovery,
        streaming, dest_name, data_size, persistent, priority, expiration,
        mime_type, encoding, identifier, 0, "", data); 
    TEST(result, amqp_handle_send, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_get_message (
    handle_context_t *context,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    lock_list_item_t
        *new_item;
    
    /*  Allocate and fill in new lock item                                   */
    new_item = (lock_list_item_t*) malloc (sizeof (lock_list_item_t));
    result = register_lock (context->connection, context->channel_id,
        context->id, &(new_item->lock_id), lock);
    TEST(result, register_lock, buffer);
    /*  Append it to the end of the queue                                    */
    new_item->next = NULL;
    new_item->prev = context->last_lock;
    if (context->last_lock)
        context->last_lock->next = new_item;
    context->last_lock = new_item;
    if (!context->first_lock)
        context->first_lock = new_item;
    result = pair_lock_and_message (context);
    TEST(result, pair_lock_and_message, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_receive_message (
    handle_context_t *context,
    amqp_frame_t *message
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    message_list_item_t
        *new_item;
    browse_list_item_t
        *browse_request;
    browse_list_item_t
        **prev_browse_request;
    apr_byte_t
        processed;
    
    processed = 0;
    if (message->fields.handle_notify.delivered == 0) {
        /*  It is a reply to browse command                              */
        browse_request = context->browse_requests;
        prev_browse_request = &(context->browse_requests);
        while (browse_request) {
            if (browse_request->message_nbr ==
                  message->fields.handle_notify.message_nbr) {
                /*  It is a reply to sync browse command                 */
                /*  Make a copy of message and unsuspend the thread      */
                /*  that's waiting for it                                */
                new_item = (message_list_item_t*)
                    malloc (sizeof (message_list_item_t));
                memcpy ((void*) (new_item->buffer), (void*) message,
                    BUFFER_SIZE);
                new_item->frame = (amqp_frame_t*) (new_item->buffer);
                new_item->next = NULL;
                new_item->prev = NULL;
                result = release_lock (context->connection,
                    browse_request->lock_id, (void*) new_item);
                /*  Remove the request from the list                         */
                *prev_browse_request = browse_request->next;
                free ((void*) browse_request);
                processed = 1;
                break;
            }
            prev_browse_request = &(browse_request->next);
            browse_request = browse_request->next;
        }
    }
    if (!processed) {
        /*  Allocate and fill in new message item                            */
        new_item = (message_list_item_t*)
            malloc (sizeof (message_list_item_t));
        memcpy ((void*) (new_item->buffer), (void*) message, BUFFER_SIZE);
        new_item->frame = (amqp_frame_t*) (new_item->buffer);
        /*  Append it to the end of the queue                                */
        new_item->next = NULL;
        new_item->prev = context->last_message;
        if(context->last_message)
            context->last_message->next = new_item;
        context->last_message = new_item;
        if (!context->first_message)
            context->first_message = new_item;
        result = pair_lock_and_message (context);
        TEST(result, pair_lock_and_message, buffer)
    }
    return APR_SUCCESS;
}

inline static apr_status_t do_flow (
    handle_context_t *context,
    apr_byte_t pause,
    apr_byte_t async,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_uint16_t
        confirm_tag;
    
    confirm_tag = 0;
    if (lock)
        *lock = NULL;
    if (!async) {
        result = register_lock (context->connection, context->channel_id,
            context->id, &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    result = amqp_handle_flow (context->socket, buffer,
        BUFFER_SIZE, context->id, confirm_tag, pause); 
    TEST(result, amqp_handle_flow, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_cancel (
    handle_context_t *context,
    const char *dest_name,
    const char *identifier,
    apr_byte_t async,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_uint16_t
        confirm_tag;

    confirm_tag = 0;
    if (lock)
        *lock = NULL;
    if (!async) {
        result = register_lock (context->connection, context->channel_id,
            context->id, &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    result = amqp_handle_cancel (context->socket, buffer,
        BUFFER_SIZE, context->id, confirm_tag, dest_name, identifier); 
    TEST(result, amqp_handle_cancel, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_unget (
    handle_context_t *context,
    apr_uint32_t message_nbr,
    apr_byte_t async,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_uint16_t
        confirm_tag;
    
    confirm_tag = 0;
    if (lock)
        *lock = NULL;
    if (!async) {
        result = register_lock (context->connection, context->channel_id,
            context->id, &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    result = amqp_handle_unget (context->socket, buffer,
        BUFFER_SIZE, context->id, confirm_tag, message_nbr); 
    TEST(result, amqp_handle_unget, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_create_query (
    handle_context_t *context,
    apr_uint32_t message_nbr,
    const char *dest_name,
    const char *mime_type,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    query_t
        query;

    /*  Create query object                                                  */
    result = query_create (&query);
    TEST(result, query_create, buffer)
    context->query = query;
    /*  Initialise it                                                        */
    result = query_init (query, (handle_t) context, message_nbr, dest_name,
        mime_type, lock);
    TEST(result, query_init, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_index (
    handle_context_t *context,
    apr_uint32_t message_nbr,
    const char *message_list
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    
    if (!context->query) {
        printf ("HANDLE INDEX received when no HANDLE QUERY was issued.\n");
        exit(1);
    }
    result = query_index (context->query, message_nbr, message_list);
    TEST(result, query_index, buffer)
    context->query = NULL;
    /*  Release exclusive access to QUERY/INDEX dialogue                     */
#   ifdef AMQTRACE_LOCKS
        printf ("# Exclusive access to QUERY/INDEX dialogue released\n");
#   endif
    result = apr_thread_mutex_unlock (context->query_sync);
    TEST(result, apr_thread_mutex_unlock, buffer)
    return APR_SUCCESS;    
}

inline static apr_status_t do_browse (
    handle_context_t *context,
    apr_uint32_t message_nbr,
    apr_byte_t wait_for_message,
    apr_byte_t async,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    browse_list_item_t
        *new_item;
    apr_uint16_t
        confirm_tag;

    if (wait_for_message && async) {
        printf ("Waiting for message in amqp_browse cannot be combined "
            "with asynchronous mode.\n");
        /*  Alternative : In sync mode use wait_for_message as it is         */
        /*  In async mode ignore it and don't wait for message               */
        exit(1);
    }
    /*  In synchronous mode, register that thread is waiting for reply       */
    if (async) {
        if (lock)
            *lock = NULL;
        confirm_tag = 0;
    }
    else {
        result = register_lock (context->connection, context->channel_id,
            context->id, &confirm_tag, lock);
        TEST(result, register_lock, buffer)
    }
    if (wait_for_message) {
        /*  Register that we are waiting for the specific message            */
        new_item = (browse_list_item_t*)
            malloc (sizeof (browse_list_item_t));
        if (!new_item) {
            printf ("Not enough memory.\n");
            exit (1);
        }
        new_item->message_nbr = message_nbr;
        new_item->lock_id = confirm_tag;
        new_item->next = context->browse_requests;
        context->browse_requests = new_item;
    }
    /*  Send HANDLE BROWSE command                                           */
    result = amqp_handle_browse (context->socket, buffer, BUFFER_SIZE,
            context->id, confirm_tag, message_nbr);
    TEST(result, amqp_handle_browse, buffer)
    return APR_SUCCESS; 
}

inline static apr_status_t do_reply (
    handle_context_t *context,
    apr_uint16_t confirm_tag,
    apr_uint16_t reply_code,
    const char *reply_text
    )
{

    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    apr_byte_t
        processed;
    browse_list_item_t
        *browse_request;
    browse_list_item_t
        **prev_browse_request;
    
    /*  If this is negative response to HANDLE BROWSE, remove                */
    /*  corresponding item from browse request list and return NULL          */
    browse_request = context->browse_requests;
    prev_browse_request = &(context->browse_requests);
    processed = 0;
    while (browse_request)
    {
        if (browse_request->lock_id == confirm_tag) {
            *prev_browse_request = browse_request->next;
            free ((void*) browse_request);
            result = release_lock (context->connection, confirm_tag, NULL);
            TEST(result, release_lock, buffer)
            processed = 1;
            break;
        }
        prev_browse_request = &(browse_request->next);
        browse_request = browse_request->next;
    }
    if (!processed) {
        /*  Resume the thread waiting for this confirmation                  */
        result = release_lock (context->connection, confirm_tag,
            (void*) context);
        TEST(result, release_lock, buffer)
    }
    return APR_SUCCESS;
}

inline static apr_status_t do_terminate (
    handle_context_t *context,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];

    /*  Send HANDLE CLOSE                                                    */
    result = register_lock (context->connection, context->channel_id,
        0, &(context->shutdown_tag), lock);
    TEST(result, register_lock, buffer)
    result = amqp_handle_close (context->socket, buffer, BUFFER_SIZE,
        context->id, 200, "Handle close requested by client");
    TEST(result, amqp_handle_close, buffer)
    return APR_SUCCESS; 
}

inline static apr_status_t do_duplicate_terminate (
    handle_context_t *context,
    amqp_lock_t *lock
    )
{
    /*  This must be an explicit error; if not defined, it would fall        */
    /*  through default state and send HANDLE CLOSE once more                */
    printf ("Handle is already being terminated.\n");
    exit (1);
    return APR_SUCCESS; 
}

inline static apr_status_t do_client_requested_close (
    handle_context_t *context
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];

    /*  Release with error all threads waiting on channel, except the        */
    /*  requesting channel termination                                       */
    result = release_all_locks (context->connection, 0, context->id,
        context->shutdown_tag, AMQ_OBJECT_CLOSED);
    TEST(result, release_all_locks, buffer)
    /*  Resume client thread waiting for handle termination                  */
    result = release_lock (context->connection,
        context->shutdown_tag, NULL);
    TEST(result, release_lock, buffer)
    return APR_SUCCESS;    
}

inline static apr_status_t do_server_requested_close (
    handle_context_t *context
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];

    /*  Release with error all threads waiting on channel                    */
    result = release_all_locks (context->connection, 0, context->id,
        0, AMQ_OBJECT_CLOSED);
    TEST(result, release_all_locks, buffer)
    /*  Reply with HANDLE CLOSE                                              */
    result = amqp_handle_close (context->socket, buffer, BUFFER_SIZE,
        context->id, 200, "Handle close requested by client");
    TEST(result, amqp_handle_close, buffer)
    return APR_SUCCESS;    
}

/*****************************************************************************/
/*  State machine actions (Query)                                            */

inline static apr_status_t do_init_query (
    query_context_t *context,
    handle_t hndl,
    apr_uint32_t message_nbr,
    const char *dest_name,
    const char *mime_type,
    amqp_lock_t *lock
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];
    handle_context_t
        *handle = (handle_context_t*) hndl;

    /*  Save values that will be needed in future                            */
    context->connection = handle->connection;
    /*  Issue HANDLE QUERY command                                           */
    result = amqp_handle_query (handle->socket, buffer, BUFFER_SIZE,
        handle->id, message_nbr, dest_name, 0, "", mime_type);
    TEST(result, amqp_handle_query, buffer)
    /*  Create lock to wait for HANDLE INDEX                                 */
    result = register_lock (context->connection, handle->channel_id,
        handle->id, &(context->index_tag), lock);
    TEST(result, register_lock, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_index_query (
    query_context_t *context,
    apr_uint32_t message_nbr,
    const char *message_list
    )
{
    apr_status_t
        result;
    char
        buffer [BUFFER_SIZE];

    /*  Save result                                                          */
    context->results = (char*) malloc (strlen (message_list));
    if (!context->results) {
        printf ("Not enough memory.\n");
        exit (1);
    }
    strcpy (context->results, message_list);
    /*  Move cursor to the beginning of the message list                     */
    context->current = 0;
    context->range_current = 0;
    context->range_end = 0;
    /*  Resume thread waiting for the HANDLE INDEX                           */
    result = release_lock (context->connection, context->index_tag,
        (void*) context);
    TEST(result, release_lock, buffer)
    return APR_SUCCESS;
}

inline static apr_status_t do_get_query_result (
    query_context_t *context,
    apr_uint32_t *message_nbr
    )
{
    apr_uint16_t
        pos;
    apr_uint32_t
        out;

    /*  If we are inside a range specifier, return next value                */
    if (context->range_current < context->range_end) {
        out = context->range_current++;
    }
    else
    /*  If we are at the end of query result string, there are no more       */
    /*  results                                                              */
    if (context->results [context->current] == '\0') {
        free (context->results);
        out = 0;
    }
    else {
        /*  Parse next value from query result string                        */
        pos = context->current;
        context->range_current = 0;
        while (isdigit (context->results [pos]))
            context->range_current = context->range_current * 10 +
                context->results [pos++] - '0';

         /*  If we have a range specifier, parse the end value               */
        if (context->results [pos] == '-') {
            pos++;
            context->range_end = 0;
            while (isdigit (context->results [pos]))
                context->range_end = context->range_end * 10 +
                    context->results [pos++] - '0';
            context->range_end++;
        }
        /*  It's just a single value, left and round bound should be         */
        /*  the same                                                         */
        else
            context->range_end = context->range_current;

        /*  Ignore whitespace                                                */
        while (isspace (context->results [pos]))
            pos++;

        context->current = pos;
        out = context->range_current++;
    }
    if (message_nbr)
        *message_nbr = out;
    return APR_SUCCESS;
}

inline static apr_status_t do_restart_query (
    query_context_t *context
    )
{
    /*  Move cursor to the beginning of the message list                     */
    context->current = 0;
    context->range_current = 0;
    context->range_end = 0;
    return APR_SUCCESS;
}
