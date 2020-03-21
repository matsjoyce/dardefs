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

#include "types.hpp"


class Disk;

const unsigned int NO_BLOCK_ASSIGNED = -1;
const unsigned int NO_CACHE_LOC_ASSIGNED = -1;

class Buffer;

struct BlockMappingInfo {
    unsigned int physical_block_id = NO_BLOCK_ASSIGNED;
    unsigned int cache_location = NO_CACHE_LOC_ASSIGNED;
    unsigned int generation;
};

struct BlockCacheEntry {
    secure_string data;
    std::pair<bool, unsigned int> logical_block_id = {false, NO_BLOCK_ASSIGNED};
    bool dirty = false;
    std::mutex lock;

    BlockCacheEntry();
};

class BlockAccessor {
    Buffer& buffer;
    BlockCacheEntry& cache_entry;
    bool moved = false;

public:
    BlockAccessor(Buffer& buffer, BlockCacheEntry& cache_entry);
    BlockAccessor(BlockAccessor&& other);
    ~BlockAccessor();

    const secure_string& read() const;
    secure_string& writable();
    std::pair<bool, unsigned int> block_id() const;
};

class Buffer {
    Disk& disk;
    std::mutex lock;
    std::map<std::pair<bool, unsigned int>, BlockMappingInfo> block_mapping;
    unsigned int max_cover_id = 0, max_hidden_id = 0;
    std::vector<unsigned int> unallocated_list;
    std::vector<BlockCacheEntry> cache;
    std::deque<unsigned int> least_recently_used;

    void scanEntriesTable();
    unsigned int freeCacheEntry();
    void return_block(BlockCacheEntry& cache_entry);

public:
    Buffer(Disk& disk);

    unsigned int totalBlocks();
    unsigned int blocksAllocated();
    unsigned int blocksForAspect(bool hidden);
    unsigned int blocksAllocatedForAspect(bool hidden);

    BlockAccessor block(unsigned int block_id, bool hidden);
    BlockAccessor allocateBlock(bool hidden);
    void deallocateBlock(unsigned int block_id, bool hidden);
    void flush();

    friend class BlockAccessor;
};

#endif // BUFFER_HPP
