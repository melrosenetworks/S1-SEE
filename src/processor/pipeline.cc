/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: pipeline.cc
 * Description: Implementation of the Pipeline class for processing S1AP messages.
 *              Handles message decoding, normalization, correlation, and rule-based
 *              event generation. Manages the complete processing pipeline from
 *              spool records to emitted events.
 */

#include "s1see/processor/pipeline.h"
#include "s1see/decode/s1ap_decoder_wrapper.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <cctype>
#include "spool_record.pb.h"

namespace s1see {
namespace processor {

Pipeline::Pipeline(const Config& config) : config_(config) {
    spool::WALLog::Config wal_config;
    wal_config.base_dir = config_.spool_base_dir;
    wal_config.num_partitions = config_.spool_partitions;
    spool_ = std::make_unique<spool::Spool>(wal_config);
    
    correlate::Correlator::Config corr_config;
    corr_config.context_expiry = config_.context_expiry;
    correlator_ = std::make_shared<correlate::Correlator>(corr_config);
    
    rule_engine_ = std::make_unique<rules::RuleEngine>(correlator_);
    
    // Use real S1AP decoder (s1ap_parser)
    decoder_ = std::make_unique<decode::RealS1APDecoder>();
}

void Pipeline::set_decoder(std::unique_ptr<decode::S1APDecoderWrapper> decoder) {
    decoder_ = std::move(decoder);
}

void Pipeline::load_ruleset(const rules::Ruleset& ruleset) {
    rule_engine_->load_ruleset(ruleset);
}

void Pipeline::add_sink(std::shared_ptr<sinks::Sink> sink) {
    sinks_.push_back(sink);
}

CanonicalMessage Pipeline::decode_and_normalize(const SpoolRecord& record) {
    CanonicalMessage canonical;
    
    // Set spool reference
    canonical.set_spool_partition(record.partition());
    canonical.set_spool_offset(record.offset());
    
    const auto& message = record.message();
    
    // Extract frame number from transport_meta if present (for PCAP sources)
    if (!message.transport_meta().empty()) {
        // Parse JSON: {"pcap": true, "packet_num": <number>}
        std::string meta = message.transport_meta();
        size_t pos = meta.find("\"packet_num\"");
        if (pos != std::string::npos) {
            pos = meta.find(":", pos);
            if (pos != std::string::npos) {
                pos++; // Skip ':'
                // Skip whitespace
                while (pos < meta.size() && (meta[pos] == ' ' || meta[pos] == '\t')) pos++;
                // Extract number
                if (pos < meta.size() && std::isdigit(meta[pos])) {
                    int64_t frame_num = 0;
                    while (pos < meta.size() && std::isdigit(meta[pos])) {
                        frame_num = frame_num * 10 + (meta[pos] - '0');
                        pos++;
                    }
                    canonical.set_frame_number(frame_num);
                }
            }
        }
    }
    
    // Decode
    decode::DecodedTree decoded_tree;
    std::vector<uint8_t> raw_bytes(message.raw_bytes().begin(), message.raw_bytes().end());
    
    bool decode_ok = decoder_->decode(raw_bytes, canonical, decoded_tree);
    
    if (!decode_ok) {
        canonical.set_decode_failed(true);
        canonical.set_raw_bytes(message.raw_bytes());
        return canonical;
    }
    
    // Preserve raw bytes
    canonical.set_raw_bytes(message.raw_bytes());
    canonical.set_decoded_tree(decoded_tree.json_representation);
    
    return canonical;
}

std::vector<Event> Pipeline::process_message(const CanonicalMessage& canonical) {
    // Run rules - this will call get_or_create_context internally
    // No need to call update_context separately as it would duplicate the work
    return rule_engine_->process(canonical);
}

int Pipeline::process_batch(int64_t max_messages) {
    int events_emitted = 0;
    
    // Process each partition
    for (int32_t p = 0; p < config_.spool_partitions; ++p) {
        // Load consumer offset
        int64_t offset = spool_->load_offset(config_.consumer_group, p);
        int64_t high_water = spool_->get_high_water_mark(p);
        
        if (offset >= high_water) {
            continue; // Nothing new
        }
        
        // Read batch
        auto records = spool_->read(p, offset, max_messages);
        
        int64_t last_offset = offset;
        for (const auto& record : records) {
            try {
                // Decode and normalize
                auto canonical = decode_and_normalize(record);
                
                // Process through rules
                auto events = process_message(canonical);
                
                // Emit events
                for (const auto& event : events) {
                    for (auto& sink : sinks_) {
                        sink->emit(event);
                    }
                    events_emitted++;
                }
                
                last_offset = record.offset();
            } catch (const std::exception& e) {
                std::cerr << "Error processing record p=" << p 
                         << " offset=" << record.offset() << ": " << e.what() << std::endl;
            }
        }
        
        // Commit offset
        if (last_offset > offset) {
            spool_->commit_offset(config_.consumer_group, p, last_offset);
        }
    }
    
    // Cleanup
    correlator_->cleanup_expired();
    rule_engine_->cleanup_expired_sequences();
    
    return events_emitted;
}

void Pipeline::run_continuous() {
    while (true) {
        process_batch();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Pipeline::dump_ue_records(std::ostream& os) const {
    if (correlator_) {
        correlator_->dump_ue_records(os);
    }
}

} // namespace processor
} // namespace s1see

