/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: ingest_adapter.h
 * Description: Header for base IngestAdapter class and concrete implementations
 *              (NATS, Kafka, AMQP, gRPC) for message ingestion. Provides interface
 *              for receiving SignalMessage records from various messaging protocols
 *              and storing them in spool partitions.
 */

#pragma once

#include "signal_message.pb.h"
#include "s1see/spool/spool.h"
#include <functional>
#include <memory>
#include <string>

namespace s1see {
namespace ingest {

// Base interface for all ingest adapters
class IngestAdapter {
public:
    using AckCallback = std::function<void(const std::string& message_id, bool success, const std::string& error)>;
    
    virtual ~IngestAdapter() = default;
    
    // Start the adapter (non-blocking)
    virtual bool start() = 0;
    
    // Stop the adapter
    virtual void stop() = 0;
    
    // Set the spool to write to
    virtual void set_spool(std::shared_ptr<spool::Spool> spool) {
        spool_ = spool;
    }

protected:
    std::shared_ptr<spool::Spool> spool_;
    
    // Helper: append to spool and return offset
    std::pair<int32_t, int64_t> append_to_spool(const SignalMessage& message) {
        if (!spool_) {
            throw std::runtime_error("Spool not set");
        }
        return spool_->append(message);
    }
};

} // namespace ingest
} // namespace s1see


