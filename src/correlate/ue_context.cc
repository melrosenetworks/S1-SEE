/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: ue_context.cc
 * Description: Implementation of UEContext class for managing User Equipment state.
 *              Maintains UE identifiers, location information (ECGI), S1AP IDs,
 *              and generates composite keys for subscriber correlation. Tracks
 *              message history and last seen timestamps.
 */

#include "s1see/correlate/ue_context.h"
#include "canonical_message.pb.h"
#include <sstream>
#include <iomanip>

namespace s1see {
namespace correlate {

// Helper function to convert bytes (std::string) to hex string
static std::string bytes_to_hex_string(const std::string& bytes) {
    if (bytes.empty()) {
        return "";
    }
    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        hex << std::setw(2) << static_cast<int>(byte);
    }
    return hex.str();
}

void UEContext::update(const CanonicalMessage& msg) {
    // Update UE S1AP IDs
    // Note: Protobuf uses int32_t, but S1AP IDs are unsigned. Convert properly.
    if (msg.mme_ue_s1ap_id() != 0) {
        mme_ue_s1ap_id = static_cast<uint32_t>(msg.mme_ue_s1ap_id());
    }
    if (msg.enb_ue_s1ap_id() != 0) {
        // eNB-UE-S1AP-ID is 24-bit unsigned, but protobuf stores as int32_t
        // Convert negative values (from signed interpretation) to unsigned
        int32_t signed_id = msg.enb_ue_s1ap_id();
        enb_ue_s1ap_id = static_cast<uint32_t>(signed_id);
    }
    
    // Update stable identifiers (these don't change)
    if (!msg.imsi().empty()) {
        imsi = msg.imsi();
    }
    if (!msg.guti().empty()) {
        guti = msg.guti();
    }
    if (!msg.tmsi().empty()) {
        tmsi = msg.tmsi();
    }
    if (!msg.imei().empty()) {
        imei = msg.imei();
    }
    
    // Update network element identifiers (may change on handover/context change)
    if (!msg.enb_id().empty()) {
        enb_id = msg.enb_id();
    }
    if (!msg.mme_id().empty()) {
        mme_id = msg.mme_id();
    }
    if (!msg.mme_group_id().empty()) {
        mme_group_id = msg.mme_group_id();
    }
    if (!msg.mme_code().empty()) {
        mme_code = msg.mme_code();
    }
    
    // Update cell information
    if (!msg.ecgi().empty()) {
        ecgi = msg.ecgi();
    }
    if (!msg.target_ecgi().empty()) {
        target_ecgi = msg.target_ecgi();
    }
    if (!msg.msg_type().empty()) {
        last_procedure = msg.msg_type();
    }
    
    last_seen = std::chrono::system_clock::now();
    
    // Update cached composite keys
    update_composite_keys();
    
    // Update handover state
    // HandoverRequired: Source eNodeB initiates handover
    if (msg.msg_type() == "HandoverRequired") {
        handover_in_progress = true;
        handover_start_time = std::chrono::system_clock::now();
        source_ecgi = ecgi;
        if (!target_ecgi.empty()) {
            ecgi = target_ecgi; // Update to target
        }
    }
    // HandoverCommand: MME commands source eNodeB to execute handover
    else if (msg.msg_type() == "HandoverCommand") {
        handover_in_progress = true;
        if (handover_start_time.time_since_epoch().count() == 0) {
            handover_start_time = std::chrono::system_clock::now();
        }
        source_ecgi = ecgi;
        if (!target_ecgi.empty()) {
            ecgi = target_ecgi;
        }
    }
    // HandoverNotify: UE successfully arrives at target cell
    else if (msg.msg_type() == "HandoverNotify") {
        if (handover_in_progress) {
            handover_in_progress = false;
            if (!target_ecgi.empty()) {
                ecgi = target_ecgi;
            }
        }
    }
    
    subscriber_key = generate_subscriber_key();
}

std::string UEContext::generate_subscriber_key() const {
    // Priority: IMSI > GUTI > TMSI+ECGI > MME composite > eNB composite > IMEI
    // Note: When IMSI is not available, we use network identifiers (eNodeB/MME composite)
    // This allows tracking UEs even without IMSI
    
    if (imsi.has_value()) {
        return "imsi:" + imsi.value();
    }
    if (guti.has_value()) {
        return "guti:" + guti.value();
    }
    if (tmsi.has_value() && !ecgi.empty()) {
        return "tmsi:" + tmsi.value() + "@" + bytes_to_hex_string(ecgi);
    }
    // Use MME composite when available (even without IMSI)
    if (mme_id.has_value() && mme_ue_s1ap_id.has_value()) {
        return "mme:" + mme_id.value() + ":" + std::to_string(mme_ue_s1ap_id.value());
    }
    // Use eNB composite when available (even without IMSI)
    if (enb_id.has_value() && enb_ue_s1ap_id.has_value()) {
        return "enb:" + enb_id.value() + ":" + std::to_string(enb_ue_s1ap_id.value());
    }
    if (imei.has_value()) {
        return "imei:" + imei.value();
    }
    // Fallback to legacy single-ID matching (less reliable, but better than nothing)
    if (mme_ue_s1ap_id.has_value()) {
        return "mme:" + std::to_string(mme_ue_s1ap_id.value());
    }
    if (enb_ue_s1ap_id.has_value()) {
        return "enb:" + std::to_string(enb_ue_s1ap_id.value());
    }
    return "unknown";
}

bool UEContext::matches_stable_identity(const UEContext& other) const {
    // Check if contexts match by stable identifiers (IMSI, GUTI, IMEI)
    // These don't change when eNodeB/MME changes
    if (imsi.has_value() && other.imsi.has_value()) {
        if (imsi.value() == other.imsi.value()) {
            return true;
        }
    }
    if (guti.has_value() && other.guti.has_value()) {
        if (guti.value() == other.guti.value()) {
            return true;
        }
    }
    if (imei.has_value() && other.imei.has_value()) {
        if (imei.value() == other.imei.value()) {
            return true;
        }
    }
    return false;
}

bool UEContext::is_expired(std::chrono::seconds max_inactivity) const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_seen);
    return elapsed > max_inactivity;
}

void UEContext::update_composite_keys() {
    // Update MME composite key
    if (mme_id.has_value() && mme_ue_s1ap_id.has_value()) {
        mme_composite_key = mme_id.value() + ":" + std::to_string(mme_ue_s1ap_id.value());
    } else {
        mme_composite_key.clear();
    }
    
    // Update eNB composite key
    if (enb_id.has_value() && enb_ue_s1ap_id.has_value()) {
        enb_composite_key = enb_id.value() + ":" + std::to_string(enb_ue_s1ap_id.value());
    } else {
        enb_composite_key.clear();
    }
    
    // Update TMSI composite key
    if (tmsi.has_value() && !ecgi.empty()) {
        tmsi_composite_key = tmsi.value() + "@" + bytes_to_hex_string(ecgi);
    } else {
        tmsi_composite_key.clear();
    }
}

} // namespace correlate
} // namespace s1see

