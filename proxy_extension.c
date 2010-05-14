/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proxy_extension.h"

/*
 * You load this proxy extension into memcached by using
 * the -X option:
 *
 * ./memcached -X .libs/proxy_extension.so -E .libs/default_engine.so
 */

static SERVER_HANDLE_V1 *g_server = NULL;

static const char *get_name(const void *cmd_cookie);
static bool accept_command(const void *cmd_cookie, void *cookie,
                           int argc, token_t *argv, size_t *ndata,
                           char **ptr);
static ENGINE_ERROR_CODE execute_command(const void *cmd_cookie, const void *cookie,
                                         int argc, token_t *argv,
                                         bool (*response_handler)(const void *cookie,
                                                                  int nbytes,
                                                                  const char *dta));
static void abort_command(const void *cmd_cookie, const void *cookie);

static EXTENSION_ASCII_PROTOCOL_DESCRIPTOR echo_descriptor = {
    .get_name = get_name,
    .accept = accept_command,
    .execute = execute_command,
    .abort = abort_command,
    .cookie = &echo_descriptor
};

static const char *get_name(const void *cmd_cookie) {
    return "echo";
}

static bool accept_command(const void *cmd_cookie, void *cookie,
                           int argc, token_t *argv, size_t *ndata,
                           char **ptr) {
    const char *cmd = argv[0].value;

    if (strcmp(cmd, "quit") == 0)
        return false;
    if (strcmp(cmd, "version") == 0)
        return false;
    if (strcmp(cmd, "verbosity") == 0)
        return false;
    if (strcmp(cmd, "stats") == 0)
        return false;

    // IDEA: We can do a hash on the key right here.
    // If we hash to ourselves (this memcached server), then we
    // can just return false to fall-through to the default memcached pathway.

    if (strcmp(cmd, "add") == 0) {
        int vlen;

        vlen = atoi(argv[4].value);
        return true;
    }

    return strcmp(cmd, "echo") == 0;
}

static ENGINE_ERROR_CODE execute_command(const void *cmd_cookie, const void *cookie,
                                         int argc, token_t *argv,
                                         bool (*response_handler)(const void *cookie,
                                                                  int nbytes,
                                                                  const char *dta)) {
    if (!response_handler(cookie, argv[0].length, argv[0].value)) {
        return ENGINE_FAILED;
    }

    for (int ii = 1; ii < argc; ++ii) {
        if (!response_handler(cookie, 2, " [") ||
            !response_handler(cookie, argv[ii].length, argv[ii].value) ||
            !response_handler(cookie, 1, "]")) {
            return ENGINE_FAILED;
        }
    }

    if (!response_handler(cookie, 2, "\r\n"))
        return ENGINE_FAILED;

    return ENGINE_SUCCESS;
}

static void abort_command(const void *cmd_cookie, const void *cookie)
{
    /* EMPTY */
}

#if defined (__SUNPRO_C) && (__SUNPRO_C >= 0x550)
__global
#elif defined __GNUC__
__attribute__ ((visibility("default")))
#endif
EXTENSION_ERROR_CODE memcached_extensions_initialize(const char *config,
                                                     GET_SERVER_API get_server_api) {
    if (g_server != NULL) {
        return EXTENSION_FATAL;
    }

    SERVER_HANDLE_V1 *server = get_server_api();
    if (server == NULL) {
        return EXTENSION_FATAL;
    }

    if (!server->extension->register_extension(EXTENSION_ASCII_PROTOCOL_BEFORE,
                                               &echo_descriptor)) {
        return EXTENSION_FATAL;
    }

    g_server = server;

    return EXTENSION_SUCCESS;
}
