/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: stdout_sink.h
 * Description: Header for StdoutSink class that emits events to standard output.
 *              Converts Event protobuf messages to JSON format and writes them
 *              to stdout for simple event output.
 */

#pragma once

#include "s1see/sinks/sink.h"
#include <iostream>

namespace s1see {
namespace sinks {

class StdoutSink : public Sink {
public:
    bool emit(const Event& event) override;
};

} // namespace sinks
} // namespace s1see


