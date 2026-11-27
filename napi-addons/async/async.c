#include "hermes/napi/node_api.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
  napi_async_work work;
  napi_deferred deferred;
  double a;
  double b;
  double sum;
} AsyncAddCtx;

static void asyncAdd_execute(napi_env env, void *data) {
  (void)env;
  AsyncAddCtx *ctx = (AsyncAddCtx *)data;
  ctx->sum = ctx->a + ctx->b;
}

static void asyncAdd_complete(napi_env env, napi_status status, void *data) {
  AsyncAddCtx *ctx = (AsyncAddCtx *)data;

  if (status == napi_ok) {
    napi_value result = NULL;
    napi_create_double(env, ctx->sum, &result);
    napi_resolve_deferred(env, ctx->deferred, result);
  } else {
    napi_value msg = NULL;
    napi_value err = NULL;
    napi_create_string_utf8(env, "asyncAdd cancelled", NAPI_AUTO_LENGTH, &msg);
    napi_create_error(env, NULL, msg, &err);
    napi_reject_deferred(env, ctx->deferred, err);
  }

  napi_delete_async_work(env, ctx->work);
  free(ctx);
}

static napi_value asyncAdd(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok) {
    return NULL;
  }

  if (argc < 2) {
    napi_throw_type_error(env, NULL, "asyncAdd(a, b) requires two arguments");
    return NULL;
  }

  for (size_t i = 0; i < 2; ++i) {
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, argv[i], &type) != napi_ok) {
      return NULL;
    }
    if (type != napi_number) {
      napi_throw_type_error(env, NULL, "Arguments must be numbers");
      return NULL;
    }
  }

  AsyncAddCtx *ctx = (AsyncAddCtx *)calloc(1, sizeof(AsyncAddCtx));
  if (ctx == NULL) {
    napi_throw_error(env, NULL, "Out of memory");
    return NULL;
  }

  napi_get_value_double(env, argv[0], &ctx->a);
  napi_get_value_double(env, argv[1], &ctx->b);

  napi_value promise = NULL;
  if (napi_create_promise(env, &ctx->deferred, &promise) != napi_ok) {
    free(ctx);
    return NULL;
  }

  napi_value resource_name = NULL;
  napi_create_string_utf8(env, "asyncAdd", NAPI_AUTO_LENGTH, &resource_name);

  if (napi_create_async_work(
          env,
          NULL,
          resource_name,
          asyncAdd_execute,
          asyncAdd_complete,
          ctx,
          &ctx->work) != napi_ok) {
    free(ctx);
    return NULL;
  }

  if (napi_queue_async_work(env, ctx->work) != napi_ok) {
    napi_delete_async_work(env, ctx->work);
    free(ctx);
    napi_throw_error(env, NULL, "Failed to queue async work");
    return NULL;
  }

  return promise;
}

typedef struct {
  napi_threadsafe_function tsfn;
  int32_t count;
} TsfnCtx;

static void tsfn_call_js(
    napi_env env,
    napi_value js_callback,
    void *context,
    void *data) {
  (void)context;
  if (env == NULL || js_callback == NULL) {
    return;
  }

  napi_value recv = NULL;
  napi_value arg = NULL;
  if (napi_get_undefined(env, &recv) != napi_ok ||
      napi_create_int32(env, (int32_t)(intptr_t)data, &arg) != napi_ok) {
    return;
  }
  napi_call_function(env, recv, js_callback, 1, &arg, NULL);
}

static void *tsfn_worker(void *arg) {
  TsfnCtx *ctx = (TsfnCtx *)arg;
  for (int32_t i = 1; i <= ctx->count; ++i) {
    napi_call_threadsafe_function(
        ctx->tsfn, (void *)(intptr_t)i, napi_tsfn_blocking);
  }
  napi_release_threadsafe_function(ctx->tsfn, napi_tsfn_release);
  free(ctx);
  return NULL;
}

static napi_value emitFromThread(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok) {
    return NULL;
  }

  if (argc < 2) {
    napi_throw_type_error(
        env, NULL, "emitFromThread(count, callback) requires two arguments");
    return NULL;
  }

  napi_valuetype countType = napi_undefined;
  napi_valuetype cbType = napi_undefined;
  if (napi_typeof(env, argv[0], &countType) != napi_ok ||
      napi_typeof(env, argv[1], &cbType) != napi_ok) {
    return NULL;
  }
  if (countType != napi_number || cbType != napi_function) {
    napi_throw_type_error(
        env, NULL, "Expected (number, function)");
    return NULL;
  }

  int32_t count = 0;
  napi_get_value_int32(env, argv[0], &count);
  if (count < 0) {
    count = 0;
  }

  TsfnCtx *ctx = (TsfnCtx *)calloc(1, sizeof(TsfnCtx));
  if (ctx == NULL) {
    napi_throw_error(env, NULL, "Out of memory");
    return NULL;
  }
  ctx->count = count;

  napi_value resource_name = NULL;
  napi_create_string_utf8(
      env, "emitFromThread", NAPI_AUTO_LENGTH, &resource_name);

  if (napi_create_threadsafe_function(
          env,
          argv[1],
          NULL,
          resource_name,
          0,
          1,
          NULL,
          NULL,
          NULL,
          tsfn_call_js,
          &ctx->tsfn) != napi_ok) {
    free(ctx);
    napi_throw_error(env, NULL, "Failed to create threadsafe function");
    return NULL;
  }

  pthread_t thread;
  if (pthread_create(&thread, NULL, tsfn_worker, ctx) != 0) {
    napi_release_threadsafe_function(ctx->tsfn, napi_tsfn_abort);
    free(ctx);
    napi_throw_error(env, NULL, "Failed to create worker thread");
    return NULL;
  }
  pthread_detach(thread);

  return NULL;
}

NAPI_MODULE_INIT() {
  napi_value fn = NULL;

  napi_create_function(
      env, "asyncAdd", NAPI_AUTO_LENGTH, asyncAdd, NULL, &fn);
  napi_set_named_property(env, exports, "asyncAdd", fn);

  napi_create_function(
      env, "emitFromThread", NAPI_AUTO_LENGTH, emitFromThread, NULL, &fn);
  napi_set_named_property(env, exports, "emitFromThread", fn);

  return exports;
}
