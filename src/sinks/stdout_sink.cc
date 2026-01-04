/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: stdout_sink.cc
 * Description: Implementation of StdoutSink class for emitting events to standard output.
 *              Converts Event protobuf messages to JSON format and writes them
 *              to stdout, providing a simple output mechanism for event processing.
 */

#include "s1see/sinks/stdout_sink.h"
#include "event.pb.h"
#include <google/protobuf/util/json_util.h>
#include <iostream>

namespace s1see {
namespace sinks {

bool StdoutSink::emit(const Event& event) {
    std::string json;
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    
    auto status = google::protobuf::util::MessageToJsonString(event, &json, options);
    if (!status.ok()) {
        std::cerr << "Failed to serialize event to JSON: " << status.message() << std::endl;
        return false;
    }
    
    std::cout << json << std::endl;
    return true;
}

} // namespace sinks
} // namespace s1see


