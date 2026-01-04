#include "s1see/ingest/nats_adapter.h"
#include <iostream>

namespace s1see {
namespace ingest {

NATSIngestAdapter::NATSIngestAdapter(const std::string& servers, const std::string& subject)
    : servers_(servers), subject_(subject) {
}

NATSIngestAdapter::~NATSIngestAdapter() {
    stop();
}

bool NATSIngestAdapter::start() {
    // TODO: Initialize NATS connection
    std::cerr << "NATSIngestAdapter: Stub implementation - integrate nats.c for production" << std::endl;
    return false;
}

void NATSIngestAdapter::stop() {
    // TODO: Cleanup NATS connection
}

} // namespace ingest
} // namespace s1see


