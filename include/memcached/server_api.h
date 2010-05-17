/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef MEMCACHED_SERVER_API_H
#define MEMCACHED_SERVER_API_H
#include <inttypes.h>

#include <memcached/types.h>
#include <memcached/config_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct {
        /**
         * The current time.
         */
        rel_time_t (*get_current_time)(void);

        /**
         * Get the relative time for the given time_t value.
         */
        rel_time_t (*realtime)(const time_t exptime);

        /**
         * Get the server's version number.
         *
         * @return the server's version number
         */
        const char* (*server_version)(void);

        /**
         * Generate a simple hash value of a piece of data.
         *
         * @param data pointer to data to hash
         * @param size size of the data to generate the hash value of
         * @param seed an extra seed value for the hash function
         * @return hash value of the data.
         */
        uint32_t (*hash)(const void *data, size_t size, uint32_t seed);

        /**
         * parser config options
         */
        int (*parse_config)(const char *str, struct config_item items[], FILE *error);

        /**
         * Get the auth data for the connection associated with the
         * given cookie.
         *
         * @param cookie The cookie provided by the frontend
         * @param data Pointer to auth_data_t structure for returning the values
         *
         */
        void (*get_auth_data)(const void *cookie, auth_data_t *data);

        /**
         * Store engine-specific session data on the given cookie.
         *
         * The engine interface allows for a single item to be
         * attached to the connection that it can use to track
         * connection-specific data throughout duration of the
         * connection.
         *
         * @param cookie The cookie provided by the frontend
         * @param engine_data pointer to opaque data
         */
        void (*store_engine_specific)(const void *cookie, void *engine_data);

        /**
         * Retrieve engine-specific session data for the given cookie.
         *
         * @param cookie The cookie provided by the frontend
         *
         * @return the data provied by store_engine_specific or NULL
         *         if none was provided
         */
        void *(*get_engine_specific)(const void *cookie);

        /**
         * Retrieve socket file descriptor of the session for the given cookie.
         *
         * @param cookie The cookie provided by the frontend
         *
         * @return the socket file descriptor of the session for the given cookie.
         */
        int (*get_socket_fd)(const void *cookie);

        /**
         * Let a connection know that IO has completed.
         * @param cookie cookie representing the connection
         * @param status the status for the io operation
         */
        void (*notify_io_complete)(const void *cookie,
                                   ENGINE_ERROR_CODE status);

    } SERVER_CORE_API;

    typedef struct {
        /**
         * Allocate and deallocate thread-specific stats arrays for
         * engine-maintained separate stats.
         */
        void *(*new_stats)(void);
        void (*release_stats)(void*);

        /**
         * Tell the server we've evicted an item.
         */
        void (*evicting)(const void *cookie,
                         const void *key,
                         int nkey);
    } SERVER_STAT_API;

#ifdef __WIN32__
#undef interface
#endif

    typedef SERVER_HANDLE_V1* (*GET_SERVER_API)(void);

#ifdef __cplusplus
}
#endif

#endif
