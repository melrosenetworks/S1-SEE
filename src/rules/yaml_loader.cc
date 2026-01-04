/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: yaml_loader.cc
 * Description: Implementation of YAML ruleset loader for loading rule configurations
 *              from YAML files. Parses single-message rules and sequence rules,
 *              extracts event names, message type patterns, attributes, and
 *              event data extraction specifications.
 */

#include "s1see/rules/yaml_loader.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <stdexcept>

namespace s1see {
namespace rules {

Ruleset load_ruleset_from_yaml(const std::string& file_path) {
    YAML::Node config = YAML::LoadFile(file_path);
    
    Ruleset ruleset;
    
    if (!config["ruleset"]) {
        throw std::runtime_error("Missing 'ruleset' key in YAML");
    }
    
    auto rs_node = config["ruleset"];
    ruleset.id = rs_node["id"].as<std::string>();
    ruleset.version = rs_node["version"].as<std::string>("1.0");
    
    // Load single message rules
    if (rs_node["single_message_rules"]) {
        for (const auto& rule_node : rs_node["single_message_rules"]) {
            SingleMessageRule rule;
            rule.event_name = rule_node["event_name"].as<std::string>();
            rule.msg_type_pattern = rule_node["msg_type"].as<std::string>();
            
            if (rule_node["attributes"]) {
                for (const auto& attr : rule_node["attributes"]) {
                    rule.attributes[attr.first.as<std::string>()] = 
                        attr.second.as<std::string>();
                }
            }
            
            // Load event data extraction specifications
            if (rule_node["event_data"]) {
                for (const auto& data_node : rule_node["event_data"]) {
                    EventDataExtraction extraction;
                    extraction.target_attribute = data_node["target"].as<std::string>();
                    extraction.source_expression = data_node["source"].as<std::string>();
                    rule.event_data.push_back(extraction);
                }
            }
            
            ruleset.single_message_rules.push_back(rule);
        }
    }
    
    // Load sequence rules
    if (rs_node["sequence_rules"]) {
        for (const auto& rule_node : rs_node["sequence_rules"]) {
            SequenceRule rule;
            rule.event_name = rule_node["event_name"].as<std::string>();
            rule.first_msg_type = rule_node["first_msg_type"].as<std::string>();
            rule.second_msg_type = rule_node["second_msg_type"].as<std::string>();
            
            int window_ms = rule_node["time_window_ms"].as<int>(15000);
            rule.time_window = std::chrono::milliseconds(window_ms);
            
            if (rule_node["attributes"]) {
                for (const auto& attr : rule_node["attributes"]) {
                    rule.attributes[attr.first.as<std::string>()] = 
                        attr.second.as<std::string>();
                }
            }
            
            // Load event data extraction specifications
            if (rule_node["event_data"]) {
                for (const auto& data_node : rule_node["event_data"]) {
                    EventDataExtraction extraction;
                    extraction.target_attribute = data_node["target"].as<std::string>();
                    extraction.source_expression = data_node["source"].as<std::string>();
                    rule.event_data.push_back(extraction);
                }
            }
            
            ruleset.sequence_rules.push_back(rule);
        }
    }
    
    return ruleset;
}

} // namespace rules
} // namespace s1see

