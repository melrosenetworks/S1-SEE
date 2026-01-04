/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: sink.h
 * Description: Header for base Sink class and concrete implementations (StdoutSink,
 *              JSONLSink) for emitting events. Provides interface for event output
 *              mechanisms with different formats (stdout, JSONL files).
 */

#pragma once

#include "event.pb.h"
#include <string>
#include <memory>
#include <vector>

namespace s1see {
namespace sinks {

// Base interface for event sinks
class Sink {
public:
    virtual ~Sink() = default;
    
    // Emit an event
    virtual bool emit(const Event& event) = 0;
    
    // Emit multiple events
    virtual bool emit_batch(const std::vector<Event>& events) {
        bool all_ok = true;
        for (const auto& event : events) {
            if (!emit(event)) {
                all_ok = false;
            }
        }
        return all_ok;
    }
    
    // Flush any buffered events
    virtual void flush() {}
    
    // Close the sink
    virtual void close() {}
};

} // namespace sinks
} // namespace s1see


