/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: jsonl_sink.cc
 * Description: Implementation of JSONLSink class for emitting events to JSONL (JSON Lines)
 *              files. Converts Event protobuf messages to JSON format and writes
 *              them as newline-delimited JSON records to a file for persistent storage.
 */

#include "s1see/sinks/jsonl_sink.h"
#include "event.pb.h"
#include <google/protobuf/util/json_util.h>
#include <iostream>

namespace s1see {
namespace sinks {

JSONLSink::JSONLSink(const std::string& file_path)
    : file_path_(file_path), opened_(false) {
    file_.open(file_path_, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "Failed to open JSONL file: " << file_path_ << std::endl;
    } else {
        opened_ = true;
    }
}

JSONLSink::~JSONLSink() {
    close();
}

bool JSONLSink::emit(const Event& event) {
    if (!opened_ || !file_.is_open()) {
        return false;
    }
    
    std::string json;
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    
    auto status = google::protobuf::util::MessageToJsonString(event, &json, options);
    if (!status.ok()) {
        std::cerr << "Failed to serialize event to JSON: " << status.message() << std::endl;
        return false;
    }
    
    file_ << json << "\n";
    return true;
}

void JSONLSink::flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

void JSONLSink::close() {
    if (file_.is_open()) {
        file_.close();
        opened_ = false;
    }
}

} // namespace sinks
} // namespace s1see


