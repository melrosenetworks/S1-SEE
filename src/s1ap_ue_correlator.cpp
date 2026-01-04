/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: s1ap_ue_correlator.cpp
 * Description: C++ implementation for correlating S1AP messages to UEs (User Equipment).
 *              Maintains subscriber records with identifiers (IMSI, TMSI, IMEISV, S1AP IDs, TEIDs),
 *              tracks associations between identifiers, and manages subscriber lifecycle.
 */

#include "s1ap_ue_correlator.h"
#include "s1ap_parser.h"  // For S1apResult definition
#include "nas_parser.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace s1ap_correlator {

// Helper function to convert hex string to bytes
// namespace {
//     std::vector<uint8_t> hexToBytes(const std::string& hex) {
//     std::vector<uint8_t> bytes;
//     for (size_t i = 0; i < hex.length(); i += 2) {
//         if (i + 1 < hex.length()) {
//             std::string byte_str = hex.substr(i, 2);
//             bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
//         }
//     }
//     return bytes;
//     }
// } // anonymous namespace

// Constructor
S1apUeCorrelator::S1apUeCorrelator() : next_subscriber_id_(1) {
}

// Destructor
S1apUeCorrelator::~S1apUeCorrelator() {
}

// Process S1AP frame and correlate to UE
SubscriberRecord* S1apUeCorrelator::processS1apFrame(uint32_t /* frame_no */, const s1ap_parser::S1apParseResult& s1ap_result, double timestamp) {
    if (s1ap_result.procedure_code == 12 /*id-initialUEMessage*/) {
        // first sight of MME-UE-S1AP-ID
        DEBUG_LOG << "[S1AP] processS1apFrame: Initial UE Message" << std::endl;
        auto enb_id_it = s1ap_result.information_elements.find("eNB-UE-S1AP-ID");
        if (enb_id_it != s1ap_result.information_elements.end()) {
            DEBUG_LOG << "[S1AP] processS1apFrame: eNB-UE-S1AP-ID: 0x" << enb_id_it->second << std::endl;
        }
    } else if (s1ap_result.procedure_code == 13) {
        DEBUG_LOG << "[S1AP] processS1apFrame: Uplink NAS Transport" << std::endl;
    } else if (s1ap_result.procedure_code == 11) {
        DEBUG_LOG << "[S1AP] processS1apFrame: Downlink NAS Transport" << std::endl;
    }

    // Extract identifiers and TEIDs (using s1ap_parser functions)
    auto teids = extractTeidsFromS1ap(s1ap_result);
    auto imsis = s1ap_parser::extractImsisFromS1ap(s1ap_result);
    auto tmsi_result = s1ap_parser::extractTmsisFromS1ap(s1ap_result);
    const auto& tmsis = tmsi_result.tmsis;
    // Add all TEIDs from decoded_list.items to the teids vector
    if (!tmsi_result.teids.empty()) {
        DEBUG_LOG << "[S1AP] processS1apFrame: Found " << tmsi_result.teids.size() 
                  << " TEID(s) from decoded_list.items: ";
        bool first = true;
        for (uint32_t teid : tmsi_result.teids) {
            if (!first) DEBUG_LOG << ", ";
            DEBUG_LOG << "0x" << std::hex << teid << std::dec << " (" << teid << ")";
            teids.push_back(teid);
            first = false;
        }
        DEBUG_LOG << std::endl;
    }
    auto imeisvs = s1ap_parser::extractImeisvsFromS1ap(s1ap_result);
    auto s1ap_ids = s1ap_parser::extractS1apIds(s1ap_result);

    // Build mappings
    for (const auto& imsi : imsis) {
        std::string imsi_norm = normalizeImsi(imsi);
        for (uint32_t teid : teids) {
            imsi_to_teids_[imsi_norm].insert(teid);
            teid_to_imsi_[teid] = imsi_norm;
        }

        // Map S1AP IDs
        if (s1ap_ids.first.has_value()) {
            imsi_to_mme_ue_s1ap_id_[imsi_norm] = s1ap_ids.first.value();
        }
        if (s1ap_ids.second.has_value()) {
            imsi_to_enb_ue_s1ap_id_[imsi_norm] = s1ap_ids.second.value();
        }
    }

    // Similar for TMSI and IMEISV
    for (const auto& tmsi : tmsis) {
        std::string tmsi_norm = normalizeTmsi(tmsi);
        for (uint32_t teid : teids) {
            tmsi_to_teids_[tmsi_norm].insert(teid);
            teid_to_tmsi_[teid] = tmsi_norm;
        }
    }

    for (const auto& imeisv : imeisvs) {
        std::string imeisv_norm = normalizeImeisv(imeisv);
        for (uint32_t teid : teids) {
            imeisv_to_teids_[imeisv_norm].insert(teid);
            teid_to_imeisv_[teid] = imeisv_norm;
        }
    }

    // Map S1AP IDs to TEIDs
    if (s1ap_ids.first.has_value() && s1ap_ids.second.has_value() && !teids.empty()) {
        std::pair<uint32_t, uint32_t> s1ap_pair(s1ap_ids.first.value(), s1ap_ids.second.value());
        for (uint32_t teid : teids) {
            s1ap_ids_to_teids_[s1ap_pair].insert(teid);
        }
    }
    
    // Normalize all identifiers
    std::optional<std::string> imsi_norm = imsis.empty() ? std::nullopt : std::make_optional(normalizeImsi(imsis[0]));
    std::optional<std::string> tmsi_norm = tmsis.empty() ? std::nullopt : std::make_optional(normalizeTmsi(tmsis[0]));
    std::optional<std::string> imeisv_norm = imeisvs.empty() ? std::nullopt : std::make_optional(normalizeImeisv(imeisvs[0]));
    
    // Process all identifiers together to ensure they're merged into a single record
    // This is critical - if IMSI and TMSI appear in the same message, they must be in the same record
    if (imsi_norm.has_value() || tmsi_norm.has_value() || imeisv_norm.has_value() || 
        s1ap_ids.first.has_value() || s1ap_ids.second.has_value()) {
        
        // Get or create subscriber record with ALL identifiers from this message
        // This ensures they're all merged into one record
        SubscriberRecord* subscriber = getOrCreateSubscriber(
            imsi_norm,
            tmsi_norm,
            s1ap_ids.second,  // eNB-UE-S1AP-ID
            s1ap_ids.first,   // MME-UE-S1AP-ID
            std::nullopt,  // TEIDs will be associated separately
            imeisv_norm
        );
        
        if (subscriber) {
            // Get subscriber_id from any available mapping
            uint64_t subscriber_id = 0;
            if (imsi_norm.has_value()) {
                auto it = imsi_to_subscriber_id_.find(imsi_norm.value());
                if (it != imsi_to_subscriber_id_.end() && it->second != 0) {
                    subscriber_id = it->second;
                }
            }
            if (subscriber_id == 0 && tmsi_norm.has_value()) {
                auto it = tmsi_to_subscriber_id_.find(tmsi_norm.value());
                if (it != tmsi_to_subscriber_id_.end() && it->second != 0) {
                    subscriber_id = it->second;
                }
            }
            if (subscriber_id == 0 && imeisv_norm.has_value()) {
                auto it = imeisv_to_subscriber_id_.find(imeisv_norm.value());
                if (it != imeisv_to_subscriber_id_.end() && it->second != 0) {
                    subscriber_id = it->second;
                }
            }
            if (subscriber_id == 0 && s1ap_ids.first.has_value()) {
                auto it = mme_ue_s1ap_id_to_subscriber_id_.find(s1ap_ids.first.value());
                if (it != mme_ue_s1ap_id_to_subscriber_id_.end() && it->second != 0) {
                    subscriber_id = it->second;
                }
            }
            if (subscriber_id == 0 && s1ap_ids.second.has_value()) {
                auto it = enb_ue_s1ap_id_to_subscriber_id_.find(s1ap_ids.second.value());
                if (it != enb_ue_s1ap_id_to_subscriber_id_.end() && it->second != 0) {
                    subscriber_id = it->second;
                }
            }
            
            // Associate S1AP IDs if present (may not have been in getOrCreateSubscriber call)
            if (subscriber_id != 0) {
                if (s1ap_ids.first.has_value()) {
                    associateMmeUeS1apId(subscriber, subscriber_id, s1ap_ids.first.value());
                }
                if (s1ap_ids.second.has_value()) {
                    associateEnbUeS1apId(subscriber, subscriber_id, s1ap_ids.second.value());
                }
                
                // Associate TEIDs
                for (uint32_t teid : teids) {
                    associateTeid(subscriber, subscriber_id, teid);
                }
            }
            
            // Update timestamps
            if (timestamp > 0.0) {
                if (!subscriber->first_seen_timestamp.has_value()) {
                    subscriber->first_seen_timestamp = timestamp;
                }
                subscriber->last_seen_timestamp = timestamp;
            }

            // Handle UEContextReleaseComplete even if no identifiers were found in normal processing
            // (e.g., if the message only contains S1AP IDs without other identifiers)
            // Note: s1ap_ids was already extracted at the beginning of the function, so we can reuse it
            if (s1ap_result.procedure_code == 23 /*id-UEContextRelease*/ && 
                s1ap_result.pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
                DEBUG_LOG << "[S1AP] processS1apFrame: UEContextReleaseComplete detected, removing S1AP IDs" << std::endl;
                
                // Remove MME-UE-S1AP-ID if present
                if (s1ap_ids.first.has_value()) {
                    uint32_t mme_id = s1ap_ids.first.value();
                    removeMmeUeS1apIdAssociation(mme_id);
                    DEBUG_LOG << "[S1AP] processS1apFrame: Removed MME-UE-S1AP-ID=" << mme_id << std::endl;
                }
                
                // Remove eNB-UE-S1AP-ID if present
                if (s1ap_ids.second.has_value()) {
                    uint32_t enb_id = s1ap_ids.second.value();
                    removeEnbUeS1apIdAssociation(enb_id);
                    DEBUG_LOG << "[S1AP] processS1apFrame: Removed eNB-UE-S1AP-ID=" << enb_id << std::endl;
                }
            }
            
            DEBUG_LOG << "[S1AP] processS1apFrame: Updated subscriber record" << std::endl;
            return subscriber;  // Return the subscriber that was created/updated
        }
    }
    
    // No identifiers found, return nullptr
    return nullptr;
}

// Identifier extraction functions
std::vector<uint32_t> S1apUeCorrelator::extractTeidsFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result) {
    // Extract TEIDs from S1AP bytes using pattern matching
    if (s1ap_result.raw_bytes.empty()) {
        return {};
    }
    
    return s1ap_parser::extractTeidsFromS1apBytes(
        s1ap_result.raw_bytes.data(),
        s1ap_result.raw_bytes.size()
    );
}

std::vector<std::string> S1apUeCorrelator::extractImsisFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result) {
    return s1ap_parser::extractImsisFromS1ap(s1ap_result);
}

s1ap_correlator::TmsiExtractionResult S1apUeCorrelator::extractTmsisFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result) {
    s1ap_parser::TmsiExtractionResult parser_result = s1ap_parser::extractTmsisFromS1ap(s1ap_result);
    s1ap_correlator::TmsiExtractionResult result;
    result.tmsis = parser_result.tmsis;
    result.teids = parser_result.teids;
    return result;
}

std::vector<std::string> S1apUeCorrelator::extractImeisvsFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result) {
    return s1ap_parser::extractImeisvsFromS1ap(s1ap_result);
}

std::pair<std::optional<uint32_t>, std::optional<uint32_t>>
S1apUeCorrelator::extractS1apIds(const s1ap_parser::S1apParseResult& s1ap_result) {
    return s1ap_parser::extractS1apIds(s1ap_result);
}

// NAS extraction helpers
std::vector<std::string> S1apUeCorrelator::extractImsiFromNas(const uint8_t* nas_bytes, size_t len) {
    return nas_parser::extractImsiFromNas(nas_bytes, len);
}

std::vector<std::string> S1apUeCorrelator::extractTmsiFromNas(const uint8_t* nas_bytes, size_t len) {
    return nas_parser::extractTmsiFromNas(nas_bytes, len);
}

std::vector<std::string> S1apUeCorrelator::extractImeisvFromNas(const uint8_t* nas_bytes, size_t len) {
    return nas_parser::extractImeisvFromNas(nas_bytes, len);
}

// Normalization functions
std::string S1apUeCorrelator::normalizeImsi(const std::string& imsi) {
    std::string normalized;
    for (char c : imsi) {
        if (std::isdigit(c)) {
            normalized += c;
        }
    }
    return normalized;
}

std::string S1apUeCorrelator::normalizeTmsi(const std::string& tmsi) {
    std::string normalized;
    for (char c : tmsi) {
        if (std::isxdigit(c)) {
            normalized += std::tolower(c);
        }
    }
    // Remove 0x prefix if present
    if (normalized.length() > 2 && normalized.substr(0, 2) == "0x") {
        normalized = normalized.substr(2);
    }
    return normalized;
}

std::string S1apUeCorrelator::normalizeImeisv(const std::string& imeisv) {
    return normalizeImsi(imeisv); // Same normalization as IMSI
}

// Subscriber Record Management Implementation

SubscriberRecord* S1apUeCorrelator::getOrCreateSubscriber(
    const std::optional<std::string>& imsi,
    const std::optional<std::string>& tmsi,
    const std::optional<uint32_t>& enb_ue_s1ap_id,
    const std::optional<uint32_t>& mme_ue_s1ap_id,
    const std::optional<uint32_t>& teid,
    const std::optional<std::string>& imeisv) {
    
    uint64_t subscriber_id = 0;
    SubscriberRecord* subscriber = nullptr;
    
    // Try to find existing subscriber by any provided identifier
    // Priority order: IMSI > TMSI > IMEISV > (Both S1AP IDs) > MME-UE-S1AP-ID > eNB-UE-S1AP-ID > TEID
    
    // Highest priority: IMSI, TMSI, IMEISV
    if (imsi.has_value()) {
        auto it = imsi_to_subscriber_id_.find(imsi.value());
        if (it != imsi_to_subscriber_id_.end()) {
            subscriber_id = it->second;
        }
    }
    if (subscriber_id == 0 && tmsi.has_value()) {
        auto it = tmsi_to_subscriber_id_.find(tmsi.value());
        if (it != tmsi_to_subscriber_id_.end()) {
            subscriber_id = it->second;
        }
    }
    if (subscriber_id == 0 && imeisv.has_value()) {
        auto it = imeisv_to_subscriber_id_.find(imeisv.value());
        if (it != imeisv_to_subscriber_id_.end()) {
            subscriber_id = it->second;
        }
    }
    
    // S1AP ID matching: prefer both IDs together, then MME, then eNB
    if (subscriber_id == 0 && mme_ue_s1ap_id.has_value() && enb_ue_s1ap_id.has_value()) {
        // Try to find subscriber with BOTH S1AP IDs (exact match)
        auto mme_it = mme_ue_s1ap_id_to_subscriber_id_.find(mme_ue_s1ap_id.value());
        auto enb_it = enb_ue_s1ap_id_to_subscriber_id_.find(enb_ue_s1ap_id.value());
        
        if (mme_it != mme_ue_s1ap_id_to_subscriber_id_.end() && 
            enb_it != enb_ue_s1ap_id_to_subscriber_id_.end() &&
            mme_it->second == enb_it->second) {
            // Found subscriber with both IDs matching
            subscriber_id = mme_it->second;
        }
    }
    
    // If both IDs didn't match, try MME-UE-S1AP-ID alone (preferred over eNB)
    if (subscriber_id == 0 && mme_ue_s1ap_id.has_value()) {
        auto it = mme_ue_s1ap_id_to_subscriber_id_.find(mme_ue_s1ap_id.value());
        if (it != mme_ue_s1ap_id_to_subscriber_id_.end()) {
            subscriber_id = it->second;
        }
    }
    
    // If MME didn't match, try eNB-UE-S1AP-ID
    if (subscriber_id == 0 && enb_ue_s1ap_id.has_value()) {
        auto it = enb_ue_s1ap_id_to_subscriber_id_.find(enb_ue_s1ap_id.value());
        if (it != enb_ue_s1ap_id_to_subscriber_id_.end()) {
            subscriber_id = it->second;
        }
    }
    
    // Lower priority: TEID
    if (subscriber_id == 0 && teid.has_value()) {
        auto it = teid_to_subscriber_id_.find(teid.value());
        if (it != teid_to_subscriber_id_.end()) {
            subscriber_id = it->second;
        }
    }
    
    // Fallback: If we only have S1AP IDs and couldn't find a subscriber (e.g., IDs were removed from mapping),
    // check if there's a subscriber with matching S1AP IDs in the record (even if not in mapping)
    // or a subscriber with IMSI/TMSI that we can merge the S1AP IDs into.
    // This handles the case where S1AP IDs were removed from mappings but kept in record (similar to ue_context).
    if (subscriber_id == 0 && !imsi.has_value() && !tmsi.has_value() && !imeisv.has_value() &&
        (mme_ue_s1ap_id.has_value() || enb_ue_s1ap_id.has_value())) {
        // First, check if any subscriber has these S1AP IDs in the record (even if not in mapping)
        // This handles the case where IDs were removed from mapping but kept in record
        uint64_t candidate_by_s1ap_id = 0;
        for (const auto& [id, record] : subscriber_records_) {
            bool mme_matches = (!mme_ue_s1ap_id.has_value() || 
                               (record.mme_ue_s1ap_id.has_value() && record.mme_ue_s1ap_id.value() == mme_ue_s1ap_id.value()));
            bool enb_matches = (!enb_ue_s1ap_id.has_value() || 
                               (record.enb_ue_s1ap_id.has_value() && record.enb_ue_s1ap_id.value() == enb_ue_s1ap_id.value()));
            if (mme_matches && enb_matches && (record.mme_ue_s1ap_id.has_value() || record.enb_ue_s1ap_id.has_value())) {
                if (candidate_by_s1ap_id == 0) {
                    candidate_by_s1ap_id = id;
                } else {
                    // Multiple matches - can't be sure
                    candidate_by_s1ap_id = 0;
                    break;
                }
            }
        }
        
        if (candidate_by_s1ap_id != 0) {
            subscriber_id = candidate_by_s1ap_id;
            DEBUG_LOG << "[S1AP] getOrCreateSubscriber: Using FALLBACK - found subscriber ID=" << subscriber_id
                      << " with matching S1AP IDs in record (removed from mapping but kept in record)" << std::endl;
        } else {
            // Fallback to checking subscribers with IMSI/TMSI
            // Since S1AP IDs were completely removed, we need to find the subscriber by stable identifiers
            uint64_t candidate_id = 0;
            size_t candidate_count = 0;
            for (const auto& [id, record] : subscriber_records_) {
                // Check if this subscriber has IMSI or TMSI
                if (record.imsi.has_value() || record.tmsi.has_value()) {
                    candidate_count++;
                    if (candidate_id == 0) {
                        candidate_id = id;
                    }
                }
            }
            // Use fallback if there's at least one subscriber with IMSI/TMSI
            // If there are multiple, prefer the one that was most recently updated (highest subscriber ID)
            // This handles the case where S1AP IDs were just removed and a new message comes in
            if (candidate_count == 1 && candidate_id != 0) {
                subscriber_id = candidate_id;
                DEBUG_LOG << "[S1AP] getOrCreateSubscriber: Using FALLBACK - found subscriber ID=" << subscriber_id
                          << " with IMSI/TMSI (count=" << candidate_count 
                          << "), adding S1AP IDs MME=" << (mme_ue_s1ap_id.has_value() ? std::to_string(mme_ue_s1ap_id.value()) : "none")
                          << " eNB=" << (enb_ue_s1ap_id.has_value() ? std::to_string(enb_ue_s1ap_id.value()) : "none") << std::endl;
            } else if (candidate_count > 1 && candidate_id != 0) {
                // Multiple candidates - use the one with the highest ID (most recently created/updated)
                // This is a heuristic: if IDs were just removed, the subscriber that had them is likely the most recent
                uint64_t most_recent_id = candidate_id;
                for (const auto& [id, record] : subscriber_records_) {
                    if ((record.imsi.has_value() || record.tmsi.has_value()) && id > most_recent_id) {
                        most_recent_id = id;
                    }
                }
                subscriber_id = most_recent_id;
                DEBUG_LOG << "[S1AP] getOrCreateSubscriber: Using FALLBACK - found subscriber ID=" << subscriber_id
                          << " with IMSI/TMSI (multiple candidates=" << candidate_count 
                          << ", using most recent), adding S1AP IDs MME=" << (mme_ue_s1ap_id.has_value() ? std::to_string(mme_ue_s1ap_id.value()) : "none")
                          << " eNB=" << (enb_ue_s1ap_id.has_value() ? std::to_string(enb_ue_s1ap_id.value()) : "none") << std::endl;
            } else if (candidate_count == 0) {
                // No subscribers with IMSI/TMSI found - allow creation of new subscriber with S1AP IDs
                // This handles the case where a new UE appears with only S1AP IDs
                DEBUG_LOG << "[S1AP] getOrCreateSubscriber: FALLBACK found no subscribers with IMSI/TMSI, "
                          << "will create new subscriber with S1AP IDs (MME=" << (mme_ue_s1ap_id.has_value() ? std::to_string(mme_ue_s1ap_id.value()) : "none")
                          << " eNB=" << (enb_ue_s1ap_id.has_value() ? std::to_string(enb_ue_s1ap_id.value()) : "none") << ")" << std::endl;
            }
        }
    }
    
    // Create new subscriber if not found
    //bool is_new_subscriber = (subscriber_id == 0);
    if (subscriber_id == 0) {
        subscriber_id = next_subscriber_id_++;
        subscriber_records_[subscriber_id] = SubscriberRecord();
        DEBUG_LOG << "[S1AP] getOrCreateSubscriber: Created NEW subscriber ID=" << subscriber_id;
        if (imsi.has_value()) DEBUG_LOG << " IMSI=" << imsi.value();
        if (tmsi.has_value()) DEBUG_LOG << " TMSI=" << tmsi.value();
        if (mme_ue_s1ap_id.has_value()) DEBUG_LOG << " MME-UE-S1AP-ID=" << mme_ue_s1ap_id.value();
        if (enb_ue_s1ap_id.has_value()) DEBUG_LOG << " eNB-UE-S1AP-ID=" << enb_ue_s1ap_id.value();
        DEBUG_LOG << std::endl;
    } else {
        DEBUG_LOG << "[S1AP] getOrCreateSubscriber: Found EXISTING subscriber ID=" << subscriber_id;
        if (imsi.has_value()) DEBUG_LOG << " matched by IMSI=" << imsi.value();
        else if (tmsi.has_value()) DEBUG_LOG << " matched by TMSI=" << tmsi.value();
        else if (imeisv.has_value()) DEBUG_LOG << " matched by IMEISV=" << imeisv.value();
        else if (mme_ue_s1ap_id.has_value() && enb_ue_s1ap_id.has_value()) DEBUG_LOG << " matched by both S1AP IDs (MME=" << mme_ue_s1ap_id.value() << " eNB=" << enb_ue_s1ap_id.value() << ")";
        else if (mme_ue_s1ap_id.has_value()) DEBUG_LOG << " matched by MME-UE-S1AP-ID=" << mme_ue_s1ap_id.value();
        else if (enb_ue_s1ap_id.has_value()) DEBUG_LOG << " matched by eNB-UE-S1AP-ID=" << enb_ue_s1ap_id.value();
        else if (teid.has_value()) DEBUG_LOG << " matched by TEID=0x" << std::hex << teid.value() << std::dec;
        DEBUG_LOG << std::endl;
    }
    
    subscriber = &subscriber_records_[subscriber_id];
    
    // Update associations with provided identifiers (pass subscriber_id to avoid O(n) lookup)
    if (imsi.has_value()) {
        associateImsi(subscriber, subscriber_id, imsi.value());
    }
    if (tmsi.has_value()) {
        associateTmsi(subscriber, subscriber_id, tmsi.value());
    }
    if (enb_ue_s1ap_id.has_value()) {
        associateEnbUeS1apId(subscriber, subscriber_id, enb_ue_s1ap_id.value());
    }
    if (mme_ue_s1ap_id.has_value()) {
        associateMmeUeS1apId(subscriber, subscriber_id, mme_ue_s1ap_id.value());
    }
    if (teid.has_value()) {
        associateTeid(subscriber, subscriber_id, teid.value());
    }
    if (imeisv.has_value()) {
        associateImeisv(subscriber, subscriber_id, imeisv.value());
    }
    
    return subscriber;
}

SubscriberRecord* S1apUeCorrelator::getSubscriberByImsi(const std::string& imsi) {
    auto it = imsi_to_subscriber_id_.find(imsi);
    if (it != imsi_to_subscriber_id_.end() && it->second != 0) {
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            return &record_it->second;
        }
    }
    return nullptr;
}

SubscriberRecord* S1apUeCorrelator::getSubscriberByTmsi(const std::string& tmsi) {
    auto it = tmsi_to_subscriber_id_.find(tmsi);
    if (it != tmsi_to_subscriber_id_.end() && it->second != 0) {
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            return &record_it->second;
        }
    }
    return nullptr;
}

SubscriberRecord* S1apUeCorrelator::getSubscriberByEnbUeS1apId(uint32_t enb_ue_s1ap_id) {
    auto it = enb_ue_s1ap_id_to_subscriber_id_.find(enb_ue_s1ap_id);
    if (it != enb_ue_s1ap_id_to_subscriber_id_.end() && it->second != 0) {
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            return &record_it->second;
        }
    }
    return nullptr;
}

SubscriberRecord* S1apUeCorrelator::getSubscriberByMmeUeS1apId(uint32_t mme_ue_s1ap_id) {
    auto it = mme_ue_s1ap_id_to_subscriber_id_.find(mme_ue_s1ap_id);
    if (it != mme_ue_s1ap_id_to_subscriber_id_.end() && it->second != 0) {
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            return &record_it->second;
        }
    }
    return nullptr;
}

SubscriberRecord* S1apUeCorrelator::getSubscriberByTeid(uint32_t teid) {
    auto it = teid_to_subscriber_id_.find(teid);
    if (it != teid_to_subscriber_id_.end() && it->second != 0) {
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            return &record_it->second;
        }
    }
    return nullptr;
}

SubscriberRecord* S1apUeCorrelator::getSubscriberByImeisv(const std::string& imeisv) {
    auto it = imeisv_to_subscriber_id_.find(imeisv);
    if (it != imeisv_to_subscriber_id_.end() && it->second != 0) {
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            return &record_it->second;
        }
    }
    return nullptr;
}

void S1apUeCorrelator::associateImsi(SubscriberRecord* subscriber, uint64_t subscriber_id, const std::string& imsi) {
    if (!subscriber) {
        std::cerr << "[ERROR] associateImsi: subscriber is null!" << std::endl;
        return;
    }
    if (subscriber_id == 0) {
        std::cerr << "[ERROR] associateImsi: subscriber_id is 0!" << std::endl;
        return;
    }
    
    // Remove old association if exists
    bool was_update = subscriber->imsi.has_value();
    std::string old_imsi;
    if (subscriber->imsi.has_value()) {
        old_imsi = subscriber->imsi.value();
        imsi_to_subscriber_id_.erase(subscriber->imsi.value());
    }
    
    subscriber->imsi = imsi;
    imsi_to_subscriber_id_[imsi] = subscriber_id;
    
    DEBUG_LOG << "[S1AP] associateImsi: Subscriber ID=" << subscriber_id;
    if (was_update) {
        DEBUG_LOG << " UPDATED IMSI from " << old_imsi << " to " << imsi;
    } else {
        DEBUG_LOG << " ADDED IMSI=" << imsi;
    }
    DEBUG_LOG << std::endl;
}

void S1apUeCorrelator::associateTmsi(SubscriberRecord* subscriber, uint64_t subscriber_id, const std::string& tmsi) {
    if (!subscriber) return;
    
    bool was_update = subscriber->tmsi.has_value();
    std::string old_tmsi;
    if (subscriber->tmsi.has_value()) {
        old_tmsi = subscriber->tmsi.value();
        tmsi_to_subscriber_id_.erase(subscriber->tmsi.value());
    }
    
    subscriber->tmsi = tmsi;
    tmsi_to_subscriber_id_[tmsi] = subscriber_id;
    
    DEBUG_LOG << "[S1AP] associateTmsi: Subscriber ID=" << subscriber_id;
    if (was_update) {
        DEBUG_LOG << " UPDATED TMSI from " << old_tmsi << " to " << tmsi;
    } else {
        DEBUG_LOG << " ADDED TMSI=" << tmsi;
    }
    DEBUG_LOG << std::endl;
}

void S1apUeCorrelator::associateEnbUeS1apId(SubscriberRecord* subscriber, uint64_t subscriber_id, uint32_t enb_ue_s1ap_id) {
    if (!subscriber) return;
    
    bool was_update = subscriber->enb_ue_s1ap_id.has_value();
    uint32_t old_enb_id = 0;
    if (subscriber->enb_ue_s1ap_id.has_value()) {
        old_enb_id = subscriber->enb_ue_s1ap_id.value();
    }
    
    // Check if this eNB-UE-S1AP-ID is already associated with a different subscriber
    auto existing_it = enb_ue_s1ap_id_to_subscriber_id_.find(enb_ue_s1ap_id);
    if (existing_it != enb_ue_s1ap_id_to_subscriber_id_.end() && existing_it->second != subscriber_id) {
        // Another subscriber already has this eNB-UE-S1AP-ID
        uint64_t other_subscriber_id = existing_it->second;
        auto other_record_it = subscriber_records_.find(other_subscriber_id);
        if (other_record_it != subscriber_records_.end()) {
            SubscriberRecord* other_subscriber = &other_record_it->second;
            // Update the mapping - the other subscriber will lose this ID
            other_subscriber->enb_ue_s1ap_id = std::nullopt;
            DEBUG_LOG << "[S1AP] associateEnbUeS1apId: CONFLICT - eNB-UE-S1AP-ID=" << enb_ue_s1ap_id 
                      << " was associated with subscriber ID=" << other_subscriber_id 
                      << ", now reassigning to subscriber ID=" << subscriber_id << std::endl;
        }
    }
    
    // Remove old association if subscriber had a different eNB-UE-S1AP-ID
    if (subscriber->enb_ue_s1ap_id.has_value() && subscriber->enb_ue_s1ap_id.value() != enb_ue_s1ap_id) {
        enb_ue_s1ap_id_to_subscriber_id_.erase(subscriber->enb_ue_s1ap_id.value());
    }
    
    subscriber->enb_ue_s1ap_id = enb_ue_s1ap_id;
    enb_ue_s1ap_id_to_subscriber_id_[enb_ue_s1ap_id] = subscriber_id;
    
    DEBUG_LOG << "[S1AP] associateEnbUeS1apId: Subscriber ID=" << subscriber_id;
    if (was_update && old_enb_id != enb_ue_s1ap_id) {
        DEBUG_LOG << " UPDATED eNB-UE-S1AP-ID from " << old_enb_id << " to " << enb_ue_s1ap_id;
    } else if (!was_update) {
        DEBUG_LOG << " ADDED eNB-UE-S1AP-ID=" << enb_ue_s1ap_id;
    } else {
        DEBUG_LOG << " eNB-UE-S1AP-ID=" << enb_ue_s1ap_id << " (unchanged)";
    }
    DEBUG_LOG << std::endl;
}

void S1apUeCorrelator::associateMmeUeS1apId(SubscriberRecord* subscriber, uint64_t subscriber_id, uint32_t mme_ue_s1ap_id) {
    if (!subscriber) return;
    
    bool was_update = subscriber->mme_ue_s1ap_id.has_value();
    uint32_t old_mme_id = 0;
    if (subscriber->mme_ue_s1ap_id.has_value()) {
        old_mme_id = subscriber->mme_ue_s1ap_id.value();
    }
    
    // Check if this MME-UE-S1AP-ID is already associated with a different subscriber
    auto existing_it = mme_ue_s1ap_id_to_subscriber_id_.find(mme_ue_s1ap_id);
    if (existing_it != mme_ue_s1ap_id_to_subscriber_id_.end() && existing_it->second != subscriber_id) {
        // Another subscriber already has this MME-UE-S1AP-ID
        uint64_t other_subscriber_id = existing_it->second;
        auto other_record_it = subscriber_records_.find(other_subscriber_id);
        if (other_record_it != subscriber_records_.end()) {
            SubscriberRecord* other_subscriber = &other_record_it->second;
            other_subscriber->mme_ue_s1ap_id = std::nullopt;
            DEBUG_LOG << "[S1AP] associateMmeUeS1apId: CONFLICT - MME-UE-S1AP-ID=" << mme_ue_s1ap_id 
                      << " was associated with subscriber ID=" << other_subscriber_id 
                      << ", now reassigning to subscriber ID=" << subscriber_id << std::endl;
        }
    }
    
    // Remove old association if subscriber had a different MME-UE-S1AP-ID
    if (subscriber->mme_ue_s1ap_id.has_value() && subscriber->mme_ue_s1ap_id.value() != mme_ue_s1ap_id) {
        mme_ue_s1ap_id_to_subscriber_id_.erase(subscriber->mme_ue_s1ap_id.value());
    }
    
    subscriber->mme_ue_s1ap_id = mme_ue_s1ap_id;
    mme_ue_s1ap_id_to_subscriber_id_[mme_ue_s1ap_id] = subscriber_id;
    
    DEBUG_LOG << "[S1AP] associateMmeUeS1apId: Subscriber ID=" << subscriber_id;
    if (was_update && old_mme_id != mme_ue_s1ap_id) {
        DEBUG_LOG << " UPDATED MME-UE-S1AP-ID from " << old_mme_id << " to " << mme_ue_s1ap_id;
    } else if (!was_update) {
        DEBUG_LOG << " ADDED MME-UE-S1AP-ID=" << mme_ue_s1ap_id;
    } else {
        DEBUG_LOG << " MME-UE-S1AP-ID=" << mme_ue_s1ap_id << " (unchanged)";
    }
    DEBUG_LOG << std::endl;
}

void S1apUeCorrelator::associateTeid(SubscriberRecord* subscriber, uint64_t subscriber_id, uint32_t teid) {
    if (!subscriber) {
        std::cerr << "[ERROR] associateTeid: subscriber is null! teid=0x" << std::hex << teid << std::dec << std::endl;
        return;
    }
    if (subscriber_id == 0) {
        std::cerr << "[ERROR] associateTeid: subscriber_id is 0! teid=0x" << std::hex << teid << std::dec << std::endl;
        return;
    }
    
    bool was_new = (subscriber->teids.find(teid) == subscriber->teids.end());
    
    // Remove old association if exists
    auto old_it = teid_to_subscriber_id_.find(teid);
    if (old_it != teid_to_subscriber_id_.end() && old_it->second != 0 && old_it->second != subscriber_id) {
        // Remove from old subscriber's TEID set
        uint64_t old_subscriber_id = old_it->second;
        auto record_it = subscriber_records_.find(old_it->second);
        if (record_it != subscriber_records_.end()) {
            record_it->second.teids.erase(teid);
            DEBUG_LOG << "[S1AP] associateTeid: CONFLICT - TEID=0x" << std::hex << teid << std::dec
                      << " was associated with subscriber ID=" << old_subscriber_id
                      << ", now reassigning to subscriber ID=" << subscriber_id << std::endl;
        } else {
            std::cerr << "[WARNING] associateTeid: old subscriber record not found! old_id=" << old_it->second << " teid=0x" << std::hex << teid << std::dec << std::endl;
        }
    }
    
    subscriber->teids.insert(teid);
    teid_to_subscriber_id_[teid] = subscriber_id;
    
    DEBUG_LOG << "[S1AP] associateTeid: Subscriber ID=" << subscriber_id;
    if (was_new) {
        DEBUG_LOG << " ADDED TEID=0x" << std::hex << teid << std::dec;
    } else {
        DEBUG_LOG << " TEID=0x" << std::hex << teid << std::dec << " (already present)";
    }
    DEBUG_LOG << std::endl;
}

void S1apUeCorrelator::associateImeisv(SubscriberRecord* subscriber, uint64_t subscriber_id, const std::string& imeisv) {
    if (!subscriber) return;
    
    bool was_update = subscriber->imeisv.has_value();
    std::string old_imeisv;
    if (subscriber->imeisv.has_value()) {
        old_imeisv = subscriber->imeisv.value();
        imeisv_to_subscriber_id_.erase(subscriber->imeisv.value());
    }
    
    subscriber->imeisv = imeisv;
    imeisv_to_subscriber_id_[imeisv] = subscriber_id;
    
    DEBUG_LOG << "[S1AP] associateImeisv: Subscriber ID=" << subscriber_id;
    if (was_update) {
        DEBUG_LOG << " UPDATED IMEISV from " << old_imeisv << " to " << imeisv;
    } else {
        DEBUG_LOG << " ADDED IMEISV=" << imeisv;
    }
    DEBUG_LOG << std::endl;
}

void S1apUeCorrelator::removeImsiAssociation(const std::string& imsi) {
    auto it = imsi_to_subscriber_id_.find(imsi);
    if (it != imsi_to_subscriber_id_.end() && it->second != 0) {
        uint64_t subscriber_id = it->second;
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            record_it->second.imsi = std::nullopt;
        }
        imsi_to_subscriber_id_.erase(it);
        DEBUG_LOG << "[S1AP] removeImsiAssociation: Removed IMSI=" << imsi 
                  << " from subscriber ID=" << subscriber_id << std::endl;
    }
}

void S1apUeCorrelator::removeTmsiAssociation(const std::string& tmsi) {
    auto it = tmsi_to_subscriber_id_.find(tmsi);
    if (it != tmsi_to_subscriber_id_.end() && it->second != 0) {
        uint64_t subscriber_id = it->second;
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            record_it->second.tmsi = std::nullopt;
        }
        tmsi_to_subscriber_id_.erase(it);
        DEBUG_LOG << "[S1AP] removeTmsiAssociation: Removed TMSI=" << tmsi 
                  << " from subscriber ID=" << subscriber_id << std::endl;
    }
}

void S1apUeCorrelator::removeEnbUeS1apIdAssociation(uint32_t enb_ue_s1ap_id) {
    auto it = enb_ue_s1ap_id_to_subscriber_id_.find(enb_ue_s1ap_id);
    if (it != enb_ue_s1ap_id_to_subscriber_id_.end() && it->second != 0) {
        uint64_t subscriber_id = it->second;
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            // Completely remove from record (UEContextReleaseComplete means context is released)
            record_it->second.enb_ue_s1ap_id = std::nullopt;
        }
        // Remove from mapping
        enb_ue_s1ap_id_to_subscriber_id_.erase(it);
        DEBUG_LOG << "[S1AP] removeEnbUeS1apIdAssociation: Removed eNB-UE-S1AP-ID=" << enb_ue_s1ap_id 
                  << " from subscriber ID=" << subscriber_id << " (mapping and record cleared)" << std::endl;
    }
}

void S1apUeCorrelator::removeMmeUeS1apIdAssociation(uint32_t mme_ue_s1ap_id) {
    auto it = mme_ue_s1ap_id_to_subscriber_id_.find(mme_ue_s1ap_id);
    if (it != mme_ue_s1ap_id_to_subscriber_id_.end() && it->second != 0) {
        uint64_t subscriber_id = it->second;
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            // Completely remove from record (UEContextReleaseComplete means context is released)
            record_it->second.mme_ue_s1ap_id = std::nullopt;
        }
        // Remove from mapping
        mme_ue_s1ap_id_to_subscriber_id_.erase(it);
        DEBUG_LOG << "[S1AP] removeMmeUeS1apIdAssociation: Removed MME-UE-S1AP-ID=" << mme_ue_s1ap_id 
                  << " from subscriber ID=" << subscriber_id << " (mapping and record cleared)" << std::endl;
    }
}

void S1apUeCorrelator::removeTeidAssociation(uint32_t teid) {
    auto it = teid_to_subscriber_id_.find(teid);
    if (it != teid_to_subscriber_id_.end() && it->second != 0) {
        uint64_t subscriber_id = it->second;
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            record_it->second.teids.erase(teid);
        }
        teid_to_subscriber_id_.erase(it);
        DEBUG_LOG << "[S1AP] removeTeidAssociation: Removed TEID=0x" << std::hex << teid << std::dec
                  << " from subscriber ID=" << subscriber_id << std::endl;
    }
}

void S1apUeCorrelator::removeImeisvAssociation(const std::string& imeisv) {
    auto it = imeisv_to_subscriber_id_.find(imeisv);
    if (it != imeisv_to_subscriber_id_.end() && it->second != 0) {
        uint64_t subscriber_id = it->second;
        auto record_it = subscriber_records_.find(it->second);
        if (record_it != subscriber_records_.end()) {
            record_it->second.imeisv = std::nullopt;
        }
        imeisv_to_subscriber_id_.erase(it);
        DEBUG_LOG << "[S1AP] removeImeisvAssociation: Removed IMEISV=" << imeisv 
                  << " from subscriber ID=" << subscriber_id << std::endl;
    }
}

std::optional<S1apUeCorrelator::SubscriberIdentifiers> S1apUeCorrelator::getIdentifiersByImsi(const std::string& imsi) {
    auto* subscriber = getSubscriberByImsi(imsi);
    if (!subscriber) {
        return std::nullopt;
    }
    
    SubscriberIdentifiers identifiers;
    identifiers.imsi = subscriber->imsi;
    identifiers.tmsi = subscriber->tmsi;
    identifiers.enb_ue_s1ap_id = subscriber->enb_ue_s1ap_id;
    identifiers.mme_ue_s1ap_id = subscriber->mme_ue_s1ap_id;
    identifiers.teids.assign(subscriber->teids.begin(), subscriber->teids.end());
    identifiers.imeisv = subscriber->imeisv;
    
    return identifiers;
}

std::vector<uint32_t> S1apUeCorrelator::getTeidsByImsi(const std::string& imsi) {
    auto* subscriber = getSubscriberByImsi(imsi);
    if (!subscriber) {
        return {};
    }
    return std::vector<uint32_t>(subscriber->teids.begin(), subscriber->teids.end());
}

std::vector<uint32_t> S1apUeCorrelator::getTeidsByTmsi(const std::string& tmsi) {
    auto* subscriber = getSubscriberByTmsi(tmsi);
    if (!subscriber) {
        return {};
    }
    return std::vector<uint32_t>(subscriber->teids.begin(), subscriber->teids.end());
}

std::vector<uint32_t> S1apUeCorrelator::getTeidsByImeisv(const std::string& imeisv) {
    auto* subscriber = getSubscriberByImeisv(imeisv);
    if (!subscriber) {
        return {};
    }
    return std::vector<uint32_t>(subscriber->teids.begin(), subscriber->teids.end());
}

} // namespace s1ap_correlator

