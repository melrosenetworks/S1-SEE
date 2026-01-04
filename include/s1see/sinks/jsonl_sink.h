/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: jsonl_sink.h
 * Description: Header for JSONLSink class that emits events to JSONL (JSON Lines)
 *              files. Converts Event protobuf messages to JSON format and writes
 *              them as newline-delimited JSON records to a file.
 */

#pragma once

#include "s1see/sinks/sink.h"
#include <string>
#include <fstream>

namespace s1see {
namespace sinks {

class JSONLSink : public Sink {
public:
    explicit JSONLSink(const std::string& file_path);
    ~JSONLSink();
    
    bool emit(const Event& event) override;
    void flush() override;
    void close() override;

private:
    std::string file_path_;
    std::ofstream file_;
    bool opened_;
};

} // namespace sinks
} // namespace s1see


