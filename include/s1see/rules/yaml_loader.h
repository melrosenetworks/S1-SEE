/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: yaml_loader.h
 * Description: Header for YAML ruleset loader functions. Provides interface for
 *              loading rule configurations from YAML files, parsing single-message
 *              rules and sequence rules with event data extraction specifications.
 */

#pragma once

#include "s1see/rules/rule_engine.h"
#include <string>

namespace s1see {
namespace rules {

// Load ruleset from YAML file
Ruleset load_ruleset_from_yaml(const std::string& file_path);

} // namespace rules
} // namespace s1see


