/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: nats_adapter.h
 * Description: Header for NATSIngestAdapter class that implements message ingestion
 *              via NATS messaging protocol. Receives SignalMessage records through
 *              NATS and stores them in spool partitions (skeleton implementation).
 */

#pragma once

#include "s1see/ingest/ingest_adapter.h"
#include <string>

namespace s1see {
namespace ingest {

// NATS adapter skeleton
class NATSIngestAdapter : public IngestAdapter {
public:
    NATSIngestAdapter(const std::string& servers, const std::string& subject);
    ~NATSIngestAdapter();
    
    bool start() override;
    void stop() override;

private:
    std::string servers_;
    std::string subject_;
    // TODO: Add NATS connection handle
};

} // namespace ingest
} // namespace s1see


