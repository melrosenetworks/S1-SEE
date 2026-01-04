/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: s1see_processor.cc
 * Description: Main application for processing S1AP messages from spool storage.
 *              Reads messages from spool partitions, processes them through the
 *              pipeline (decode, correlate, rule evaluation), and emits events to
 *              configured sinks (stdout, JSONL file). Supports continuous and
 *              batch processing modes.
 */

#include "s1see/processor/pipeline.h"
#include "s1see/rules/yaml_loader.h"
#include "s1see/sinks/stdout_sink.h"
#include "s1see/sinks/jsonl_sink.h"
#include "event.pb.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>

using s1see::Event;

std::unique_ptr<s1see::processor::Pipeline> g_pipeline;
bool g_running = true;

void signal_handler(int sig) {
    g_running = false;
}

int main(int argc, char** argv) {
    std::string spool_dir = "spool_data";
    std::string ruleset_file = "config/rulesets/mobility.yaml";
    std::string output_file = "events.jsonl";
    bool continuous = true;
    
    if (argc > 1) {
        spool_dir = argv[1];
    }
    if (argc > 2) {
        ruleset_file = argv[2];
    }
    if (argc > 3) {
        output_file = argv[3];
    }
    if (argc > 4) {
        continuous = (std::string(argv[4]) == "true" || std::string(argv[4]) == "1");
    }
    
    std::cout << "S1-SEE Processor" << std::endl;
    std::cout << "Spool directory: " << spool_dir << std::endl;
    std::cout << "Ruleset: " << ruleset_file << std::endl;
    std::cout << "Output: " << output_file << std::endl;
    
    // Setup pipeline
    s1see::processor::Pipeline::Config config;
    config.spool_base_dir = spool_dir;
    config.spool_partitions = 1;
    config.consumer_group = "processor";
    g_pipeline = std::make_unique<s1see::processor::Pipeline>(config);
    
    // Load ruleset
    try {
        auto ruleset = s1see::rules::load_ruleset_from_yaml(ruleset_file);
        g_pipeline->load_ruleset(ruleset);
        std::cout << "Loaded ruleset: " << ruleset.id << " v" << ruleset.version << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load ruleset: " << e.what() << std::endl;
        return 1;
    }
    
    // Setup sinks
    auto stdout_sink = std::make_shared<s1see::sinks::StdoutSink>();
    auto jsonl_sink = std::make_shared<s1see::sinks::JSONLSink>(output_file);
    g_pipeline->add_sink(stdout_sink);
    g_pipeline->add_sink(jsonl_sink);
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Processor running. Processing messages..." << std::endl;
    
    if (continuous) {
        // Run continuously
        while (g_running) {
            int events = g_pipeline->process_batch();
            if (events > 0) {
                std::cout << "Emitted " << events << " events" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        // Process one batch
        int events = g_pipeline->process_batch();
        std::cout << "Emitted " << events << " events" << std::endl;
    }
    
    // Flush sinks
    stdout_sink->flush();
    jsonl_sink->flush();
    jsonl_sink->close();
    
    // Dump UE records on exit
    std::cout << "\nDumping UE records..." << std::endl;
    g_pipeline->dump_ue_records(std::cout);
    
    return 0;
}

