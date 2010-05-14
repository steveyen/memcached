/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef PROXY_EXTENSION_H
#define PROXY_EXTENSION_H

#include <memcached/engine.h>

EXTENSION_ERROR_CODE memcached_extensions_initialize(const char *config,
                                                     GET_SERVER_API get_server_api);

#endif
