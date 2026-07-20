#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

namespace py = pybind11;

std::string getPythonInvoker() {
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == nullptr) {
        return "python";
    }

    const py::object pythonFrame =
        py::reinterpret_borrow<py::object>(reinterpret_cast<PyObject *>(frame));
    const std::string scopeName = py::str(pythonFrame.attr("f_code").attr("co_name"));
    if (scopeName != "<module>") {
        return scopeName;
    }

    const py::dict globals = pythonFrame.attr("f_globals");
    const py::str fileKey("__file__");
    if (!globals.contains(fileKey) || globals[fileKey].is_none()) {
        return "python";
    }

    const std::string scriptPath = py::str(globals[fileKey]);
    if (scriptPath.empty() || (scriptPath.front() == '<' && scriptPath.back() == '>')) {
        return "python";
    }

    const std::string scriptName = std::filesystem::path(scriptPath).filename().string();
    return scriptName.empty() ? "python" : scriptName;
}

template <typename Adapter> class PythonLogger {
  public:
    PythonLogger(const std::string &message, const std::string &invoker, const std::string &file,
                 unsigned int line, long tid)
        : logger_(std::make_unique<TemplateLogger<Adapter>>(invoker.c_str(), file.c_str(), line, tid,
                                                            "%s", message.c_str())) {}

    void log(const std::string &message) {
        ensureOpen();
        logger_->log("%s", message.c_str());
    }

    void close() { logger_.reset(); }

    PythonLogger &enter() {
        ensureOpen();
        return *this;
    }

  private:
    void ensureOpen() const {
        if (!logger_) {
            throw std::runtime_error("logger is closed");
        }
    }

  protected:
    std::unique_ptr<TemplateLogger<Adapter>> logger_;
};

class PythonStlLogger : public PythonLogger<StlLogger> {
  public:
    using PythonLogger::PythonLogger;

    std::string getLogFileName() const { return StlLogger::getLogFileName(); }
};

class PythonStdoutLogger : public PythonLogger<StdoutLogger> {
  public:
    using PythonLogger::PythonLogger;

    static void print(const std::string &message, const std::optional<std::string> &invoker) {
        const std::string resolvedInvoker = invoker.value_or(getPythonInvoker());
        StdoutLogger::printLine(resolvedInvoker.c_str(), message.c_str());
    }

    static StdoutLoggerOptions getOptions() { return StdoutLogger::options; }
};

template <typename PythonLoggerType>
void bindLogger(py::class_<PythonLoggerType> &binding) {
    binding
        .def(py::init([](const std::string &message, std::optional<std::string> invoker,
                         const std::string &file, unsigned int line, std::optional<long> tid) {
                 return std::make_unique<PythonLoggerType>(
                     message, invoker.value_or(getPythonInvoker()), file, line,
                     tid.value_or(calf_current_tid()));
             }),
             py::arg("message") = "", py::arg("invoker") = py::none(),
             py::arg("file") = "<python>", py::arg("line") = 0, py::arg("tid") = py::none())
        .def("log", &PythonLoggerType::log, py::arg("message"))
        .def("close", &PythonLoggerType::close)
        .def(
            "__enter__",
            [](PythonLoggerType &logger) -> PythonLoggerType & {
                logger.enter();
                return logger;
            },
            py::return_value_policy::reference_internal)
        .def("__exit__",
             [](PythonLoggerType &logger, py::object, py::object, py::object) { logger.close(); });
}

PYBIND11_MODULE(_py_calf, module) {
    module.doc() = "Python bindings for the CALF STL and stdout loggers";

    py::class_<StdoutLoggerOptions>(module, "StdoutLoggerOptions")
        .def(py::init<>())
        .def_readwrite("color", &StdoutLoggerOptions::color)
        .def_readwrite("workflow_name", &StdoutLoggerOptions::workflowName)
        .def_readwrite("print_header", &StdoutLoggerOptions::printHeader)
        .def_readwrite("use_color", &StdoutLoggerOptions::useColor);

    py::class_<PythonStlLogger> stlLogger(module, "StlLogger");
    bindLogger(stlLogger);
    stlLogger
        .def("get_log_file_name", &PythonStlLogger::getLogFileName)
        .def_property_readonly("log_file_name", &PythonStlLogger::getLogFileName);

    py::class_<PythonStdoutLogger> stdoutLogger(module, "StdoutLogger");
    bindLogger(stdoutLogger);
    stdoutLogger
        .def_static("print", &PythonStdoutLogger::print, py::arg("message"),
                    py::arg("invoker") = py::none())
        .def_static("set_options", &StdoutLogger::setOptions, py::arg("options"))
        .def_static("get_options", &PythonStdoutLogger::getOptions);

    module.attr("CLI_LEVEL_RESET") = CAPIO_LOG_SERVER_CLI_LEVEL_RESET;
    module.attr("CLI_LEVEL_STATUS") = CALF_CLI_LEVEL_STATUS;
    module.attr("CLI_LEVEL_INFO") = CALF_CLI_LEVEL_INFO;
    module.attr("CLI_LEVEL_WARNING") = CALF_CLI_LEVEL_WARNING;
    module.attr("CLI_LEVEL_ERROR") = CALF_CLI_LEVEL_ERROR;
    module.attr("Logger") = module.attr("StlLogger");
}
