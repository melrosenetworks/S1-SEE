/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: grpc_adapter.h
 * Description: Header for GrpcIngestAdapter class that implements message ingestion
 *              via gRPC protocol. Receives SignalMessage records through gRPC service
 *              and stores them in spool partitions for later processing.
 */

#pragma once

#include "s1see/ingest/ingest_adapter.h"
#include "ingest.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <thread>
#include <atomic>

namespace s1see {
namespace ingest {

class GrpcIngestAdapter : public IngestAdapter, public IngestService::Service {
public:
    GrpcIngestAdapter(const std::string& listen_address);
    ~GrpcIngestAdapter();
    
    bool start() override;
    void stop() override;
    
    // gRPC service implementation
    grpc::Status Ingest(grpc::ServerContext* context,
                       grpc::ServerReaderWriter<IngestAck, SignalMessage>* stream) override;

private:
    std::string listen_address_;
    std::unique_ptr<grpc::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
};

} // namespace ingest
} // namespace s1see


