/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: spool.h
 * Description: Header for Spool class that manages message storage and retrieval.
 *              Provides append and read operations for SignalMessage records using
 *              WAL (Write-Ahead Log) for persistent storage with partition support.
 */

#pragma once

#include "s1see/spool/wal_log.h"
#include "signal_message.pb.h"
#include "spool_record.pb.h"
#include <memory>

namespace s1see {
namespace spool {

// Main spool interface
class Spool {
public:
    explicit Spool(const WALLog::Config& config);
    
    // Append a message, returns (partition, offset)
    std::pair<int32_t, int64_t> append(const SignalMessage& message);
    
    // Read records
    std::vector<SpoolRecord> read(int32_t partition, int64_t offset, int64_t max_records = 1000);
    
    // Consumer group management
    void commit_offset(const std::string& group, int32_t partition, int64_t offset);
    int64_t load_offset(const std::string& group, int32_t partition);
    
    // Maintenance
    void prune_old_segments();
    int64_t get_high_water_mark(int32_t partition);
    void flush();  // Flush all buffers to disk

private:
    std::unique_ptr<WALLog> wal_;
};

} // namespace spool
} // namespace s1see

