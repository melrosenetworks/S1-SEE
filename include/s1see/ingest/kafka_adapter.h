/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: kafka_adapter.h
 * Description: Header for KafkaIngestAdapter class that implements message ingestion
 *              via Apache Kafka messaging protocol. Receives SignalMessage records
 *              through Kafka and stores them in spool partitions (skeleton implementation).
 */

#pragma once

#include "s1see/ingest/ingest_adapter.h"
#include <string>
#include <memory>

namespace s1see {
namespace ingest {

// Kafka adapter skeleton
// For production, integrate with librdkafka or similar
class KafkaIngestAdapter : public IngestAdapter {
public:
    KafkaIngestAdapter(const std::string& brokers, const std::string& topic);
    ~KafkaIngestAdapter();
    
    bool start() override;
    void stop() override;

private:
    std::string brokers_;
    std::string topic_;
    // TODO: Add Kafka consumer handle
};

} // namespace ingest
} // namespace s1see


