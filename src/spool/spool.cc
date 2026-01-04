/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: spool.cc
 * Description: Implementation of Spool class for managing message storage and retrieval.
 *              Provides append and read operations for SignalMessage records using
 *              WAL (Write-Ahead Log) for persistent storage with partition support.
 */

#include "s1see/spool/spool.h"

namespace s1see {
namespace spool {

Spool::Spool(const WALLog::Config& config) {
    wal_ = std::make_unique<WALLog>(config);
}

std::pair<int32_t, int64_t> Spool::append(const SignalMessage& message) {
    return wal_->append(message);
}

std::vector<SpoolRecord> Spool::read(int32_t partition, int64_t offset, int64_t max_records) {
    return wal_->read(partition, offset, max_records);
}

void Spool::commit_offset(const std::string& group, int32_t partition, int64_t offset) {
    wal_->commit_offset(group, partition, offset);
}

int64_t Spool::load_offset(const std::string& group, int32_t partition) {
    return wal_->load_offset(group, partition);
}

void Spool::prune_old_segments() {
    wal_->prune_old_segments();
}

int64_t Spool::get_high_water_mark(int32_t partition) {
    return wal_->get_high_water_mark(partition);
}

void Spool::flush() {
    wal_->flush_all_segments();
}

} // namespace spool
} // namespace s1see

