/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: s1see_spoolerd.cc
 * Description: Spooler daemon application for receiving messages via gRPC and
 *              storing them in spool partitions. Acts as a message ingestion service
 *              that accepts SignalMessage records from external sources and persists
 *              them to WAL (Write-Ahead Log) storage for later processing.
 */

#include "s1see/ingest/grpc_adapter.h"
#include "s1see/spool/spool.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>

std::unique_ptr<s1see::ingest::GrpcIngestAdapter> g_adapter;

void signal_handler(int sig) {
    if (g_adapter) {
        g_adapter->stop();
    }
    exit(0);
}

int main(int argc, char** argv) {
    std::string listen_address = "0.0.0.0:50051";
    std::string spool_dir = "spool_data";
    
    if (argc > 1) {
        listen_address = argv[1];
    }
    if (argc > 2) {
        spool_dir = argv[2];
    }
    
    std::cout << "S1-SEE Spooler Daemon" << std::endl;
    std::cout << "Listening on: " << listen_address << std::endl;
    std::cout << "Spool directory: " << spool_dir << std::endl;
    
    // Setup spool
    s1see::spool::WALLog::Config spool_config;
    spool_config.base_dir = spool_dir;
    spool_config.num_partitions = 1;
    spool_config.fsync_on_append = true;
    auto spool = std::make_shared<s1see::spool::Spool>(spool_config);
    
    // Setup gRPC adapter
    g_adapter = std::make_unique<s1see::ingest::GrpcIngestAdapter>(listen_address);
    g_adapter->set_spool(spool);
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Start adapter
    if (!g_adapter->start()) {
        std::cerr << "Failed to start gRPC adapter" << std::endl;
        return 1;
    }
    
    std::cout << "Spooler daemon running. Press Ctrl+C to stop." << std::endl;
    
    // Wait
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}

