#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "hermes/Public/RuntimeConfig.h"
#include "hermes/VM/Runtime.h"
#include "napi/hermes_napi.h"

static std::optional<std::string> readFile(const char *path) {
  std::ifstream fileStream(path);
  if (!fileStream) {
    std::cerr << path << ": error opening file" << std::endl;
    return std::nullopt;
  }
  std::stringstream stringStream;
  stringStream << fileStream.rdbuf();
  return stringStream.str();
}

static napi_value hostAdd(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  if (napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr) != napi_ok) {
    return nullptr;
  }
  std::vector<napi_value> argv(argc);
  if (argc > 0 &&
      napi_get_cb_info(env, info, &argc, argv.data(), nullptr, nullptr) !=
          napi_ok) {
    return nullptr;
  }

  double sum = 0.0;
  for (size_t i = 0; i < argc; ++i) {
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, argv[i], &type) != napi_ok) {
      return nullptr;
    }
    if (type != napi_number) {
      napi_throw_type_error(env, nullptr, "Argument must be a number");
      return nullptr;
    }
    double val = 0.0;
    if (napi_get_value_double(env, argv[i], &val) != napi_ok) {
      return nullptr;
    }
    sum += val;
  }

  napi_value result = nullptr;
  napi_create_double(env, sum, &result);
  return result;
}

static void printAndClearException(napi_env env) {
  bool hasPending = false;
  if (napi_is_exception_pending(env, &hasPending) != napi_ok || !hasPending) {
    return;
  }
  napi_value err = nullptr;
  napi_get_and_clear_last_exception(env, &err);

  napi_value str = nullptr;
  if (napi_coerce_to_string(env, err, &str) == napi_ok) {
    size_t len = 0;
    napi_get_value_string_utf8(env, str, nullptr, 0, &len);
    std::string buf(len + 1, '\0');
    napi_get_value_string_utf8(env, str, &buf[0], len + 1, &len);
    buf.resize(len);
    std::cerr << "Error: " << buf << std::endl;
  } else {
    napi_value ignored = nullptr;
    napi_get_and_clear_last_exception(env, &ignored);
    std::cerr << "Error: (could not convert exception to string)" << std::endl;
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <path-to-js-file>" << std::endl;
    return 1;
  }

  const char *jsPath = argv[1];
  auto optCode = readFile(jsPath);
  if (!optCode) {
    return 1;
  }

  auto runtimeConfig =
      hermes::vm::RuntimeConfig::Builder().withIntl(false).build();
  auto runtime = hermes::vm::Runtime::create(runtimeConfig);
  napi_env env = hermes_napi_create_env(runtime.get(), nullptr);

  napi_handle_scope scope = nullptr;
  napi_open_handle_scope(env, &scope);

  napi_value global = nullptr;
  napi_value addFn = nullptr;
  napi_get_global(env, &global);
  napi_create_function(env, "add", NAPI_AUTO_LENGTH, hostAdd, nullptr, &addFn);
  napi_set_named_property(env, global, "add", addFn);

  hermes_run_script_flags rflags{};
  rflags.struct_size = sizeof(rflags);
  napi_value result = nullptr;
  int status = 0;
  if (hermes_run_script(
          env,
          reinterpret_cast<const uint8_t *>(optCode->c_str()),
          optCode->size() + 1,
          nullptr,
          nullptr,
          jsPath,
          &rflags,
          &result) != napi_ok) {
    printAndClearException(env);
    status = 1;
  }

  napi_close_handle_scope(env, scope);
  return status;
}
