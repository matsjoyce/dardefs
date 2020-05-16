/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2020  <copyright holder> <email>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <map>
#include <vector>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

#include "types.hpp"


class Disk;
class Buffer;

const unsigned int NO_BLOCK_ASSIGNED = -1;
const unsigned int NO_CACHE_LOC_ASSIGNED = -1;

struct BlockMappingInfo {
    unsigned int physical_block_id = NO_BLOCK_ASSIGNED;
    unsigned int cache_location = NO_CACHE_LOC_ASSIGNED;
};

struct BlockCacheEntry {
    secure_string data;
    std::pair<bool, unsigned int> logical_block_id = {false, NO_BLOCK_ASSIGNED};
    bool dirty = false, dirtied_by_current = false;
    std::mutex lock;

    BlockCacheEntry();
};

class BlockAccessor {
    Buffer& buffer_;
    BlockCacheEntry& cache_entry;
    bool moved = false;

public:
    BlockAccessor(Buffer& buffer, BlockCacheEntry& cache_entry);
    BlockAccessor(BlockAccessor&& other);
    ~BlockAccessor();

    const secure_string& read() const;
    secure_string& writable();
    std::pair<bool, unsigned int> block_id() const;
    Buffer& buffer() const;
};

struct BufferOperationData {
    unsigned int max_blocks;
    char hidden = 2;
    std::set<unsigned int> blocks = {};
    unsigned int block_requests = 0, block_writes = 0, max_cache_takeup = 0;
};

class BufferOperation {
    Buffer& buffer;

public:
    BufferOperation(Buffer& buf);
    BufferOperation(BufferOperation&) = delete;
    BufferOperation(BufferOperation&&) = delete;
    ~BufferOperation();

    friend class Buffer;
};

class Buffer {
    Disk& disk;
    std::mutex lock;
    std::map<std::pair<bool, unsigned int>, BlockMappingInfo> block_mapping;
    unsigned int max_cover_id = 0, max_hidden_id = 0, number_of_mapping_blocks = 0;
    unsigned int cover_blocks_allocated = 0, hidden_blocks_allocated = 0, reserved_cache_space = 0;
    unsigned int cover_blocks_changed = 0, hidden_blocks_changed;
    std::vector<unsigned int> unallocated_list, virtual_list;

    std::vector<std::pair<bool, unsigned int>> reverse_block_mapping;
    std::vector<BlockCacheEntry> cache;
    std::deque<unsigned int> least_recently_used;
    std::map<std::thread::id, BufferOperationData> operation_for_thread;
    bool enforce_operations, debug, no_hidden;
    std::mutex wait_for_ops_done, wait_for_flush_done;
    bool thread_is_waiting_to_flush = false;
    unsigned int waiting_for_flush_to_finish = 0;

    void scanEntriesTable();
    void writeEntriesTable();
    unsigned int freeCacheEntry();
    void return_block(BlockCacheEntry& cache_entry);
    void end_operation();
    void op_requested(unsigned int block_id, bool hid);
    void op_released(unsigned int block_id, bool hid, bool dirty);
    void unlocked_flush();
    BufferOperationData& current_operation();

public:
    Buffer(Disk& disk, unsigned int cache_size, bool wipe_mapping_table, bool enforce_operations, bool debug, bool no_hidden);

    unsigned int totalBlocks();
    unsigned int blocksAllocated();
    unsigned int blocksForAspect(bool hidden);
    unsigned int blocksAllocatedForAspect(bool hidden);

    BlockAccessor block(unsigned int block_id, bool hidden);
    BlockAccessor allocateBlock(bool hidden);
    void deallocateBlock(unsigned int block_id, bool hidden);
    void flush();
    BufferOperation operation(unsigned int max_blocks);
    inline bool isDebugging() const { return debug; }
    inline bool hasHidden() const { return !no_hidden; }
    bool allowed(bool hidden, unsigned int allocated, unsigned int changed, unsigned int deallocated);

    friend class BlockAccessor;
    friend class BufferOperation;
};

#endif // BUFFER_HPP
