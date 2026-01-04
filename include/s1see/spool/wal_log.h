/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: wal_log.h
 * Description: Header for WAL (Write-Ahead Log) class that provides persistent
 *              message storage. Manages partitioned log files, provides append
 *              and read operations, and handles consumer offset tracking.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <fstream>
#include <chrono>
#include "spool_record.pb.h"

namespace s1see {
namespace spool {

struct SegmentInfo {
    int32_t partition;
    int64_t base_offset;
    std::string log_path;
    std::string idx_path;
    int64_t current_offset;
    int64_t file_size;
    
    // Cached file handles for performance
    std::unique_ptr<std::ofstream> log_file;
    std::unique_ptr<std::ofstream> idx_file;
    
    // Write buffering
    std::vector<char> log_buffer;
    std::vector<char> idx_buffer;
    static constexpr size_t BUFFER_SIZE = 64 * 1024; // 64KB buffer
    std::chrono::system_clock::time_point last_fsync;
    static constexpr auto FSYNC_INTERVAL = std::chrono::milliseconds(100); // fsync every 100ms
};

// Write-Ahead Log segmented storage
class WALLog {
public:
    struct Config {
        std::string base_dir = "spool_data";
        int32_t num_partitions = 1;
        int64_t max_segment_size = 100 * 1024 * 1024; // 100MB
        int64_t max_retention_bytes = 1024 * 1024 * 1024; // 1GB
        int64_t max_retention_seconds = 7 * 24 * 3600; // 7 days
        bool fsync_on_append = true;
        bool use_buffering = true; // Enable write buffering
        std::chrono::milliseconds fsync_interval = std::chrono::milliseconds(100);
    };

    explicit WALLog(const Config& config);
    ~WALLog();

    // Append a record, returns (partition, offset)
    std::pair<int32_t, int64_t> append(const SignalMessage& message);

    // Read records from a partition starting at offset
    std::vector<SpoolRecord> read(int32_t partition, int64_t offset, int64_t max_records = 1000);

    // Consumer group offset management
    void commit_offset(const std::string& group, int32_t partition, int64_t offset);
    int64_t load_offset(const std::string& group, int32_t partition);

    // Retention and maintenance
    void prune_old_segments();

    // Get current high water mark for a partition
    int64_t get_high_water_mark(int32_t partition);
    
    // Flush all active segments (public for Spool::flush)
    void flush_all_segments();

private:
    Config config_;
    std::mutex mutex_;
    
    // Active segments per partition
    std::unordered_map<int32_t, std::unique_ptr<SegmentInfo>> active_segments_;
    
    // Consumer group offsets: group -> (partition -> offset)
    std::unordered_map<std::string, std::unordered_map<int32_t, int64_t>> consumer_offsets_;
    
    // Cached directory listings for reads (partition -> segments)
    std::unordered_map<int32_t, std::vector<std::pair<int64_t, std::string>>> segment_cache_;
    std::chrono::system_clock::time_point cache_timestamp_;
    static constexpr auto CACHE_TTL = std::chrono::seconds(5);
    
    std::string offset_file_path(const std::string& group, int32_t partition);
    std::string segment_path(int32_t partition, int64_t base_offset, const std::string& suffix);
    SegmentInfo* get_or_create_segment(int32_t partition);
    void rotate_segment(int32_t partition);
    int32_t partition_for_message(const SignalMessage& message);
    void ensure_directory(const std::string& path);
    void load_consumer_offsets();
    void save_consumer_offset(const std::string& group, int32_t partition, int64_t offset);
    
    // Helper functions for buffered writes
    void open_segment_files(SegmentInfo* seg);
    void flush_segment_buffers(SegmentInfo* seg, bool force_fsync = false);
    void close_segment_files(SegmentInfo* seg);
    std::vector<std::pair<int64_t, std::string>> get_segments_for_partition(int32_t partition);
};

} // namespace spool
} // namespace s1see

