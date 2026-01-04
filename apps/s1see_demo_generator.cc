/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: s1see_demo_generator.cc
 * Description: Demo/test application for generating sample SignalMessage records
 *              and sending them to the spooler daemon via gRPC. Used for testing
 *              and demonstration purposes to simulate message ingestion.
 */

#include "ingest.grpc.pb.h"
#include "signal_message.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;
using s1see::SignalMessage;
using s1see::IngestService;
using s1see::IngestAck;

int main(int argc, char** argv) {
    std::string server_address = "localhost:50051";
    int num_messages = 10;
    
    if (argc > 1) {
        server_address = argv[1];
    }
    if (argc > 2) {
        num_messages = std::stoi(argv[2]);
    }
    
    std::cout << "S1-SEE Demo Generator" << std::endl;
    std::cout << "Connecting to: " << server_address << std::endl;
    std::cout << "Sending " << num_messages << " messages" << std::endl;
    
    // Create channel
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    auto stub = IngestService::NewStub(channel);
    
    // Create stream
    ClientContext context;
    std::shared_ptr<ClientReaderWriter<SignalMessage, IngestAck>> stream(
        stub->Ingest(&context));
    
    // Send messages
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Generate sample PDUs
    std::vector<std::vector<uint8_t>> sample_pdus = {
        {0x00, 0x01, 0x02, 0x03, 0x04}, // HandoverRequest (stub)
        {0x01, 0x05, 0x06, 0x07, 0x08}, // HandoverNotify (stub)
        {0x02, 0x09, 0x0A, 0x0B, 0x0C}, // InitialUEMessage (stub)
    };
    
    for (int i = 0; i < num_messages; ++i) {
        SignalMessage message;
        message.set_ts_capture(now + i * 1000000); // 1ms apart
        message.set_ts_ingest(now + i * 1000000);
        message.set_source_id("demo_source");
        message.set_direction(SignalMessage::UPLINK);
        message.set_source_sequence(i);
        message.set_transport_meta("{\"demo\": true}");
        message.set_payload_type(SignalMessage::RAW_BYTES);
        
        // Use sample PDU (round-robin)
        const auto& pdu = sample_pdus[i % sample_pdus.size()];
        message.set_raw_bytes(pdu.data(), pdu.size());
        
        // Send message
        if (!stream->Write(message)) {
            std::cerr << "Failed to write message " << i << std::endl;
            break;
        }
        
        // Read ack
        IngestAck ack;
        if (stream->Read(&ack)) {
            if (ack.success()) {
                std::cout << "Message " << i << " acked: p=" << ack.spool_offset().partition()
                         << " offset=" << ack.spool_offset().offset() << std::endl;
            } else {
                std::cerr << "Message " << i << " failed: " << ack.error_message() << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Close stream
    stream->WritesDone();
    Status status = stream->Finish();
    
    if (!status.ok()) {
        std::cerr << "Stream failed: " << status.error_message() << std::endl;
        return 1;
    }
    
    std::cout << "Demo complete. Sent " << num_messages << " messages." << std::endl;
    return 0;
}

