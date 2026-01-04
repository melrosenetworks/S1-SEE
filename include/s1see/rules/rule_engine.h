/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: rule_engine.h
 * Description: Header for RuleEngine class that processes canonical messages
 *              against rule sets. Evaluates single-message rules and sequence
 *              rules, extracts event data, and generates events when rules match.
 */

#pragma once

#include "canonical_message.pb.h"
#include "event.pb.h"
#include "s1see/correlate/correlator.h"
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

namespace s1see {
namespace rules {

// Event data extraction specification
// Supports expressions like:
//   "message.ecgi" - from current message
//   "first_message.ecgi" - from first message (sequence rules only)
//   "context.source_ecgi" - from UE context
//   "context.ecgi" - from UE context
struct EventDataExtraction {
    std::string target_attribute;  // Event attribute name (e.g., "target_cell_id")
    std::string source_expression; // Source expression (e.g., "message.ecgi")
};

// Rule types
struct SingleMessageRule {
    std::string event_name;
    std::string msg_type_pattern;  // e.g., "HandoverRequest"
    std::map<std::string, std::string> attributes; // Static attributes
    std::vector<EventDataExtraction> event_data; // Data extraction specifications
};

struct SequenceRule {
    std::string event_name;
    std::string first_msg_type;
    std::string second_msg_type;
    std::chrono::milliseconds time_window;
    std::map<std::string, std::string> attributes;
    std::vector<EventDataExtraction> event_data; // Data extraction specifications
};

struct Ruleset {
    std::string id;
    std::string version;
    std::vector<SingleMessageRule> single_message_rules;
    std::vector<SequenceRule> sequence_rules;
};

// Sequence state tracking
struct SequenceState {
    std::string subscriber_key;
    std::string first_msg_type;
    CanonicalMessage first_message;
    std::chrono::system_clock::time_point first_seen;
    std::string ruleset_id;
    std::string ruleset_version;
};

// Event Engine
class RuleEngine {
public:
    explicit RuleEngine(std::shared_ptr<correlate::Correlator> correlator);
    
    // Load ruleset from YAML
    void load_ruleset(const Ruleset& ruleset);
    
    // Process a canonical message and emit events
    std::vector<Event> process(const CanonicalMessage& message);
    
    // Cleanup expired sequence states
    void cleanup_expired_sequences();

private:
    std::shared_ptr<correlate::Correlator> correlator_;
    std::vector<Ruleset> rulesets_;
    
    // Sequence state: subscriber_key -> vector of active sequences
    std::unordered_map<std::string, std::vector<SequenceState>> sequence_states_;
    
    std::vector<Event> check_single_message_rules(const CanonicalMessage& message,
                                                   const Ruleset& ruleset,
                                                   const std::string& subscriber_key);
    std::vector<Event> check_sequence_rules(const CanonicalMessage& message,
                                           const Ruleset& ruleset,
                                           const std::string& subscriber_key);
    Event create_event(const std::string& name,
                      const CanonicalMessage& message,
                      const std::map<std::string, std::string>& attributes,
                      const std::string& ruleset_id,
                      const std::string& ruleset_version,
                      const std::string& subscriber_key);
    
    // Extract data value from expression
    std::string extract_event_data_value(const std::string& expression,
                                        const CanonicalMessage& message,
                                        const CanonicalMessage* first_message,
                                        const std::string& subscriber_key);
    
    // Extract data from expression and add to event attributes
    void extract_event_data(Event& event,
                           const std::string& expression,
                           const CanonicalMessage& message,
                           const CanonicalMessage* first_message = nullptr);
};

} // namespace rules
} // namespace s1see

