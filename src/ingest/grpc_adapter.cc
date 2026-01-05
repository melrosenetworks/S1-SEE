#include "s1see/ingest/grpc_adapter.h"
#include <grpcpp/server_builder.h>
#include <iostream>

namespace s1see {
namespace ingest {

GrpcIngestAdapter::GrpcIngestAdapter(const std::string& listen_address)
    : listen_address_(listen_address) {
}

GrpcIngestAdapter::~GrpcIngestAdapter() {
    stop();
}

bool GrpcIngestAdapter::start() {
    if (running_.exchange(true)) {
        return false; // Already running
    }

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_address_, grpc::InsecureServerCredentials());
    builder.RegisterService(this);
    
    server_ = builder.BuildAndStart();
    if (!server_) {
        running_ = false;
        return false;
    }

    server_thread_ = std::thread([this]() {
        server_->Wait();
    });

    return true;
}

void GrpcIngestAdapter::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (server_) {
        server_->Shutdown();
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

grpc::Status GrpcIngestAdapter::Ingest(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<IngestAck, SignalMessage>* stream) {
    
    SignalMessage message;
    int64_t sequence = 0;
    
    while (stream->Read(&message)) {
        sequence++;
        
        try {
            // Set ingest timestamp if not set
            if (message.ts_ingest() == 0) {
                auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                message.set_ts_ingest(now);
            }
            
            // Append to spool
            auto [partition, offset] = append_to_spool(message);
            
            // Send ack
            IngestAck ack;
            ack.set_message_id(message.source_id() + ":" + std::to_string(message.source_sequence()));
            ack.set_sequence(sequence);
            ack.mutable_spool_offset()->set_partition(partition);
            ack.mutable_spool_offset()->set_offset(offset);
            ack.set_success(true);
            
            if (!stream->Write(ack)) {
                return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to send ack");
            }
        } catch (const std::exception& e) {
            IngestAck ack;
            ack.set_sequence(sequence);
            ack.set_success(false);
            ack.set_error_message(e.what());
            stream->Write(ack);
            return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
        }
    }
    
    return grpc::Status::OK;
}

} // namespace ingest
} // namespace s1see




