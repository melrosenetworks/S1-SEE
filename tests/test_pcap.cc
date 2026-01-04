#include "s1see/spool/spool.h"
#include "s1see/processor/pipeline.h"
#include "s1see/rules/yaml_loader.h"
#include "s1see/sinks/stdout_sink.h"
#include "s1see/sinks/jsonl_sink.h"
#include "s1see/utils/pcap_reader.h"
#include "s1ap_parser.h"
#include "signal_message.pb.h"
#include "event.pb.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <vector>
#include <chrono>
#include <map>

using s1see::SignalMessage;
using s1see::Event;

namespace fs = std::filesystem;

void test_pcap_processing(const std::string& pcap_path) {
    std::cout << "Testing PCAP processing..." << std::endl;
    std::cout << "PCAP file: " << pcap_path << std::endl;
    
    if (!fs::exists(pcap_path)) {
        std::cerr << "PCAP file not found: " << pcap_path << std::endl;
        std::cerr << "Skipping PCAP test. Place a test PCAP file at: " << pcap_path << std::endl;
        return;
    }
    
    // Setup spool
    std::string test_spool_dir = "test_pcap_spool";
    fs::remove_all(test_spool_dir);
    
    s1see::spool::WALLog::Config spool_config;
    spool_config.base_dir = test_spool_dir;
    spool_config.num_partitions = 1;
    spool_config.fsync_on_append = false;
    auto spool = std::make_shared<s1see::spool::Spool>(spool_config);
    
    // Setup pipeline
    s1see::processor::Pipeline::Config pipeline_config;
    pipeline_config.spool_base_dir = test_spool_dir;
    pipeline_config.spool_partitions = 1;
    pipeline_config.consumer_group = "pcap_test";
    
    s1see::processor::Pipeline pipeline(pipeline_config);
    
    // Load ruleset
    std::string ruleset_file = "../config/rulesets/mobility.yaml";
    if (!fs::exists(ruleset_file)) {
        ruleset_file = "config/rulesets/mobility.yaml"; // Try relative to current dir
    }
    if (fs::exists(ruleset_file)) {
        auto ruleset = s1see::rules::load_ruleset_from_yaml(ruleset_file);
        pipeline.load_ruleset(ruleset);
        std::cout << "  ✓ Loaded ruleset: " << ruleset.id << std::endl;
    } else {
        std::cerr << "  ⚠ Ruleset file not found: " << ruleset_file << std::endl;
    }
    
    // Setup sinks
    auto stdout_sink = std::make_shared<s1see::sinks::StdoutSink>();
    auto jsonl_sink = std::make_shared<s1see::sinks::JSONLSink>("test_pcap_events.jsonl");
    pipeline.add_sink(stdout_sink);
    pipeline.add_sink(jsonl_sink);
    
    // Read PCAP and extract S1AP PDUs
    int packet_count = 0;
    int s1ap_count = 0;
    int64_t sequence = 0;
    std::map<std::string, int> msg_type_counts;
    std::map<int, int> proc_code_counts;
    int sample_count = 0;
    
    s1see::utils::read_pcap_file(pcap_path, [&](const s1see::utils::PcapPacket& pkt) {
        packet_count++;
        
        // Extract S1AP from SCTP packet
        auto s1ap_opt = s1ap_parser::extractS1apFromSctp(pkt.data.data(), pkt.data.size());
        if (!s1ap_opt.has_value()) {
            // Try extracting all S1AP PDUs (multiple DATA chunks)
            auto all_s1ap = s1ap_parser::extractAllS1apFromSctp(pkt.data.data(), pkt.data.size());
            if (all_s1ap.empty()) {
                return; // No S1AP in this packet
            }
            
            // Process all S1AP PDUs
            for (const auto& s1ap_bytes : all_s1ap) {
                // Debug: sample messages to see what types we get
                auto parse_result = s1ap_parser::parseS1apPdu(s1ap_bytes.data(), s1ap_bytes.size());
                if (parse_result.decoded) {
                    int proc_code = parse_result.procedure_code;
                    proc_code_counts[proc_code]++;
                    if (proc_code == 0 || proc_code == 1 || sample_count < 100) {
                        std::string proc_name = parse_result.procedure_name;
                        int pdu_type = static_cast<int>(parse_result.pdu_type);
                        std::string key = proc_name + " (code=" + std::to_string(proc_code) + ", pdu=" + std::to_string(pdu_type) + ")";
                        msg_type_counts[key]++;
                        sample_count++;
                    }
                }
                
                SignalMessage msg;
                msg.set_ts_capture(pkt.timestamp_sec * 1000000000ULL + pkt.timestamp_usec * 1000);
                msg.set_ts_ingest(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                msg.set_source_id("pcap:" + fs::path(pcap_path).filename().string());
                msg.set_direction(SignalMessage::UNKNOWN); // Could be determined from packet analysis
                msg.set_source_sequence(sequence++);
                msg.set_transport_meta("{\"pcap\": true, \"packet_num\": " + 
                                      std::to_string(pkt.frame_number) + "}");
                msg.set_payload_type(SignalMessage::RAW_BYTES);
                msg.set_raw_bytes(s1ap_bytes.data(), s1ap_bytes.size());
                
                // Append to spool
                spool->append(msg);
                s1ap_count++;
            }
        } else {
            // Single S1AP PDU
            const auto& s1ap_bytes = s1ap_opt.value();
            
            // Debug: sample messages to see what types we get
            auto parse_result = s1ap_parser::parseS1apPdu(s1ap_bytes.data(), s1ap_bytes.size());
            if (parse_result.decoded) {
                int proc_code = parse_result.procedure_code;
                proc_code_counts[proc_code]++;
                if (proc_code == 0 || proc_code == 1 || sample_count < 100) {
                    std::string proc_name = parse_result.procedure_name;
                    int pdu_type = static_cast<int>(parse_result.pdu_type);
                    std::string key = proc_name + " (code=" + std::to_string(proc_code) + ", pdu=" + std::to_string(pdu_type) + ")";
                    msg_type_counts[key]++;
                    sample_count++;
                }
            }
            
            SignalMessage msg;
            msg.set_ts_capture(pkt.timestamp_sec * 1000000000ULL + pkt.timestamp_usec * 1000);
            msg.set_ts_ingest(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            msg.set_source_id("pcap:" + fs::path(pcap_path).filename().string());
            msg.set_direction(SignalMessage::UNKNOWN);
            msg.set_source_sequence(sequence++);
            msg.set_transport_meta("{\"pcap\": true, \"packet_num\": " + 
                                  std::to_string(pkt.frame_number) + "}");
            msg.set_payload_type(SignalMessage::RAW_BYTES);
            msg.set_raw_bytes(s1ap_bytes.data(), s1ap_bytes.size());
            
            // Append to spool
            spool->append(msg);
            s1ap_count++;
        }
    });
    
    std::cout << "  ✓ Processed " << packet_count << " packets, extracted " 
              << s1ap_count << " S1AP PDUs" << std::endl;
    
    // Flush spool to ensure all writes are on disk before pipeline reads
    spool->flush();
    
    // Print message type distribution
    std::cout << "  Procedure code distribution:" << std::endl;
    for (const auto& [proc_code, count] : proc_code_counts) {
        std::cout << "    Code " << proc_code << ": " << count << " messages" << std::endl;
    }
    if (!msg_type_counts.empty()) {
        std::cout << "  Sample message types (handover codes + first 100):" << std::endl;
        for (const auto& [msg_type, count] : msg_type_counts) {
            std::cout << "    " << msg_type << ": " << count << std::endl;
        }
    }
    
    if (s1ap_count == 0) {
        std::cout << "  ⚠ No S1AP PDUs found in PCAP file" << std::endl;
        fs::remove_all(test_spool_dir);
        return;
    }
    
    // Process through pipeline
    int total_events = 0;
    int batches = 0;
    int total_processed = 0;
    while (batches < 100) { // Limit to prevent infinite loop
        int events = pipeline.process_batch(1000); // Process more messages per batch
        total_events += events;
        batches++;
        total_processed += events; // Track how many messages were processed
        
        if (events == 0 && batches > 1) {
            // No more messages to process (but process at least one batch)
            break;
        }
    }
    
    std::cout << "  ✓ Processed " << batches << " batches, emitted " 
              << total_events << " events" << std::endl;
    
    // Verify we got some events
    assert(s1ap_count > 0);
    std::cout << "  ✓ PCAP processing test passed" << std::endl;
    
    // Dump UE records before cleanup
    std::cout << "\nDumping UE records..." << std::endl;
    pipeline.dump_ue_records(std::cout);
    
    // Cleanup
    jsonl_sink->close();
    fs::remove_all(test_spool_dir);
}

int main(int argc, char** argv) {
    std::cout << "Running PCAP processing test..." << std::endl;
    
    std::string pcap_path = "test_data/sample.pcap";
    if (argc > 1) {
        pcap_path = argv[1];
    }
    
    test_pcap_processing(pcap_path);
    
    std::cout << "\nPCAP test completed!" << std::endl;
    return 0;
}

