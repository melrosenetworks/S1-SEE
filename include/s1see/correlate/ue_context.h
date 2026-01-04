/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: ue_context.h
 * Description: Header for UEContext class that manages User Equipment state.
 *              Maintains UE identifiers, location information (ECGI), S1AP IDs,
 *              and generates composite keys for subscriber correlation.
 */

#pragma once

#include "canonical_message.pb.h"
#include <string>
#include <chrono>
#include <unordered_map>
#include <optional>

namespace s1see {
namespace correlate {

// UE Context for correlation
struct UEContext {
    // Identifiers (best available)
    std::optional<uint32_t> mme_ue_s1ap_id;
    std::optional<uint32_t> enb_ue_s1ap_id;
    std::optional<std::string> guti;
    std::optional<std::string> imsi;
    std::optional<std::string> tmsi;
    std::optional<std::string> imei;
    
    // Network element identifiers
    std::optional<std::string> enb_id;        // eNodeB identifier
    std::optional<std::string> mme_id;        // MME identifier
    std::optional<std::string> mme_group_id;  // MME Group ID (from GUTI)
    std::optional<std::string> mme_code;      // MME Code (from GUTI)
    
    // Current cell
    std::string ecgi;
    std::string target_ecgi; // For handovers in progress
    
    // Procedure state
    std::string last_procedure;
    std::chrono::system_clock::time_point last_seen;
    
    // Subscriber key (best identifier available)
    std::string subscriber_key;
    
    // Cached composite keys for fast lookups (avoid repeated string concatenation)
    std::string mme_composite_key;  // "mme_id:mme_ue_s1ap_id"
    std::string enb_composite_key;   // "enb_id:enb_ue_s1ap_id"
    std::string tmsi_composite_key;  // "tmsi@ecgi"
    
    // Additional state for handover sequences
    bool handover_in_progress = false;
    std::chrono::system_clock::time_point handover_start_time;
    std::string source_ecgi;
    
    // Update from a canonical message
    void update(const CanonicalMessage& msg);
    
    // Generate subscriber key from available identifiers
    std::string generate_subscriber_key() const;
    
    // Check if context is expired (inactive for too long)
    bool is_expired(std::chrono::seconds max_inactivity) const;
    
    // Check if this context matches another by stable identifiers (IMSI, GUTI, IMEI)
    bool matches_stable_identity(const UEContext& other) const;
    
    // Update cached composite keys (call after updating identifiers)
    void update_composite_keys();
};

} // namespace correlate
} // namespace s1see

