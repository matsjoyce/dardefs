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

#include "buffer.hpp"
#include "disk.hpp"
#include "utilities.hpp"
#include "consts.hpp"

#include <iostream>

BlockCacheEntry::BlockCacheEntry() : data(LOGICAL_BLOCK_SIZE, '\xff') {
}

BlockAccessor::BlockAccessor(Buffer& buffer, BlockCacheEntry& cache_entry) : buffer(buffer), cache_entry(cache_entry) {
    // Lock baton is passed from Buffer::block
}

BlockAccessor::BlockAccessor(BlockAccessor&& other) : buffer(other.buffer), cache_entry(other.cache_entry) {
    other.moved = true;
}

BlockAccessor::~BlockAccessor() {
    if (!moved) {
        buffer.return_block(cache_entry);
    }
}


const secure_string& BlockAccessor::read() const {
    ensure(!moved, "BlockAccessor::read") << "Accessor has been moved";
    return cache_entry.data;
}

secure_string& BlockAccessor::writable() {
    ensure(!moved, "BlockAccessor::writable") << "Accessor has been moved";
    cache_entry.dirty = true;
    return cache_entry.data;
}

std::pair<bool, unsigned int> BlockAccessor::block_id() const {
    ensure(!moved, "BlockAccessor::block_id") << "Accessor has been moved";
    return cache_entry.logical_block_id;
}

Buffer::Buffer(Disk& disk) : disk(disk), cache(1024) {
    for (auto i = 0u; i < cache.size(); ++i) {
        least_recently_used.push_back(i);
    }
    scanEntriesTable();
}

void Buffer::scanEntriesTable() {
    std::lock_guard<std::mutex> lg(lock);

    for (unsigned int block_num = 0; block_num < disk.numberOfBlocks(); ++block_num) {
        BlockMappingType type;
        unsigned int block_id, generation;
        std::tie(type, block_id, generation) = disk.readBlockMapping(block_num);
//         std::cout << block_num << " " << static_cast<int>(type) << " " << block_id << " " << generation << std::endl;

        switch (type) {
            case BlockMappingType::COVER: case BlockMappingType::HIDDEN: {
                auto pair = std::make_pair(type == BlockMappingType::HIDDEN, block_id);
                auto iter = block_mapping.find(pair);
                if (iter == block_mapping.end()) {
                    block_mapping[pair].physical_block_id = block_num;
                    block_mapping[pair].generation = generation;
                }
                else if (iter->second.generation < generation) {
                    unallocated_list.push_back(block_mapping[pair].physical_block_id);
                    block_mapping[pair].physical_block_id = block_num;
                    block_mapping[pair].generation = generation;
                }
                else {
                    unallocated_list.push_back(block_num);
                }
                break;
            }
            case BlockMappingType::NEITHER: {
                unallocated_list.push_back(block_num);
                break;
            }
        }
    }
    for (auto item : block_mapping) {
        if (item.first.first) {
            max_hidden_id = std::max(max_hidden_id, item.first.second + 1);
        }
        else {
            max_cover_id = std::max(max_cover_id, item.first.second + 1);
        }
    }
}

unsigned int Buffer::totalBlocks() {
    return disk.numberOfBlocks();
}

unsigned int Buffer::blocksAllocated() {
    return block_mapping.size();
}

unsigned int Buffer::blocksForAspect(bool /*hidden*/) {
    return disk.numberOfBlocks() / 2;
}

unsigned int Buffer::blocksAllocatedForAspect(bool hidden) {
    return 0;
}

BlockAccessor Buffer::block(unsigned int block_id, bool hidden) {
    unsigned int cache_location;
    auto start_time = std::chrono::high_resolution_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> lg(lock);

            auto iter = block_mapping.find({hidden, block_id});
            ensure(iter != block_mapping.end(), "Buffer::block") << "Block " << block_id << "/" << hidden << " does not exist";
            auto& block_info = iter->second;
            if (block_info.cache_location == NO_CACHE_LOC_ASSIGNED) {
                block_info.cache_location = freeCacheEntry();
                auto& cache_entry = cache[block_info.cache_location];
                cache_entry.logical_block_id = {hidden, block_id};
                cache_entry.dirty = false;
                if (block_info.physical_block_id != NO_BLOCK_ASSIGNED) {
                    disk.readBlock(block_info.physical_block_id, hidden, cache_entry.data);
                }
                else {
                    cache_entry.dirty = true;
                }
            }
            cache_location = block_info.cache_location;
        }

        auto& cache_entry = cache[cache_location];
        cache_entry.lock.lock();
        if (cache_entry.logical_block_id == std::make_pair(hidden, block_id)) {
            return {*this, cache[cache_location]};
        }
        cache_entry.lock.unlock();
        auto cur_time = std::chrono::high_resolution_clock::now();
        using namespace std::chrono_literals;
        ensure(cur_time - start_time < 10s, "Buffer::block") << "Been waiting for block for > 10s";
    }
}

void Buffer::return_block(BlockCacheEntry& cache_entry) {
    std::lock_guard<std::mutex> lg(lock);

    cache_entry.lock.unlock();

    auto [block_id, hidden] = cache_entry.logical_block_id;
    auto iter = block_mapping.find(cache_entry.logical_block_id);
    ensure(iter != block_mapping.end(), "Buffer::block") << "Block " << block_id << "/" << hidden << " does not exist";
    auto& block_info = iter->second;
    ensure(block_info.cache_location != NO_CACHE_LOC_ASSIGNED, "Buffer::block") << "No cache location for block being returned";
    ensure(&cache_entry == &cache[block_info.cache_location], "Buffer::block") << "Cache location of returned block is different";
    if (!cache_entry.dirty) {
        least_recently_used.push_back(block_info.cache_location);
    }
}


unsigned int Buffer::freeCacheEntry() {
    if (least_recently_used.size()) {
        auto blk_id = least_recently_used.front();
        least_recently_used.pop_front();
        return blk_id;
    }
    throw std::runtime_error("Cache full");
}

BlockAccessor Buffer::allocateBlock(bool hidden) {
    unsigned int block_id = hidden ? max_hidden_id++ : max_cover_id++;
    block_mapping[{hidden, block_id}] = {};
    std::cout << "Allocated " << block_id << "/" << hidden << std::endl;
    return block(block_id, hidden);
}

void Buffer::deallocateBlock(unsigned int block_id, bool hidden) {
    std::lock_guard<std::mutex> lg(lock);

    auto iter = block_mapping.find({hidden, block_id});
    ensure(iter != block_mapping.end(), "Buffer::block") << "Block " << block_id << "/" << hidden << " does not exist";
    auto& block_info = iter->second;

    if (block_info.cache_location != NO_CACHE_LOC_ASSIGNED) {
        auto& cache_entry = cache[block_info.cache_location];
        std::lock_guard<std::mutex> lg2(cache_entry.lock);
        cache_entry.logical_block_id = {false, NO_BLOCK_ASSIGNED};
        cache_entry.dirty = false;
        block_info.cache_location = NO_CACHE_LOC_ASSIGNED;
    }
    block_mapping.erase({hidden, block_id});
    std::cout << "Deallocated " << block_id << "/" << hidden << std::endl;
}

void Buffer::flush() {
    for (auto& cache_entry : cache) {
        if (cache_entry.logical_block_id.second != NO_BLOCK_ASSIGNED && cache_entry.dirty) {
            auto location = cache_entry.logical_block_id.first * 10 + cache_entry.logical_block_id.second;
            disk.writeBlock(location, cache_entry.logical_block_id.first, cache_entry.data);
            auto& block_info = block_mapping[cache_entry.logical_block_id];
            disk.writeBlockMapping(location, cache_entry.logical_block_id.first ? BlockMappingType::HIDDEN : BlockMappingType::COVER,
                                   cache_entry.logical_block_id.second, ++block_info.generation);
            cache_entry.dirty = false;
            block_info.physical_block_id = location;
        }
    }
}
