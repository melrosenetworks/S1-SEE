/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: rule_engine.cc
 * Description: Implementation of the RuleEngine class for processing canonical messages
 *              against rule sets. Evaluates single-message rules and sequence rules,
 *              extracts event data from messages and contexts, and generates events
 *              when rules match.
 */

#include "s1see/rules/rule_engine.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "spool_record.pb.h"

namespace s1see {
namespace rules {

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

RuleEngine::RuleEngine(std::shared_ptr<correlate::Correlator> correlator)
    : correlator_(correlator) {
}

void RuleEngine::load_ruleset(const Ruleset& ruleset) {
    rulesets_.push_back(ruleset);
}

std::vector<Event> RuleEngine::process(const CanonicalMessage& message) {
    std::vector<Event> events;
    
    // Get subscriber key ONCE and cache it to avoid calling get_or_create_context multiple times
    // This prevents processS1apFrame from being called multiple times for the same message
    std::string subscriber_key = correlator_->get_or_create_context(message);
    
    // Check all rulesets
    for (const auto& ruleset : rulesets_) {
        // Check single message rules - pass subscriber_key to avoid re-calling get_or_create_context
        auto single_events = check_single_message_rules(message, ruleset, subscriber_key);
        events.insert(events.end(), single_events.begin(), single_events.end());
        
        // Check sequence rules - pass subscriber_key to avoid re-calling get_or_create_context
        auto seq_events = check_sequence_rules(message, ruleset, subscriber_key);
        events.insert(events.end(), seq_events.begin(), seq_events.end());
    }
    
    return events;
}

std::vector<Event> RuleEngine::check_single_message_rules(const CanonicalMessage& message,
                                                          const Ruleset& ruleset,
                                                          const std::string& subscriber_key) {
    std::vector<Event> events;
    
    for (const auto& rule : ruleset.single_message_rules) {
        if (message.msg_type() == rule.msg_type_pattern) {
            Event event = create_event(rule.event_name, message, rule.attributes,
                                      ruleset.id, ruleset.version, subscriber_key);
            
            // Extract event data based on rule specifications
            for (const auto& extraction : rule.event_data) {
                std::string value = extract_event_data_value(extraction.source_expression, message, nullptr, subscriber_key);
                if (!value.empty()) {
                    (*event.mutable_attributes())[extraction.target_attribute] = value;
                }
            }
            
            events.push_back(event);
        }
    }
    
    return events;
}

std::vector<Event> RuleEngine::check_sequence_rules(const CanonicalMessage& message,
                                                     const Ruleset& ruleset,
                                                     const std::string& subscriber_key) {
    std::vector<Event> events;
    
    // Cleanup expired sequences first
    cleanup_expired_sequences();
    
    auto& sequences = sequence_states_[subscriber_key];
    
    for (const auto& rule : ruleset.sequence_rules) {
        if (message.msg_type() == rule.first_msg_type) {
            // Start new sequence
            SequenceState state;
            state.subscriber_key = subscriber_key;
            state.first_msg_type = rule.first_msg_type;
            state.first_message = message;
            state.first_seen = std::chrono::system_clock::now();
            state.ruleset_id = ruleset.id;
            state.ruleset_version = ruleset.version;
            sequences.push_back(state);
        } else if (message.msg_type() == rule.second_msg_type) {
            // Check for matching first message
            auto it = sequences.begin();
            while (it != sequences.end()) {
                if (it->first_msg_type == rule.first_msg_type) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now() - it->first_seen);
                    
                    if (elapsed <= rule.time_window) {
                        // Sequence matched!
                        Event event = create_event(rule.event_name, message, rule.attributes,
                                                  ruleset.id, ruleset.version, subscriber_key);
                        
                        // Extract event data based on rule specifications
                        for (const auto& extraction : rule.event_data) {
                            std::string value = extract_event_data_value(extraction.source_expression, message, &it->first_message, subscriber_key);
                            if (!value.empty()) {
                                (*event.mutable_attributes())[extraction.target_attribute] = value;
                            }
                        }
                        
                        // Add evidence from first message
                        SpoolOffset first_offset;
                        first_offset.set_partition(it->first_message.spool_partition());
                        first_offset.set_offset(it->first_message.spool_offset());
                        if (it->first_message.frame_number() != 0) {
                            first_offset.set_frame_number(it->first_message.frame_number());
                        }
                        *event.mutable_evidence()->add_offsets() = first_offset;
                        
                        // Add evidence from current message
                        SpoolOffset current_offset;
                        current_offset.set_partition(message.spool_partition());
                        current_offset.set_offset(message.spool_offset());
                        if (message.frame_number() != 0) {
                            current_offset.set_frame_number(message.frame_number());
                        }
                        *event.mutable_evidence()->add_offsets() = current_offset;
                        
                        events.push_back(event);
                        it = sequences.erase(it);
                    } else {
                        // Expired
                        ++it;
                    }
                } else {
                    ++it;
                }
            }
        }
    }
    
    return events;
}

Event RuleEngine::create_event(const std::string& name,
                               const CanonicalMessage& message,
                               const std::map<std::string, std::string>& attributes,
                               const std::string& ruleset_id,
                               const std::string& ruleset_version,
                               const std::string& subscriber_key) {
    Event event;
    event.set_name(name);
    event.set_ts(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    // Use the provided subscriber_key instead of calling get_or_create_context again
    event.set_subscriber_key(subscriber_key);
    
    // Add attributes
    for (const auto& [key, value] : attributes) {
        (*event.mutable_attributes())[key] = value;
    }
    
    // Add message-derived attributes
    (*event.mutable_attributes())["msg_type"] = message.msg_type();
    if (!message.ecgi().empty()) {
        (*event.mutable_attributes())["ecgi"] = bytes_to_hex_string(message.ecgi());
    }
    
    event.set_confidence(1.0); // Explicit events have full confidence
    
    // Add evidence
    SpoolOffset offset;
    offset.set_partition(message.spool_partition());
    offset.set_offset(message.spool_offset());
    if (message.frame_number() != 0) {
        offset.set_frame_number(message.frame_number());
    }
    *event.mutable_evidence()->add_offsets() = offset;
    
    event.set_ruleset_id(ruleset_id);
    event.set_ruleset_version(ruleset_version);
    
    return event;
}

std::string RuleEngine::extract_event_data_value(const std::string& expression,
                                                 const CanonicalMessage& message,
                                                 const CanonicalMessage* first_message,
                                                 const std::string& subscriber_key) {
    // Parse expression: "message.field" or "first_message.field" or "context.field"
    size_t dot_pos = expression.find('.');
    if (dot_pos == std::string::npos) {
        return ""; // Invalid expression
    }
    
    std::string source = expression.substr(0, dot_pos);
    std::string field = expression.substr(dot_pos + 1);
    
    std::string value;
    
    if (source == "message") {
        // Extract from current message
        if (field == "ecgi" && !message.ecgi().empty()) {
            value = bytes_to_hex_string(message.ecgi());
        } else if (field == "target_ecgi" && !message.target_ecgi().empty()) {
            value = bytes_to_hex_string(message.target_ecgi());
        } else if (field == "mme_ue_s1ap_id" && message.mme_ue_s1ap_id() != 0) {
            value = std::to_string(message.mme_ue_s1ap_id());
        } else if (field == "enb_ue_s1ap_id" && message.enb_ue_s1ap_id() != 0) {
            value = std::to_string(message.enb_ue_s1ap_id());
        } else if (field == "imsi" && !message.imsi().empty()) {
            value = message.imsi();
        } else if (field == "tmsi" && !message.tmsi().empty()) {
            value = message.tmsi();
        } else if (field == "msg_type" && !message.msg_type().empty()) {
            value = message.msg_type();
        }
    } else if (source == "first_message" && first_message) {
        // Extract from first message (sequence rules only)
        if (field == "ecgi" && !first_message->ecgi().empty()) {
            value = bytes_to_hex_string(first_message->ecgi());
        } else if (field == "target_ecgi" && !first_message->target_ecgi().empty()) {
            value = bytes_to_hex_string(first_message->target_ecgi());
        } else if (field == "mme_ue_s1ap_id" && first_message->mme_ue_s1ap_id() != 0) {
            value = std::to_string(first_message->mme_ue_s1ap_id());
        } else if (field == "enb_ue_s1ap_id" && first_message->enb_ue_s1ap_id() != 0) {
            value = std::to_string(first_message->enb_ue_s1ap_id());
        } else if (field == "imsi" && !first_message->imsi().empty()) {
            value = first_message->imsi();
        } else if (field == "tmsi" && !first_message->tmsi().empty()) {
            value = first_message->tmsi();
        } else if (field == "msg_type" && !first_message->msg_type().empty()) {
            value = first_message->msg_type();
        }
    } else if (source == "context") {
        // Extract from UE context
        auto context = correlator_->get_context(subscriber_key);
        if (context) {
            if (field == "source_ecgi" && !context->source_ecgi.empty()) {
                value = bytes_to_hex_string(context->source_ecgi);
            } else if (field == "ecgi" && !context->ecgi.empty()) {
                value = bytes_to_hex_string(context->ecgi);
            } else if (field == "target_ecgi" && !context->target_ecgi.empty()) {
                value = bytes_to_hex_string(context->target_ecgi);
            } else if (field == "imsi" && context->imsi.has_value()) {
                value = context->imsi.value();
            } else if (field == "tmsi" && context->tmsi.has_value()) {
                value = context->tmsi.value();
            }
        }
    }
    
    return value;
}

void RuleEngine::extract_event_data(Event& event,
                                   const std::string& expression,
                                   const CanonicalMessage& message,
                                   const CanonicalMessage* first_message) {
    std::string subscriber_key = event.subscriber_key();
    std::string value = extract_event_data_value(expression, message, first_message, subscriber_key);
    
    // Add to event attributes if value was extracted
    // Note: The target attribute name is set by the caller
    if (!value.empty()) {
        std::string field_name = expression.substr(expression.find('.') + 1);
        (*event.mutable_attributes())[field_name] = value;
    }
}

void RuleEngine::cleanup_expired_sequences() {
    auto now = std::chrono::system_clock::now();
    constexpr auto max_sequence_age = std::chrono::seconds(60); // 1 minute max
    
    auto it = sequence_states_.begin();
    while (it != sequence_states_.end()) {
        auto& sequences = it->second;
        sequences.erase(
            std::remove_if(sequences.begin(), sequences.end(),
                [&](const SequenceState& state) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - state.first_seen);
                    return elapsed > max_sequence_age;
                }),
            sequences.end());
        
        if (sequences.empty()) {
            it = sequence_states_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace rules
} // namespace s1see

