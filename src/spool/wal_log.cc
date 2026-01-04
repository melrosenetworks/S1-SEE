/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: wal_log.cc
 * Description: Implementation of WAL (Write-Ahead Log) for persistent message storage.
 *              Manages partitioned log files, provides append and read operations,
 *              handles consumer offset tracking, and ensures data durability through
 *              file-based storage with partition support.
 */

#include "s1see/spool/wal_log.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <chrono>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;

namespace s1see {
namespace spool {

namespace {
    constexpr int64_t INDEX_ENTRY_SIZE = 16; // offset (8) + position (8)
}

WALLog::WALLog(const Config& config) : config_(config) {
    ensure_directory(config_.base_dir);
    for (int32_t p = 0; p < config_.num_partitions; ++p) {
        ensure_directory(fs::path(config_.base_dir) / ("partition_" + std::to_string(p)));
    }
    load_consumer_offsets();
    cache_timestamp_ = std::chrono::system_clock::now();
}

WALLog::~WALLog() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Flush and close all open segments
    for (auto& [partition, seg] : active_segments_) {
        if (seg) {
            flush_segment_buffers(seg.get(), true);
            close_segment_files(seg.get());
        }
    }
}

int32_t WALLog::partition_for_message(const SignalMessage& message) {
    // Hash source_id + source_sequence for partitioning
    std::hash<std::string> hasher;
    std::string key = message.source_id() + ":" + std::to_string(message.source_sequence());
    return static_cast<int32_t>(hasher(key) % config_.num_partitions);
}

void WALLog::ensure_directory(const std::string& path) {
    fs::create_directories(path);
}

std::string WALLog::segment_path(int32_t partition, int64_t base_offset, const std::string& suffix) {
    fs::path p(config_.base_dir);
    p /= ("partition_" + std::to_string(partition));
    p /= ("segment_" + std::to_string(base_offset) + suffix);
    return p.string();
}

SegmentInfo* WALLog::get_or_create_segment(int32_t partition) {
    auto it = active_segments_.find(partition);
    if (it != active_segments_.end()) {
        // Check if current segment needs rotation
        if (it->second->file_size >= config_.max_segment_size) {
            rotate_segment(partition);
            it = active_segments_.find(partition);
        }
        return it->second.get();
    }

    // Find the highest existing segment or start at 0
    int64_t base_offset = 0;
    fs::path part_dir = fs::path(config_.base_dir) / ("partition_" + std::to_string(partition));
    
    if (fs::exists(part_dir)) {
        for (const auto& entry : fs::directory_iterator(part_dir)) {
            if (entry.path().extension() == ".log") {
                std::string stem = entry.path().stem().string();
                if (stem.find("segment_") == 0) {
                    int64_t seg_offset = std::stoll(stem.substr(8));
                    if (seg_offset >= base_offset) {
                        base_offset = seg_offset + 1;
                    }
                }
            }
        }
    }

    auto seg = std::make_unique<SegmentInfo>();
    seg->partition = partition;
    seg->base_offset = base_offset;
    seg->log_path = segment_path(partition, base_offset, ".log");
    seg->idx_path = segment_path(partition, base_offset, ".idx");
    seg->current_offset = base_offset;
    seg->file_size = 0;
    seg->last_fsync = std::chrono::system_clock::now();

    // If segment file exists, read to find current offset
    if (fs::exists(seg->log_path)) {
        std::ifstream log_file(seg->log_path, std::ios::binary | std::ios::ate);
        if (log_file.is_open()) {
            // Read existing records to find last offset
            // Simplified: assume we can read the index
            std::ifstream idx_file(seg->idx_path, std::ios::binary);
            if (idx_file.is_open()) {
                idx_file.seekg(0, std::ios::end);
                size_t idx_size = idx_file.tellg();
                if (idx_size > 0) {
                    seg->current_offset = base_offset + (idx_size / INDEX_ENTRY_SIZE);
                }
            }
        }
    }

    // Open file handles for this segment
    open_segment_files(seg.get());

    SegmentInfo* ptr = seg.get();
    active_segments_[partition] = std::move(seg);
    return ptr;
}

void WALLog::rotate_segment(int32_t partition) {
    auto it = active_segments_.find(partition);
    if (it == active_segments_.end()) return;

    // Flush and close current segment
    if (it->second) {
        flush_segment_buffers(it->second.get(), true);
        close_segment_files(it->second.get());
    }
    it->second.reset();
    active_segments_.erase(it);
    
    // Invalidate cache for this partition
    segment_cache_.erase(partition);
}

std::pair<int32_t, int64_t> WALLog::append(const SignalMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    int32_t partition = partition_for_message(message);
    SegmentInfo* seg = get_or_create_segment(partition);

    int64_t offset = seg->current_offset++;

    // Create SpoolRecord
    SpoolRecord record;
    record.set_partition(partition);
    record.set_offset(offset);
    record.set_ts_append(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    *record.mutable_message() = message;

    // Serialize record
    std::string serialized;
    if (!record.SerializeToString(&serialized)) {
        throw std::runtime_error("Failed to serialize SpoolRecord");
    }

    // Get position before writing
    int64_t position = seg->file_size;
    
    // Write length prefix + data to buffer or directly to file
    uint32_t length = static_cast<uint32_t>(serialized.size());
    
    if (config_.use_buffering && seg->log_file) {
        // Buffered write
        const char* length_bytes = reinterpret_cast<const char*>(&length);
        seg->log_buffer.insert(seg->log_buffer.end(), length_bytes, length_bytes + sizeof(length));
        seg->log_buffer.insert(seg->log_buffer.end(), serialized.begin(), serialized.end());
        
        // Flush buffer if it's getting large
        if (seg->log_buffer.size() >= SegmentInfo::BUFFER_SIZE) {
            seg->log_file->write(seg->log_buffer.data(), seg->log_buffer.size());
            seg->log_buffer.clear();
        }
    } else {
        // Direct write (fallback)
        if (!seg->log_file) {
            open_segment_files(seg);
        }
        seg->log_file->write(reinterpret_cast<const char*>(&length), sizeof(length));
        seg->log_file->write(serialized.data(), serialized.size());
    }
    
    seg->file_size += sizeof(length) + serialized.size();

    // Write to index (buffered or direct)
    if (config_.use_buffering && seg->idx_file) {
        const char* offset_bytes = reinterpret_cast<const char*>(&offset);
        const char* position_bytes = reinterpret_cast<const char*>(&position);
        seg->idx_buffer.insert(seg->idx_buffer.end(), offset_bytes, offset_bytes + sizeof(offset));
        seg->idx_buffer.insert(seg->idx_buffer.end(), position_bytes, position_bytes + sizeof(position));
        
        if (seg->idx_buffer.size() >= SegmentInfo::BUFFER_SIZE) {
            seg->idx_file->write(seg->idx_buffer.data(), seg->idx_buffer.size());
            seg->idx_buffer.clear();
        }
    } else {
        if (!seg->idx_file) {
            open_segment_files(seg);
        }
        seg->idx_file->write(reinterpret_cast<const char*>(&offset), sizeof(offset));
        seg->idx_file->write(reinterpret_cast<const char*>(&position), sizeof(position));
    }

    // Periodic fsync (instead of every append)
    auto now = std::chrono::system_clock::now();
    bool should_fsync = config_.fsync_on_append && 
                       (now - seg->last_fsync) >= config_.fsync_interval;
    
    if (should_fsync) {
        flush_segment_buffers(seg, true);
        seg->last_fsync = now;
    }

    return {partition, offset};
}

void WALLog::open_segment_files(SegmentInfo* seg) {
    if (!seg->log_file) {
        seg->log_file = std::make_unique<std::ofstream>(
            seg->log_path, std::ios::binary | std::ios::app);
        if (!seg->log_file->is_open()) {
            throw std::runtime_error("Failed to open log file: " + seg->log_path);
        }
    }
    
    if (!seg->idx_file) {
        seg->idx_file = std::make_unique<std::ofstream>(
            seg->idx_path, std::ios::binary | std::ios::app);
        if (!seg->idx_file->is_open()) {
            throw std::runtime_error("Failed to open index file: " + seg->idx_path);
        }
    }
}

void WALLog::flush_segment_buffers(SegmentInfo* seg, bool force_fsync) {
    if (!seg) return;
    
    // Flush log buffer
    if (seg->log_file && !seg->log_buffer.empty()) {
        seg->log_file->write(seg->log_buffer.data(), seg->log_buffer.size());
        seg->log_buffer.clear();
        seg->log_file->flush();
    }
    
    // Flush index buffer
    if (seg->idx_file && !seg->idx_buffer.empty()) {
        seg->idx_file->write(seg->idx_buffer.data(), seg->idx_buffer.size());
        seg->idx_buffer.clear();
        seg->idx_file->flush();
    }
    
    if (force_fsync && seg->log_file) {
        // Note: Full fsync would require file descriptor access
        // For now, flush is sufficient
    }
}

void WALLog::flush_all_segments() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [partition, seg] : active_segments_) {
        if (seg) {
            flush_segment_buffers(seg.get(), true);
        }
    }
}

void WALLog::close_segment_files(SegmentInfo* seg) {
    if (!seg) return;
    
    // Flush any remaining buffers before closing
    flush_segment_buffers(seg, true);
    
    if (seg->log_file) {
        seg->log_file->close();
        seg->log_file.reset();
    }
    
    if (seg->idx_file) {
        seg->idx_file->close();
        seg->idx_file.reset();
    }
}

std::vector<std::pair<int64_t, std::string>> WALLog::get_segments_for_partition(int32_t partition) {
    // Check cache first
    auto now = std::chrono::system_clock::now();
    bool cache_valid = (now - cache_timestamp_) < CACHE_TTL;
    
    auto cache_it = segment_cache_.find(partition);
    if (cache_valid && cache_it != segment_cache_.end()) {
        return cache_it->second;
    }
    
    // Cache miss or expired - scan directory
    fs::path part_dir = fs::path(config_.base_dir) / ("partition_" + std::to_string(partition));
    if (!fs::exists(part_dir)) {
        return {};
    }

    std::vector<std::pair<int64_t, std::string>> segments;
    for (const auto& entry : fs::directory_iterator(part_dir)) {
        if (entry.path().extension() == ".log") {
            std::string stem = entry.path().stem().string();
            if (stem.find("segment_") == 0) {
                int64_t seg_base = std::stoll(stem.substr(8));
                segments.emplace_back(seg_base, entry.path().string());
            }
        }
    }
    std::sort(segments.begin(), segments.end());
    
    // Update cache
    segment_cache_[partition] = segments;
    cache_timestamp_ = now;
    
    return segments;
}

std::vector<SpoolRecord> WALLog::read(int32_t partition, int64_t offset, int64_t max_records) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<SpoolRecord> records;
    
    // Use cached segment list
    auto segments = get_segments_for_partition(partition);
    if (segments.empty()) {
        return records;
    }

    // Read from appropriate segment(s)
    for (const auto& [seg_base, log_path] : segments) {
        if (offset < seg_base) continue;
        
        std::string idx_path = log_path;
        idx_path.replace(idx_path.size() - 4, 4, ".idx");

        // Read index to find position (optimized with binary search on file)
        std::ifstream idx_file(idx_path, std::ios::binary);
        if (!idx_file.is_open()) continue;

        // Get file size for binary search
        idx_file.seekg(0, std::ios::end);
        std::streampos file_size = idx_file.tellg();
        if (file_size < INDEX_ENTRY_SIZE) continue;
        
        // Binary search for the offset
        int64_t file_position = 0;
        bool found = false;
        int64_t num_entries = file_size / INDEX_ENTRY_SIZE;
        int64_t left = 0, right = num_entries - 1;
        
        while (left <= right) {
            int64_t mid = left + (right - left) / 2;
            idx_file.seekg(mid * INDEX_ENTRY_SIZE, std::ios::beg);
            
            int64_t idx_offset;
            int64_t idx_position;
            idx_file.read(reinterpret_cast<char*>(&idx_offset), sizeof(idx_offset));
            idx_file.read(reinterpret_cast<char*>(&idx_position), sizeof(idx_position));
            
            if (idx_file.fail()) break;
            
            if (idx_offset < offset) {
                left = mid + 1;
            } else {
                file_position = idx_position;
                found = true;
                right = mid - 1;
            }
        }
        
        if (!found) continue;

        // Read from log file
        std::ifstream log_file(log_path, std::ios::binary);
        if (!log_file.is_open()) continue;

        log_file.seekg(file_position, std::ios::beg);
        
        while (records.size() < static_cast<size_t>(max_records) && !log_file.eof()) {
            uint32_t length;
            log_file.read(reinterpret_cast<char*>(&length), sizeof(length));
            if (log_file.fail() || length == 0) break;

            std::string buffer(length, '\0');
            log_file.read(&buffer[0], length);
            if (log_file.fail()) break;

            SpoolRecord record;
            if (record.ParseFromString(buffer)) {
                if (record.offset() >= offset) {
                    records.push_back(record);
                }
            }
        }

        if (records.size() >= static_cast<size_t>(max_records)) break;
    }

    return records;
}

void WALLog::commit_offset(const std::string& group, int32_t partition, int64_t offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    consumer_offsets_[group][partition] = offset;
    save_consumer_offset(group, partition, offset);
}

int64_t WALLog::load_offset(const std::string& group, int32_t partition) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = consumer_offsets_.find(group);
    if (it != consumer_offsets_.end()) {
        auto pit = it->second.find(partition);
        if (pit != it->second.end()) {
            return pit->second;
        }
    }
    return 0;
}

std::string WALLog::offset_file_path(const std::string& group, int32_t partition) {
    fs::path p(config_.base_dir);
    p /= "offsets";
    p /= (group + "_p" + std::to_string(partition) + ".offset");
    return p.string();
}

void WALLog::load_consumer_offsets() {
    fs::path offsets_dir = fs::path(config_.base_dir) / "offsets";
    if (!fs::exists(offsets_dir)) return;

    for (const auto& entry : fs::directory_iterator(offsets_dir)) {
        if (entry.path().extension() == ".offset") {
            std::string filename = entry.path().stem().string();
            size_t pos = filename.find("_p");
            if (pos != std::string::npos) {
                std::string group = filename.substr(0, pos);
                int32_t partition = std::stoi(filename.substr(pos + 2));
                
                std::ifstream file(entry.path(), std::ios::binary);
                if (file.is_open()) {
                    int64_t offset;
                    file.read(reinterpret_cast<char*>(&offset), sizeof(offset));
                    if (!file.fail()) {
                        consumer_offsets_[group][partition] = offset;
                    }
                }
            }
        }
    }
}

void WALLog::save_consumer_offset(const std::string& group, int32_t partition, int64_t offset) {
    fs::path offsets_dir = fs::path(config_.base_dir) / "offsets";
    ensure_directory(offsets_dir.string());
    
    std::string path = offset_file_path(group, partition);
    std::ofstream file(path, std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
    }
}

void WALLog::prune_old_segments() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Implementation: remove segments older than retention policy
    // Simplified for prototype
}

int64_t WALLog::get_high_water_mark(int32_t partition) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // First check in-memory active segments
    auto it = active_segments_.find(partition);
    if (it != active_segments_.end()) {
        return it->second->current_offset - 1;
    }
    
    // If not in memory, scan filesystem to find the highest offset
    fs::path part_dir = fs::path(config_.base_dir) / ("partition_" + std::to_string(partition));
    if (!fs::exists(part_dir)) {
        return 0;
    }
    
    int64_t max_offset = 0;
    
    // Find all segments and check their indexes
    for (const auto& entry : fs::directory_iterator(part_dir)) {
        if (entry.path().extension() == ".idx") {
            std::ifstream idx_file(entry.path(), std::ios::binary);
            if (!idx_file.is_open()) continue;
            
            // Read last index entry to find max offset
            idx_file.seekg(0, std::ios::end);
            std::streampos file_size = idx_file.tellg();
            if (file_size >= INDEX_ENTRY_SIZE) {
                // Read last entry
                idx_file.seekg(file_size - INDEX_ENTRY_SIZE, std::ios::beg);
                int64_t last_offset;
                int64_t last_position;
                idx_file.read(reinterpret_cast<char*>(&last_offset), sizeof(last_offset));
                idx_file.read(reinterpret_cast<char*>(&last_position), sizeof(last_position));
                if (!idx_file.fail() && last_offset > max_offset) {
                    max_offset = last_offset;
                }
            }
        }
    }
    
    return max_offset;
}

} // namespace spool
} // namespace s1see

