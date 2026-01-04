/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: amqp_adapter.h
 * Description: Header for AMQPIngestAdapter class that implements message ingestion
 *              via AMQP messaging protocol. Receives SignalMessage records through
 *              AMQP and stores them in spool partitions (skeleton implementation).
 */

#pragma once

#include "s1see/ingest/ingest_adapter.h"
#include <string>

namespace s1see {
namespace ingest {

// AMQP adapter skeleton
class AMQPIngestAdapter : public IngestAdapter {
public:
    AMQPIngestAdapter(const std::string& connection_string, const std::string& queue);
    ~AMQPIngestAdapter();
    
    bool start() override;
    void stop() override;

private:
    std::string connection_string_;
    std::string queue_;
    // TODO: Add AMQP connection handle
};

} // namespace ingest
} // namespace s1see


