#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <hermes/hermes.h>

using namespace facebook;

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

static jsi::Value hostAdd(
    jsi::Runtime &rt,
    const jsi::Value &,
    const jsi::Value *args,
    size_t count) {
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    if (!args[i].isNumber()) {
      throw jsi::JSError(rt, "Argument must be a number");
    }
    sum += args[i].asNumber();
  }
  return sum;
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
  auto runtime = facebook::hermes::makeHermesRuntime(runtimeConfig);

  runtime->global().setProperty(
      *runtime,
      "add",
      jsi::Function::createFromHostFunction(
          *runtime, jsi::PropNameID::forAscii(*runtime, "add"), 2, hostAdd));

  int status = 0;
  try {
    runtime->evaluateJavaScript(
        std::make_unique<jsi::StringBuffer>(std::move(*optCode)), jsPath);
  } catch (jsi::JSError &e) {
    std::cerr << "JS Exception: " << e.getStack() << std::endl;
    status = 1;
  } catch (jsi::JSIException &e) {
    std::cerr << "JSI Exception: " << e.what() << std::endl;
    status = 1;
  }

  return status;
}
