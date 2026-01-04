/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: correlator.h
 * Description: Header for Correlator class that manages UE (User Equipment) contexts
 *              and correlates S1AP messages to subscribers. Integrates with
 *              S1apUeCorrelator to maintain UE state and generate composite
 *              subscriber keys for event correlation.
 */

#pragma once

#include "s1see/correlate/ue_context.h"
#include "canonical_message.pb.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <chrono>
#include <shared_mutex>
#include <cstdint>

// Include S1apUeCorrelator (full definition needed for unique_ptr)
#include "s1ap_ue_correlator.h"

namespace s1see {
namespace correlate {

// UE Context Correlator
class Correlator {
public:
    struct Config {
        Config() : context_expiry(std::chrono::seconds(300)) {}
        std::chrono::seconds context_expiry; // 5 minutes default
    };
    
    explicit Correlator(const Config& config = Config());
    
    // Get or create UE context for a message
    // Returns subscriber_key for the context
    std::string get_or_create_context(const CanonicalMessage& message);
    
    // Update context from a message
    void update_context(const CanonicalMessage& message);
    
    // Get context by subscriber key
    std::shared_ptr<UEContext> get_context(const std::string& subscriber_key);
    
    // Cleanup expired contexts
    void cleanup_expired();
    
    // Dump all UE records to output stream (for debugging/shutdown)
    void dump_ue_records(std::ostream& os) const;

private:
    Config config_;
    mutable std::shared_mutex mutex_; // Read-write lock for fine-grained locking
    
    // S1apUeCorrelator instance
    std::unique_ptr<s1ap_correlator::S1apUeCorrelator> s1ap_correlator_;
    
    // Context storage: subscriber_key -> UEContext
    std::unordered_map<std::string, std::shared_ptr<UEContext>> contexts_;
    
    // Counter for unknown subscriber IDs
    uint64_t next_unknown_id_ = 1;
    
    // Helper function to update UEContext from SubscriberRecord
    void update_context_from_subscriber(
        std::shared_ptr<UEContext> context,
        const s1ap_correlator::SubscriberRecord* subscriber,
        const CanonicalMessage& message);
};

} // namespace correlate
} // namespace s1see

