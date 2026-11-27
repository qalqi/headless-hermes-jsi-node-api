#include "hermes/napi/node_api.h"

static napi_value add(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok) {
    return NULL;
  }

  if (argc < 2) {
    napi_throw_type_error(env, NULL, "add(a, b) requires two arguments");
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

  double a = 0.0;
  double b = 0.0;
  if (napi_get_value_double(env, argv[0], &a) != napi_ok ||
      napi_get_value_double(env, argv[1], &b) != napi_ok) {
    return NULL;
  }

  napi_value result = NULL;
  napi_create_double(env, a + b, &result);
  return result;
}

static napi_value hello(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value result = NULL;
  napi_create_string_utf8(env, "hello from Node-API", NAPI_AUTO_LENGTH, &result);
  return result;
}

NAPI_MODULE_INIT() {
  napi_value add_fn = NULL;
  napi_value hello_fn = NULL;

  napi_create_function(env, "add", NAPI_AUTO_LENGTH, add, NULL, &add_fn);
  napi_set_named_property(env, exports, "add", add_fn);

  napi_create_function(env, "hello", NAPI_AUTO_LENGTH, hello, NULL, &hello_fn);
  napi_set_named_property(env, exports, "hello", hello_fn);

  return exports;
}
