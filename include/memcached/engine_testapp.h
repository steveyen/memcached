#ifndef _ENGINE_TESTAPP_H
#define    _ENGINE_TESTAPP_H

#include <memcached/engine.h>

#ifdef    __cplusplus
extern "C" {
#endif

#ifndef PUBLIC

#if defined (__SUNPRO_C) && (__SUNPRO_C >= 0x550)
#define PUBLIC __global
#elif defined __GNUC__
#define PUBLIC __attribute__ ((visibility("default")))
#else
#define PUBLIC
#endif

#endif

enum test_result {
    SUCCESS = 11,
    FAIL = 13,
    DIED = 14,
    CORE = 15,
    PENDING = 19
};

typedef struct test {
    const char *name;
    enum test_result(*tfun)(ENGINE_HANDLE *, ENGINE_HANDLE_V1 *);
    bool(*test_setup)(ENGINE_HANDLE *, ENGINE_HANDLE_V1 *);
    bool(*test_teardown)(ENGINE_HANDLE *, ENGINE_HANDLE_V1 *);
    const char *cfg;
} engine_test_t;

typedef engine_test_t* (*GET_TESTS)();

typedef bool (*SETUP_SUITE)();

typedef bool (*TEARDOWN_SUITE)();

#ifdef    __cplusplus
}
#endif

#endif    /* _ENGINE_TESTAPP_H */
