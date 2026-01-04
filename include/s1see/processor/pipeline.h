/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: pipeline.h
 * Description: Header for Pipeline class that processes S1AP messages through
 *              decode, normalize, correlate, and rule evaluation stages.
 *              Manages the complete processing pipeline from spool records
 *              to emitted events.
 */

#pragma once

#include "s1see/spool/spool.h"
#include "s1see/decode/s1ap_decoder_wrapper.h"
#include "s1see/correlate/correlator.h"
#include "s1see/rules/rule_engine.h"
#include "s1see/sinks/sink.h"
#include "canonical_message.pb.h"
#include "event.pb.h"
#include <memory>
#include <vector>
#include <string>

namespace s1see {
namespace processor {

// Main processing pipeline
class Pipeline {
public:
    struct Config {
        std::string spool_base_dir = "spool_data";
        int32_t spool_partitions = 1;
        std::string consumer_group = "default";
        std::chrono::seconds context_expiry = std::chrono::seconds(300);
    };
    
    explicit Pipeline(const Config& config);
    
    // Set decoder (takes ownership)
    void set_decoder(std::unique_ptr<decode::S1APDecoderWrapper> decoder);
    
    // Load ruleset
    void load_ruleset(const rules::Ruleset& ruleset);
    
    // Add sink
    void add_sink(std::shared_ptr<sinks::Sink> sink);
    
    // Process one batch from spool
    // Returns number of events emitted
    int process_batch(int64_t max_messages = 100);
    
    // Run continuous processing (blocking)
    void run_continuous();
    
    // Dump UE records (for debugging/shutdown)
    void dump_ue_records(std::ostream& os) const;

private:
    Config config_;
    std::unique_ptr<spool::Spool> spool_;
    std::unique_ptr<decode::S1APDecoderWrapper> decoder_;
    std::shared_ptr<correlate::Correlator> correlator_;
    std::unique_ptr<rules::RuleEngine> rule_engine_;
    std::vector<std::shared_ptr<sinks::Sink>> sinks_;
    
    CanonicalMessage decode_and_normalize(const SpoolRecord& record);
    std::vector<Event> process_message(const CanonicalMessage& canonical);
};

} // namespace processor
} // namespace s1see

