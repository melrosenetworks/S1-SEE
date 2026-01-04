/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: correlator.cc
 * Description: Implementation of the Correlator class for managing UE (User Equipment)
 *              contexts and correlating S1AP messages to subscribers. Integrates with
 *              S1apUeCorrelator to maintain UE state, extract identifiers, and generate
 *              composite subscriber keys for event correlation.
 */

#include "s1see/correlate/correlator.h"
#include "s1ap_ue_correlator.h"
#include "s1ap_parser.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <memory>
#include <iostream>

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

// Helper function to convert hex string to bytes
static std::vector<uint8_t> hex_string_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (i + 1 < hex.length()) {
            std::string byte_str = hex.substr(i, 2);
            bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
        }
    }
    return bytes;
}

// Helper function to extract S1AP IDs from UE-S1AP-IDs IE
// UE-S1AP-IDs contains MME-UE-S1AP-ID in first 4 bytes and eNB-UE-S1AP-ID in second 4 bytes
static std::pair<std::optional<uint32_t>, std::optional<uint32_t>> 
extract_ids_from_ue_s1ap_ids(const std::string& hex_value) {
    std::optional<uint32_t> mme_id = std::nullopt;
    std::optional<uint32_t> enb_id = std::nullopt;
    
    if (hex_value.empty()) {
        return {mme_id, enb_id};
    }
    
    try {
        // Convert hex string to bytes
        std::vector<uint8_t> bytes = hex_string_to_bytes(hex_value);
        
        // UE-S1AP-IDs should be 8 bytes (4 bytes for MME-UE-S1AP-ID + 4 bytes for eNB-UE-S1AP-ID)
        if (bytes.size() >= 8) {
            // Extract MME-UE-S1AP-ID from first 4 bytes (big-endian)
            mme_id = (static_cast<uint32_t>(bytes[0]) << 24) |
                     (static_cast<uint32_t>(bytes[1]) << 16) |
                     (static_cast<uint32_t>(bytes[2]) << 8) |
                     static_cast<uint32_t>(bytes[3]);
            
            // Extract eNB-UE-S1AP-ID from next 4 bytes (big-endian)
            enb_id = (static_cast<uint32_t>(bytes[4]) << 24) |
                     (static_cast<uint32_t>(bytes[5]) << 16) |
                     (static_cast<uint32_t>(bytes[6]) << 8) |
                     static_cast<uint32_t>(bytes[7]);
        }
    } catch (const std::exception& e) {
        // Failed to parse UE-S1AP-IDs - silently continue
    }
    
    return {mme_id, enb_id};
}

Correlator::Correlator(const Config& config) : config_(config) {
    s1ap_correlator_ = std::make_unique<s1ap_correlator::S1apUeCorrelator>();
}

std::string Correlator::get_or_create_context(const CanonicalMessage& message) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // Convert CanonicalMessage to S1apParseResult format
    s1ap_parser::S1apParseResult s1ap_result;
    s1ap_result.procedure_code = static_cast<uint8_t>(message.procedure_code());
    s1ap_result.decoded = !message.decode_failed();
    s1ap_result.procedure_name = message.msg_type();
    
    // Convert information_elements from decoded_tree if available
    // We need to parse the JSON to extract IEs, or we can reconstruct from CanonicalMessage fields
    // For now, we'll extract what we can from the message and use the raw bytes
    if (!message.raw_bytes().empty()) {
        s1ap_result.raw_bytes.assign(message.raw_bytes().begin(), message.raw_bytes().end());
    }
    
    // Try to parse the decoded_tree JSON to get information_elements
    if (!message.decoded_tree().empty()) {
        // Simple JSON parsing for information_elements
        // This is a simplified parser - in production you'd use a proper JSON library
        std::string json = message.decoded_tree();
        size_t ie_pos = json.find("\"information_elements\":{");
        if (ie_pos != std::string::npos) {
            size_t start = ie_pos + 24; // Length of "\"information_elements\":{"
            size_t end = json.find("}", start);
            if (end != std::string::npos) {
                std::string ie_section = json.substr(start, end - start);
                // Parse key-value pairs
                size_t pos = 0;
                while (pos < ie_section.length()) {
                    size_t key_start = ie_section.find("\"", pos);
                    if (key_start == std::string::npos) break;
                    size_t key_end = ie_section.find("\"", key_start + 1);
                    if (key_end == std::string::npos) break;
                    std::string key = ie_section.substr(key_start + 1, key_end - key_start - 1);
                    
                    size_t val_start = ie_section.find(":\"", key_end);
                    if (val_start == std::string::npos) break;
                    size_t val_end = ie_section.find("\"", val_start + 2);
                    if (val_end == std::string::npos) break;
                    std::string value = ie_section.substr(val_start + 2, val_end - val_start - 2);
                    
                    s1ap_result.information_elements[key] = value;
                    pos = val_end + 1;
                }
            }
        }
    }
    
    // Add S1AP IDs to information_elements if not already present
    if (message.mme_ue_s1ap_id() != 0) {
        std::ostringstream hex;
        hex << std::hex << std::setfill('0') << std::setw(8) << message.mme_ue_s1ap_id();
        if (s1ap_result.information_elements.find("MME-UE-S1AP-ID") == s1ap_result.information_elements.end()) {
            s1ap_result.information_elements["MME-UE-S1AP-ID"] = hex.str();
        }
    }
    if (message.enb_ue_s1ap_id() != 0) {
        std::ostringstream hex;
        hex << std::hex << std::setfill('0') << std::setw(6) << message.enb_ue_s1ap_id();
        if (s1ap_result.information_elements.find("eNB-UE-S1AP-ID") == s1ap_result.information_elements.end()) {
            s1ap_result.information_elements["eNB-UE-S1AP-ID"] = hex.str();
        }
    }
    
    // Process frame through S1apUeCorrelator
    // This will create/update a subscriber record if identifiers are found
    uint32_t frame_no = static_cast<uint32_t>(message.frame_number()); // Use frame_number if available
    double timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    s1ap_correlator::SubscriberRecord* subscriber_from_process = s1ap_correlator_->processS1apFrame(frame_no, s1ap_result, timestamp);
    
    // Extract identifiers from current message
    std::optional<std::string> imsi = message.imsi().empty() ? std::nullopt : std::make_optional(message.imsi());
    std::optional<std::string> tmsi = message.tmsi().empty() ? std::nullopt : std::make_optional(message.tmsi());
    std::optional<uint32_t> enb_ue_s1ap_id = std::nullopt;
    std::optional<uint32_t> mme_ue_s1ap_id = std::nullopt;
    std::optional<std::string> imeisv = message.imei().empty() ? std::nullopt : std::make_optional(message.imei());
    
    // First, try to extract S1AP IDs from UE-S1AP-IDs IE (contains both IDs in one field)
    auto ue_s1ap_ids_it = s1ap_result.information_elements.find("UE-S1AP-IDs");
    if (ue_s1ap_ids_it != s1ap_result.information_elements.end()) {
        const std::string& ue_s1ap_ids_hex = ue_s1ap_ids_it->second;
        
        auto [mme_id, enb_id] = extract_ids_from_ue_s1ap_ids(ue_s1ap_ids_hex);
        if (mme_id.has_value()) {
            mme_ue_s1ap_id = mme_id;
        }
        if (enb_id.has_value()) {
            enb_ue_s1ap_id = enb_id;
        }
    }
    
    // If UE-S1AP-IDs was not found or failed, try individual IEs or message fields
    if (!mme_ue_s1ap_id.has_value()) {
        // Try individual MME-UE-S1AP-ID IE first
        auto mme_id_it = s1ap_result.information_elements.find("MME-UE-S1AP-ID");
        if (mme_id_it != s1ap_result.information_elements.end()) {
            const std::string& mme_id_hex = mme_id_it->second;
            try {
                std::string hex_str = mme_id_hex;
                if (hex_str.length() > 2 && hex_str.substr(0, 2) == "0x") {
                    hex_str = hex_str.substr(2);
                }
                mme_ue_s1ap_id = static_cast<uint32_t>(std::stoul(hex_str, nullptr, 16));
            } catch (...) {}
        }
        // Fallback to message field
        if (!mme_ue_s1ap_id.has_value() && message.mme_ue_s1ap_id() != 0) {
            mme_ue_s1ap_id = static_cast<uint32_t>(message.mme_ue_s1ap_id());
        }
    }
    
    if (!enb_ue_s1ap_id.has_value()) {
        // Try individual eNB-UE-S1AP-ID IE first
        auto enb_id_it = s1ap_result.information_elements.find("eNB-UE-S1AP-ID");
        if (enb_id_it != s1ap_result.information_elements.end()) {
            const std::string& enb_id_hex = enb_id_it->second;
            try {
                std::string hex_str = enb_id_hex;
                if (hex_str.length() > 2 && hex_str.substr(0, 2) == "0x") {
                    hex_str = hex_str.substr(2);
                }
                enb_ue_s1ap_id = static_cast<uint32_t>(std::stoul(hex_str, nullptr, 16));
            } catch (...) {}
        }
        // Fallback to message field
        if (!enb_ue_s1ap_id.has_value() && message.enb_ue_s1ap_id() != 0) {
            enb_ue_s1ap_id = static_cast<uint32_t>(message.enb_ue_s1ap_id());
        }
    }
    
    // Check if we already have a context that matches by any identifier
    // If so, merge identifiers from existing context with message identifiers
    std::shared_ptr<UEContext> existing_context = nullptr;
    std::string existing_key;
    
    // Try to find existing context by any identifier from the message
    for (const auto& [key, context] : contexts_) {
        bool matches = false;
        
        // Check IMSI match
        if (imsi.has_value() && context->imsi.has_value() && 
            imsi.value() == context->imsi.value()) {
            matches = true;
        }
        // Check TMSI match
        else if (tmsi.has_value() && context->tmsi.has_value() && 
                 tmsi.value() == context->tmsi.value()) {
            matches = true;
        }
        // Check MME-UE-S1AP-ID match
        else if (mme_ue_s1ap_id.has_value() && context->mme_ue_s1ap_id.has_value() && 
                 mme_ue_s1ap_id.value() == context->mme_ue_s1ap_id.value()) {
            matches = true;
        }
        // Check eNB-UE-S1AP-ID match
        else if (enb_ue_s1ap_id.has_value() && context->enb_ue_s1ap_id.has_value() && 
                 enb_ue_s1ap_id.value() == context->enb_ue_s1ap_id.value()) {
            matches = true;
        }
        // Check IMEI match
        else if (imeisv.has_value() && context->imei.has_value() && 
                 imeisv.value() == context->imei.value()) {
            matches = true;
        }
        
        if (matches) {
            existing_context = context;
            existing_key = key;
            break;
        }
    }
    
    // Merge identifiers: use existing context identifiers if message doesn't have them
    if (existing_context) {
        // Merge IMSI: use message if available, otherwise keep existing
        if (!imsi.has_value() && existing_context->imsi.has_value()) {
            imsi = existing_context->imsi.value();
        }
        // Merge TMSI: use message if available, otherwise keep existing
        if (!tmsi.has_value() && existing_context->tmsi.has_value()) {
            tmsi = existing_context->tmsi.value();
        }
        // Merge MME-UE-S1AP-ID: use message if available, otherwise keep existing
        if (!mme_ue_s1ap_id.has_value() && existing_context->mme_ue_s1ap_id.has_value()) {
            mme_ue_s1ap_id = existing_context->mme_ue_s1ap_id.value();
        }
        // Merge eNB-UE-S1AP-ID: use message if available, otherwise keep existing
        if (!enb_ue_s1ap_id.has_value() && existing_context->enb_ue_s1ap_id.has_value()) {
            enb_ue_s1ap_id = existing_context->enb_ue_s1ap_id.value();
        }
        // Merge IMEI: use message if available, otherwise keep existing
        if (!imeisv.has_value() && existing_context->imei.has_value()) {
            imeisv = existing_context->imei.value();
        }
    }
    
    // Check if we have any identifiers at all (from message or merged from existing context)
    bool has_any_identifier = imsi.has_value() || tmsi.has_value() || 
                              mme_ue_s1ap_id.has_value() || enb_ue_s1ap_id.has_value() || 
                              imeisv.has_value();
    
    if (!has_any_identifier) {
        // No identifiers available - cannot correlate, do not process further
        return "";
    }
    
    // For UEContextReleaseComplete, don't create new subscribers - only update existing ones
    bool is_release_complete = (message.msg_type() == "UEContextReleaseComplete");
    
    // Use the subscriber from processS1apFrame if it was created/updated
    // Only call getOrCreateSubscriber/getSubscriberBy* if processS1apFrame didn't find one
    s1ap_correlator::SubscriberRecord* subscriber = subscriber_from_process;
    
    if (!subscriber) {
        // processS1apFrame didn't create/update a subscriber (no identifiers in S1AP frame)
        // Try to find one using identifiers from the message or existing context
        if (is_release_complete) {
            // Only try to get existing subscriber, don't create new one
            if (imsi.has_value()) {
                subscriber = s1ap_correlator_->getSubscriberByImsi(imsi.value());
            } else if (tmsi.has_value()) {
                subscriber = s1ap_correlator_->getSubscriberByTmsi(tmsi.value());
            } else if (mme_ue_s1ap_id.has_value()) {
                subscriber = s1ap_correlator_->getSubscriberByMmeUeS1apId(mme_ue_s1ap_id.value());
            } else if (enb_ue_s1ap_id.has_value()) {
                subscriber = s1ap_correlator_->getSubscriberByEnbUeS1apId(enb_ue_s1ap_id.value());
            } else if (imeisv.has_value()) {
                subscriber = s1ap_correlator_->getSubscriberByImeisv(imeisv.value());
            }
            
            // If no existing subscriber found, and no existing context, return early
            if (!subscriber && !existing_context) {
                return "";
            }
        } else {
            // For other messages, call getOrCreateSubscriber with ALL available identifiers (existing + new)
            // This ensures the correlator can locate the existing record using any identifier
            // Only do this if processS1apFrame didn't already create/update a subscriber
            subscriber = s1ap_correlator_->getOrCreateSubscriber(
                imsi, tmsi, enb_ue_s1ap_id, mme_ue_s1ap_id, std::nullopt, imeisv);
        }
    }
    
    if (!subscriber) {
        // For UEContextReleaseComplete, if no subscriber found, return early
        if (is_release_complete) {
            return "";
        }
        // Fallback: create a context with minimal information
        std::string key = "unknown_" + std::to_string(next_unknown_id_++);
        auto context = std::make_shared<UEContext>();
        context->update(message);
        context->subscriber_key = key;
        contexts_[key] = context;
        return key;
    }
    
    // Generate subscriber key from ALL identifiers in the merged subscriber record
    std::string subscriber_key;
    if (subscriber->imsi.has_value()) {
        subscriber_key = "imsi:" + subscriber->imsi.value();
    } else if (subscriber->tmsi.has_value()) {
        subscriber_key = "tmsi:" + subscriber->tmsi.value();
    } else if (subscriber->mme_ue_s1ap_id.has_value()) {
        subscriber_key = "mme_ue_s1ap_id:" + std::to_string(subscriber->mme_ue_s1ap_id.value());
    } else if (subscriber->enb_ue_s1ap_id.has_value()) {
        subscriber_key = "enb_ue_s1ap_id:" + std::to_string(subscriber->enb_ue_s1ap_id.value());
    } else {
        subscriber_key = "unknown_" + std::to_string(next_unknown_id_++);
    }
    
    // If we found an existing context, check if we need to update the key
    if (existing_context) {
        // If the new key is better (e.g., we now have IMSI), move context to new key
        if (subscriber_key != existing_key) {
            // Check if new key is "better" (IMSI > TMSI > MME > eNB)
            bool new_key_better = false;
            if (subscriber_key.find("imsi:") == 0 && existing_key.find("imsi:") != 0) {
                new_key_better = true;
            } else if (subscriber_key.find("tmsi:") == 0 && 
                      existing_key.find("imsi:") != 0 && existing_key.find("tmsi:") != 0) {
                new_key_better = true;
            } else if (subscriber_key.find("mme_ue_s1ap_id:") == 0 && 
                      existing_key.find("unknown_") == 0) {
                new_key_better = true;
            } else if (subscriber_key.find("enb_ue_s1ap_id:") == 0 && 
                      existing_key.find("unknown_") == 0) {
                new_key_better = true;
            }
            
            if (new_key_better) {
                // Move context to new key
                existing_context->subscriber_key = subscriber_key;
                contexts_[subscriber_key] = existing_context;
                contexts_.erase(existing_key);
                update_context_from_subscriber(existing_context, subscriber, message);
                return subscriber_key;
            } else {
                // Keep existing key, but update the context
                update_context_from_subscriber(existing_context, subscriber, message);
                return existing_key;
            }
        } else {
            // Same key, just update
            update_context_from_subscriber(existing_context, subscriber, message);
            return subscriber_key;
        }
    }
    
    // No existing context found - create new one (unless it's a release complete)
    if (is_release_complete) {
        // For UEContextReleaseComplete, don't create new context if none exists
        return "";
    }
    
    auto context = std::make_shared<UEContext>();
    update_context_from_subscriber(context, subscriber, message);
    context->subscriber_key = subscriber_key;
    contexts_[subscriber_key] = context;
    
    return subscriber_key;
}

void Correlator::update_context_from_subscriber(
    std::shared_ptr<UEContext> context,
    const s1ap_correlator::SubscriberRecord* subscriber,
    const CanonicalMessage& message) {
    
    // Update from SubscriberRecord
    if (subscriber->imsi.has_value()) {
        context->imsi = subscriber->imsi.value();
    }
    if (subscriber->tmsi.has_value()) {
        context->tmsi = subscriber->tmsi.value();
    }
    if (subscriber->mme_ue_s1ap_id.has_value()) {
        context->mme_ue_s1ap_id = subscriber->mme_ue_s1ap_id.value();
    }
    if (subscriber->enb_ue_s1ap_id.has_value()) {
        context->enb_ue_s1ap_id = subscriber->enb_ue_s1ap_id.value();
    }
    if (subscriber->imeisv.has_value()) {
        context->imei = subscriber->imeisv.value();
    }
    
    // Update from message (for fields not in SubscriberRecord)
    if (!message.ecgi().empty()) {
        context->ecgi = message.ecgi();
    }
    if (!message.target_ecgi().empty()) {
        context->target_ecgi = message.target_ecgi();
    }
    if (!message.guti().empty()) {
        context->guti = message.guti();
    }
    if (!message.mme_id().empty()) {
        context->mme_id = message.mme_id();
    }
    if (!message.enb_id().empty()) {
        context->enb_id = message.enb_id();
    }
    if (!message.msg_type().empty()) {
        context->last_procedure = message.msg_type();
    }
    
    context->last_seen = std::chrono::system_clock::now();
    context->update_composite_keys();
    
    // Handle UEContextReleaseComplete: remove S1AP IDs as the LAST step after all processing
    // This ensures all correlation and updates are complete before removing the IDs
    if (message.msg_type() == "UEContextReleaseComplete") {
        // Remove MME-UE-S1AP-ID from context and subscriber record
        if (context->mme_ue_s1ap_id.has_value()) {
            uint32_t mme_id = context->mme_ue_s1ap_id.value();
            s1ap_correlator_->removeMmeUeS1apIdAssociation(mme_id);
            context->mme_ue_s1ap_id = std::nullopt;
        }
        // Remove eNB-UE-S1AP-ID from context and subscriber record
        if (context->enb_ue_s1ap_id.has_value()) {
            uint32_t enb_id = context->enb_ue_s1ap_id.value();
            s1ap_correlator_->removeEnbUeS1apIdAssociation(enb_id);
            context->enb_ue_s1ap_id = std::nullopt;
        }
    }
}

void Correlator::update_context(const CanonicalMessage& message) {
    get_or_create_context(message);
}

std::shared_ptr<UEContext> Correlator::get_context(const std::string& subscriber_key) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = contexts_.find(subscriber_key);
    if (it != contexts_.end()) {
        return it->second;
    }
    return nullptr;
}

void Correlator::cleanup_expired() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = contexts_.begin();
    while (it != contexts_.end()) {
        if (it->second->is_expired(config_.context_expiry)) {
            it = contexts_.erase(it);
        } else {
            ++it;
        }
    }
}

void Correlator::dump_ue_records(std::ostream& os) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    os << "\n=== UE Records Dump ===" << std::endl;
    os << "Total UE contexts: " << contexts_.size() << std::endl;
    os << std::endl;
    
    for (const auto& [subscriber_key, context] : contexts_) {
        os << "Subscriber Key: " << subscriber_key << std::endl;
        
        if (context->imsi.has_value()) {
            os << "  IMSI: " << context->imsi.value() << std::endl;
        }
        if (context->guti.has_value()) {
            os << "  GUTI: " << context->guti.value() << std::endl;
        }
        if (context->tmsi.has_value()) {
            os << "  TMSI: " << context->tmsi.value() << std::endl;
        }
        if (context->imei.has_value()) {
            os << "  IMEI: " << context->imei.value() << std::endl;
        }
        
        if (context->mme_ue_s1ap_id.has_value()) {
            os << "  MME-UE-S1AP-ID: " << context->mme_ue_s1ap_id.value() << std::endl;
        }
        if (context->enb_ue_s1ap_id.has_value()) {
            os << "  eNB-UE-S1AP-ID: " << context->enb_ue_s1ap_id.value() << std::endl;
        }
        
        if (context->mme_id.has_value()) {
            os << "  MME ID: " << context->mme_id.value() << std::endl;
        }
        if (context->enb_id.has_value()) {
            os << "  eNB ID: " << context->enb_id.value() << std::endl;
        }
        if (context->mme_group_id.has_value()) {
            os << "  MME Group ID: " << context->mme_group_id.value() << std::endl;
        }
        if (context->mme_code.has_value()) {
            os << "  MME Code: " << context->mme_code.value() << std::endl;
        }
        
        if (!context->ecgi.empty()) {
            os << "  ECGI: " << bytes_to_hex_string(context->ecgi) << std::endl;
        }
        if (!context->source_ecgi.empty()) {
            os << "  Source ECGI: " << bytes_to_hex_string(context->source_ecgi) << std::endl;
        }
        if (!context->target_ecgi.empty()) {
            os << "  Target ECGI: " << bytes_to_hex_string(context->target_ecgi) << std::endl;
        }
        
        if (!context->last_procedure.empty()) {
            os << "  Last Procedure: " << context->last_procedure << std::endl;
        }
        
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - context->last_seen);
        os << "  Last Seen: " << age.count() << " seconds ago" << std::endl;
        
        if (context->handover_in_progress) {
            os << "  Handover In Progress: true" << std::endl;
            auto ho_age = std::chrono::duration_cast<std::chrono::seconds>(now - context->handover_start_time);
            os << "  Handover Started: " << ho_age.count() << " seconds ago" << std::endl;
        }
        
        os << std::endl;
    }
    
    // Dump subscribers from S1apUeCorrelator
    os << "\n=== S1apUeCorrelator Subscribers Dump ===" << std::endl;
    const auto& all_subscribers = s1ap_correlator_->getAllSubscribers();
    os << "Total subscribers: " << all_subscribers.size() << std::endl;
    os << std::endl;
    
    for (const auto& [subscriber_id, subscriber] : all_subscribers) {
        os << "Subscriber ID: " << subscriber_id << std::endl;
        
        if (subscriber.imsi.has_value()) {
            os << "  IMSI: " << subscriber.imsi.value() << std::endl;
        }
        if (subscriber.tmsi.has_value()) {
            os << "  TMSI: " << subscriber.tmsi.value() << std::endl;
        }
        if (subscriber.imeisv.has_value()) {
            os << "  IMEISV: " << subscriber.imeisv.value() << std::endl;
        }
        
        if (subscriber.mme_ue_s1ap_id.has_value()) {
            os << "  MME-UE-S1AP-ID: " << subscriber.mme_ue_s1ap_id.value() << std::endl;
        }
        if (subscriber.enb_ue_s1ap_id.has_value()) {
            os << "  eNB-UE-S1AP-ID: " << subscriber.enb_ue_s1ap_id.value() << std::endl;
        }
        
        if (!subscriber.teids.empty()) {
            os << "  TEIDs: ";
            bool first = true;
            for (uint32_t teid : subscriber.teids) {
                if (!first) os << ", ";
                os << "0x" << std::hex << teid << std::dec;
                first = false;
            }
            os << std::endl;
        }
        
        if (subscriber.first_seen_timestamp.has_value()) {
            auto first_seen_time = std::chrono::system_clock::from_time_t(
                static_cast<time_t>(subscriber.first_seen_timestamp.value()));
            auto first_seen_tp = std::chrono::time_point_cast<std::chrono::seconds>(first_seen_time);
            std::time_t first_seen_tt = std::chrono::system_clock::to_time_t(first_seen_tp);
            os << "  First Seen: " << std::put_time(std::localtime(&first_seen_tt), "%Y-%m-%d %H:%M:%S") << std::endl;
        }
        if (subscriber.last_seen_timestamp.has_value()) {
            auto last_seen_time = std::chrono::system_clock::from_time_t(
                static_cast<time_t>(subscriber.last_seen_timestamp.value()));
            auto last_seen_tp = std::chrono::time_point_cast<std::chrono::seconds>(last_seen_time);
            std::time_t last_seen_tt = std::chrono::system_clock::to_time_t(last_seen_tp);
            os << "  Last Seen: " << std::put_time(std::localtime(&last_seen_tt), "%Y-%m-%d %H:%M:%S") << std::endl;
        }
        
        if (subscriber.gps_data_available) {
            os << "  GPS Data Available: true" << std::endl;
            if (subscriber.gps_latitude.has_value() && subscriber.gps_longitude.has_value()) {
                os << "  GPS Location: " << std::fixed << std::setprecision(6) 
                   << subscriber.gps_latitude.value() << ", " 
                   << subscriber.gps_longitude.value() << std::endl;
            }
            if (subscriber.gps_altitude.has_value()) {
                os << "  GPS Altitude: " << subscriber.gps_altitude.value() << " m" << std::endl;
            }
        }
        
        os << std::endl;
    }
    
    os << "=== End S1apUeCorrelator Subscribers Dump ===" << std::endl;
    os << "\n=== End UE Records Dump ===" << std::endl;
}

} // namespace correlate
} // namespace s1see
