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

#include "cryptopp/osrng.h"

#include <iostream>

const unsigned int VIRTUAL_BLOCK = -2;

BlockCacheEntry::BlockCacheEntry() : data(LOGICAL_BLOCK_SIZE, '\xff') {
}

BlockAccessor::BlockAccessor(Buffer& buffer, BlockCacheEntry& cache_entry) : buffer_(buffer), cache_entry(cache_entry) {
    // Lock baton is passed from Buffer::block
}

BlockAccessor::BlockAccessor(BlockAccessor&& other) : buffer_(other.buffer_), cache_entry(other.cache_entry) {
    other.moved = true;
}

BlockAccessor::~BlockAccessor() {
    if (!moved) {
        buffer_.return_block(cache_entry);
    }
}

Buffer& BlockAccessor::buffer() const {
    return buffer_;
}


const secure_string& BlockAccessor::read() const {
    ensure(!moved, "BlockAccessor::read") << "Accessor has been moved";
    return cache_entry.data;
}

secure_string& BlockAccessor::writable() {
    ensure(!moved, "BlockAccessor::writable") << "Accessor has been moved";
    cache_entry.dirtied_by_current = true;
    return cache_entry.data;
}

std::pair<bool, unsigned int> BlockAccessor::block_id() const {
    ensure(!moved, "BlockAccessor::block_id") << "Accessor has been moved";
    return cache_entry.logical_block_id;
}

Buffer::Buffer(Disk& disk, unsigned int cache_size, bool wipe_mapping_table, bool enforce_operations, bool debug, bool no_hidden) :
        disk(disk), cache(cache_size), enforce_operations(enforce_operations), debug(debug), no_hidden(no_hidden) {
    wait_for_flush_done.lock();
    wait_for_ops_done.lock();

    for (auto i = 0u; i < cache.size(); ++i) {
        least_recently_used.push_back(i);
    }

    while (number_of_mapping_blocks * MAPPING_POINTERS_PER_BLOCK < disk.numberOfBlocks() - number_of_mapping_blocks * 2) {
        ++number_of_mapping_blocks;
    }

    if (wipe_mapping_table) {
        secure_string buf(LOGICAL_BLOCK_SIZE, '\xff');
        for (auto i = 0u; i < number_of_mapping_blocks; ++i) {
            disk.writeBlock(i, false, buf);
        }
        for (auto i = 0u; i < number_of_mapping_blocks; ++i) {
            disk.writeBlock(i + number_of_mapping_blocks, true, buf);
        }
    }

    scanEntriesTable();

    for (auto item : block_mapping) {
        if (item.first.first) {
            max_hidden_id = std::max(max_hidden_id, item.first.second + 1);
            ++hidden_blocks_allocated;
        }
        else {
            max_cover_id = std::max(max_cover_id, item.first.second + 1);
            ++cover_blocks_allocated;
        }
    }
    ensure(hidden_blocks_allocated + virtual_list.size() == cover_blocks_allocated, "Buffer::Buffer")
        << "Number of cover blocks (" << cover_blocks_allocated << ") does not equal number of hidden blocks (" << hidden_blocks_allocated + virtual_list.size() << ")";

    ensure(disk.numberOfBlocks() == block_mapping.size() + unallocated_list.size() + virtual_list.size() + number_of_mapping_blocks * 2, "Buffer::unlocked_flush")
        << "Numbers of types don't add up";
}

void Buffer::scanEntriesTable() {
    std::lock_guard<std::mutex> lg(lock);
    secure_string buf(LOGICAL_BLOCK_SIZE, '\0');
    reverse_block_mapping.resize(totalBlocks());

    // Cover mapping
    for (auto i = 0u; i < number_of_mapping_blocks; ++i) {
        disk.readBlock(i, false, buf);
        for (auto pos = 0u; pos < MAPPING_POINTERS_PER_BLOCK; ++pos) {
            auto phy_blk_id = i * MAPPING_POINTERS_PER_BLOCK + pos;
            auto log_blk_id = intFromBytes(&buf[BLOCK_POINTER_SIZE * pos]);
            if (log_blk_id != NO_BLOCK_ASSIGNED) {
                ensure(phy_blk_id < totalBlocks(), "Buffer::scanEntriesTable") << "Block mapping set for non-existant block";
                if (log_blk_id == VIRTUAL_BLOCK) {
                    reverse_block_mapping[phy_blk_id] = {true, VIRTUAL_BLOCK};
                }
                else {
                    block_mapping[{false, log_blk_id}].physical_block_id = phy_blk_id;
                    reverse_block_mapping[phy_blk_id] = {false, log_blk_id};
                }
            }
            else if (phy_blk_id < totalBlocks()) {
                unallocated_list.push_back(phy_blk_id);
                reverse_block_mapping[phy_blk_id] = {false, NO_BLOCK_ASSIGNED};
            }
        }
    }

    if (!no_hidden) {
        // Hidden mapping
        for (auto i = 0u; i < number_of_mapping_blocks; ++i) {
            disk.readBlock(number_of_mapping_blocks + i, true, buf);
            for (auto pos = 0u; pos < MAPPING_POINTERS_PER_BLOCK; ++pos) {
                auto phy_blk_id = i * MAPPING_POINTERS_PER_BLOCK + pos;
                auto log_blk_id = intFromBytes(&buf[BLOCK_POINTER_SIZE * pos]);
                if (log_blk_id != NO_BLOCK_ASSIGNED) {
                    ensure(phy_blk_id < totalBlocks(), "Buffer::scanEntriesTable") << "Block mapping set for non-existant block";
                    ensure(reverse_block_mapping[phy_blk_id] == std::make_pair(true, VIRTUAL_BLOCK), "Buffer::scanEntriesTable") << "Hidden block not shown in cover block table";
                    block_mapping[{true, log_blk_id}].physical_block_id = phy_blk_id;
                    reverse_block_mapping[phy_blk_id] = {true, log_blk_id};
                }
            }
        }
    }

    for (auto i = 0u; i < totalBlocks(); ++i) {
        if (reverse_block_mapping[i].second == VIRTUAL_BLOCK) {
            virtual_list.push_back(i);
        }
    }
}

unsigned int Buffer::totalBlocks() {
    return disk.numberOfBlocks() - number_of_mapping_blocks * 2;
}

unsigned int Buffer::blocksAllocated() {
    return block_mapping.size();
}

unsigned int Buffer::blocksForAspect(bool /*hidden*/) {
    return totalBlocks() / 2;
}

unsigned int Buffer::blocksAllocatedForAspect(bool hidden) {
    return hidden ? hidden_blocks_allocated : cover_blocks_allocated;
}

BlockAccessor Buffer::block(unsigned int block_id, bool hidden) {
    if (enforce_operations) {
        std::lock_guard<std::mutex> lg(lock);
        op_requested(block_id, hidden);
    }

    unsigned int cache_location;
    auto start_time = std::chrono::high_resolution_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> lg(lock);

            auto iter = block_mapping.find({hidden, block_id});
            ensure(iter != block_mapping.end(), "Buffer::block") << "Block " << block_id << "/" << hidden << " does not exist";
            auto& block_info = iter->second;
            if (block_info.cache_location != NO_CACHE_LOC_ASSIGNED) {
                auto& cache_entry = cache[block_info.cache_location];
                if (cache_entry.logical_block_id != std::make_pair(hidden, block_id)) {
                    block_info.cache_location = NO_CACHE_LOC_ASSIGNED;
                }
            }
            if (block_info.cache_location == NO_CACHE_LOC_ASSIGNED) {
                block_info.cache_location = freeCacheEntry();
                auto& cache_entry = cache[block_info.cache_location];
                cache_entry.logical_block_id = {hidden, block_id};
                cache_entry.dirty = false;
                if (block_info.physical_block_id != NO_BLOCK_ASSIGNED) {
                    disk.readBlock(block_info.physical_block_id + number_of_mapping_blocks * 2, hidden, cache_entry.data);
                }
                else {
                    cache_entry.dirtied_by_current = true;
                }
            }
            cache_location = block_info.cache_location;
        }

        auto& cache_entry = cache[cache_location];
        cache_entry.lock.lock();
        {
            std::lock_guard<std::mutex> lg(lock);
            if (cache_entry.logical_block_id == std::make_pair(hidden, block_id)) {
                auto iter = std::find(least_recently_used.begin(), least_recently_used.end(), cache_location);
                if (iter != least_recently_used.end()) {
                    least_recently_used.erase(iter);
                }
                return {*this, cache[cache_location]};
            }
            cache_entry.lock.unlock();
            auto cur_time = std::chrono::high_resolution_clock::now();
            using namespace std::chrono_literals;
            ensure(cur_time - start_time < 10s, "Buffer::block") << "Been waiting for block for > 10s";
        }
    }
}

void Buffer::return_block(BlockCacheEntry& cache_entry) {
    std::unique_lock<std::mutex> lg(lock);

    auto [hidden, block_id] = cache_entry.logical_block_id;
    auto iter = block_mapping.find(cache_entry.logical_block_id);
    ensure(iter != block_mapping.end(), "Buffer::return_block") << "Block " << block_id << "/" << hidden << " does not exist";
    auto& block_info = iter->second;
    ensure(block_info.cache_location != NO_CACHE_LOC_ASSIGNED, "Buffer::return_block") << "No cache location for block being returned";
    ensure(&cache_entry == &cache[block_info.cache_location], "Buffer::return_block") << "Cache location of returned block is different";
    if (!cache_entry.dirty && !cache_entry.dirtied_by_current) {
        ensure(std::find(least_recently_used.begin(), least_recently_used.end(), block_info.cache_location) == least_recently_used.end(), "Buffer::return_block")
            << "Returned cache entry " << block_info.cache_location << " is already in the LRU cache!";
        least_recently_used.push_back(block_info.cache_location);
    }
    else if (block_info.physical_block_id != NO_BLOCK_ASSIGNED) {
        unallocated_list.push_back(block_info.physical_block_id);
        reverse_block_mapping[block_info.physical_block_id] = {false, NO_BLOCK_ASSIGNED};
        block_info.physical_block_id = NO_BLOCK_ASSIGNED;
    }
    if (enforce_operations) {
        op_released(block_id, hidden, cache_entry.dirtied_by_current);
    }
    if (cache_entry.dirtied_by_current && !cache_entry.dirty) {
        hidden ? ++hidden_blocks_changed : ++cover_blocks_changed;
        ensure(hidden_blocks_changed <= cover_blocks_changed, "Buffer::return_block") << "Too many hidden blocks changed";
    }
    cache_entry.dirty = cache_entry.dirty || cache_entry.dirtied_by_current;
    cache_entry.dirtied_by_current = false;
    cache_entry.lock.unlock();
}

unsigned int Buffer::freeCacheEntry() {
    if (least_recently_used.size()) {
        auto blk_id = least_recently_used.front();
        least_recently_used.pop_front();
        ensure(!enforce_operations || reserved_cache_space >= cache.size() - least_recently_used.size(), "Buffer::freeCacheEntry")
            << "Too much cache space used";
        return blk_id;
    }
    throw std::runtime_error("Cache full");
}

BlockAccessor Buffer::allocateBlock(bool hidden) {
    unsigned int block_id;
    {
        std::lock_guard<std::mutex> lg(lock);

        ensure(cover_blocks_allocated + hidden_blocks_allocated < totalBlocks(), "Buffer::allocateBlock") << "FS is full";
        ensure(hidden_blocks_allocated <= cover_blocks_allocated, "Buffer::allocateBlock") << "Too many hidden blocks allocated";
        block_id = hidden ? max_hidden_id++ : max_cover_id++;
        block_mapping[{hidden, block_id}] = {};
        if (isDebugging()) {
            std::cout << "Allocated " << block_id << "/" << hidden << std::endl;
        }
        hidden ? ++hidden_blocks_allocated : ++cover_blocks_allocated;
    }
    auto acc = block(block_id, hidden);
    acc.writable();
    return acc;
}

void Buffer::deallocateBlock(unsigned int block_id, bool hidden) {
    std::lock_guard<std::mutex> lg(lock);

    auto iter = block_mapping.find({hidden, block_id});
    ensure(iter != block_mapping.end(), "Buffer::deallocateBlock") << "Block " << block_id << "/" << hidden << " does not exist";
    auto& block_info = iter->second;

    if (block_info.cache_location != NO_CACHE_LOC_ASSIGNED) {
        auto& cache_entry = cache[block_info.cache_location];
        if (cache_entry.logical_block_id == std::make_pair(hidden, block_id)) {
            std::lock_guard<std::mutex> lg2(cache_entry.lock);
            ensure(cache_entry.logical_block_id == std::make_pair(hidden, block_id), "Buffer::deallocateBlock") << "Cache entry is not correct";
            cache_entry.logical_block_id = {false, NO_BLOCK_ASSIGNED};
            if (cache_entry.dirty) {
                least_recently_used.push_back(block_info.cache_location);
                hidden ? --hidden_blocks_changed : --cover_blocks_changed;
                ensure(hidden_blocks_changed <= cover_blocks_changed, "Buffer::deallocateBlock") << "Too many hidden blocks changed";
            }
            cache_entry.dirty = false;
        }
        block_info.cache_location = NO_CACHE_LOC_ASSIGNED;
        if (block_info.physical_block_id != NO_BLOCK_ASSIGNED) {
            unallocated_list.push_back(block_info.physical_block_id);
            reverse_block_mapping[block_info.physical_block_id] = {false, NO_BLOCK_ASSIGNED};
        }
    }
    block_mapping.erase({hidden, block_id});
    hidden ? --hidden_blocks_allocated : --cover_blocks_allocated;
    ensure(hidden_blocks_allocated <= cover_blocks_allocated, "Buffer::deallocateBlock") << "Too many hidden blocks deallocated";
    if (isDebugging()) {
        std::cout << "Deallocated " << block_id << "/" << hidden << std::endl;
    }
}

void Buffer::flush() {
    std::lock_guard<std::mutex> lg(lock);
    unlocked_flush();
}

void Buffer::unlocked_flush() {
    std::vector<std::pair<char, unsigned int>> to_flush;
    CryptoPP::AutoSeededRandomPool rng;
    unsigned int num_cover = 0, num_hidden = 0;

    for (auto i = 0u; i < cache.size(); ++i) {
        auto& cache_entry = cache[i];
        if (cache_entry.logical_block_id.second != NO_BLOCK_ASSIGNED && cache_entry.dirty) {
            cache_entry.logical_block_id.first ? ++num_hidden : ++num_cover;
            to_flush.push_back({'C', i});
        }
    }

    ensure(hidden_blocks_changed + cover_blocks_changed == to_flush.size(), "Buffer::unlocked_flush")
        << "Changed stats do not match";

    ensure(hidden_blocks_allocated + cover_blocks_allocated == block_mapping.size(), "Buffer::unlocked_flush")
        << "Changed stats do not match";

    ensure(disk.numberOfBlocks() == block_mapping.size() - to_flush.size() + unallocated_list.size() + virtual_list.size() + number_of_mapping_blocks * 2, "Buffer::unlocked_flush")
        << "Flush sizes don't add up: "
        << disk.numberOfBlocks() << " total blocks, "
        << block_mapping.size() << " allocated blocks, "
        << to_flush.size() << " changed blocks, "
        << unallocated_list.size() << " unallocated blocks, "
        << virtual_list.size() << " virtual blocks and "
        << number_of_mapping_blocks * 2 << " mapping blocks";

    while (hidden_blocks_allocated + virtual_list.size() < cover_blocks_allocated) {
        virtual_list.push_back(NO_BLOCK_ASSIGNED);
        to_flush.push_back({'V', virtual_list.size() - 1});
        ++num_hidden;
    }

    while (hidden_blocks_allocated + virtual_list.size() > cover_blocks_allocated) {
        auto idx = CryptoPP::Integer(rng, 0, virtual_list.size() - 1).ConvertToLong();
        auto phy_blk_id = virtual_list[idx];
        if (reverse_block_mapping[phy_blk_id].second != NO_BLOCK_ASSIGNED) {
            unallocated_list.push_back(phy_blk_id);
            reverse_block_mapping[phy_blk_id] = {false, NO_BLOCK_ASSIGNED};
        }
        virtual_list.erase(virtual_list.begin() + idx);
    }

    auto virtual_idx = 0u;
    while (num_hidden < num_cover) {
        if (virtual_list[virtual_idx] == NO_BLOCK_ASSIGNED) {
            break;
        }
        unallocated_list.push_back(virtual_list[virtual_idx]);
        reverse_block_mapping[virtual_list[virtual_idx]] = {false, NO_BLOCK_ASSIGNED};
        virtual_list[virtual_idx] = NO_BLOCK_ASSIGNED;
        to_flush.push_back({'V', virtual_idx});
        ++num_hidden;
        ++virtual_idx;
    }

    for (auto bm_iter = block_mapping.begin(); num_hidden < num_cover && bm_iter != block_mapping.end(); ++bm_iter) {
        if (bm_iter->first.first && bm_iter->second.physical_block_id != NO_BLOCK_ASSIGNED) {
            unallocated_list.push_back(bm_iter->second.physical_block_id);
            reverse_block_mapping[bm_iter->second.physical_block_id] = {false, NO_BLOCK_ASSIGNED};
            to_flush.push_back({'H', bm_iter->first.second});
            ++num_hidden;
        }
    }
    ensure(num_hidden == num_cover, "Buffer::unlocked_flush") << "Could not generate enough changed";

    secure_string buf(LOGICAL_BLOCK_SIZE, '\0');
    while (to_flush.size()) {
        auto idx = CryptoPP::Integer(rng, 0, to_flush.size() - 1).ConvertToLong();
        unsigned int rand = CryptoPP::Integer(rng, 0, unallocated_list.size() - 1).ConvertToLong();
        auto [mode, cache_idx] = to_flush[idx];
        auto phy_block_id = unallocated_list[rand];

        if (mode == 'C') {
            auto& cache_entry = cache[cache_idx];
            auto& block_info = block_mapping[cache_entry.logical_block_id];

            ensure(block_info.cache_location == cache_idx, "Buffer::unlocked_flush") << "Block info cache location is wrong";
            disk.writeBlock(phy_block_id + number_of_mapping_blocks * 2, cache_entry.logical_block_id.first, cache_entry.data);
            reverse_block_mapping[phy_block_id] = cache_entry.logical_block_id;
            cache_entry.dirty = false;
            block_info.physical_block_id = phy_block_id;

            ensure(std::find(least_recently_used.begin(), least_recently_used.end(), cache_idx) == least_recently_used.end(), "Buffer::unlocked_flush")
                << "Returned cache entry " << block_info.cache_location << " is already in the LRU cache!";
            least_recently_used.push_back(cache_idx);
        }
        else if (mode == 'V') {
            rng.GenerateBlock(&buf[0], LOGICAL_BLOCK_SIZE);
            disk.writeBlock(phy_block_id + number_of_mapping_blocks * 2, true, buf);
            reverse_block_mapping[phy_block_id] = {true, VIRTUAL_BLOCK};
            virtual_list[cache_idx] = phy_block_id;
        }
        else if (mode == 'H') {
            auto location = std::make_pair(true, cache_idx);
            auto& block_info = block_mapping[location];
            disk.readBlock(block_info.physical_block_id + number_of_mapping_blocks * 2, true, buf);
            disk.writeBlock(phy_block_id + number_of_mapping_blocks * 2, true, buf);
            reverse_block_mapping[phy_block_id] = {true, cache_idx};
            block_info.physical_block_id = phy_block_id;
        }

        to_flush.erase(to_flush.begin() + idx);
        unallocated_list.erase(unallocated_list.begin() + rand);
    }

    writeEntriesTable();

    cover_blocks_changed = hidden_blocks_changed = 0;
}

void Buffer::writeEntriesTable() {
    secure_string buf(LOGICAL_BLOCK_SIZE, '\0');

    // Cover mapping
    for (auto i = 0u; i < number_of_mapping_blocks; ++i) {
        buf.replace(0, LOGICAL_BLOCK_SIZE, LOGICAL_BLOCK_SIZE, '\xff');
        for (auto pos = 0u; pos < MAPPING_POINTERS_PER_BLOCK; ++pos) {
            auto phy_blk_id = i * MAPPING_POINTERS_PER_BLOCK + pos;
            if (phy_blk_id >= totalBlocks()) {
                break;
            }
            if (reverse_block_mapping[phy_blk_id].first) {
                intToBytes(&buf[BLOCK_POINTER_SIZE * pos], VIRTUAL_BLOCK);
            }
            else {
                intToBytes(&buf[BLOCK_POINTER_SIZE * pos], reverse_block_mapping[phy_blk_id].second);
            }
        }
        disk.writeBlock(i, false, buf);
    }

    // Hidden mapping
    for (auto i = 0u; i < number_of_mapping_blocks; ++i) {
        buf.replace(0, LOGICAL_BLOCK_SIZE, LOGICAL_BLOCK_SIZE, '\xff');
        for (auto pos = 0u; pos < MAPPING_POINTERS_PER_BLOCK; ++pos) {
            auto phy_blk_id = i * MAPPING_POINTERS_PER_BLOCK + pos;
            if (phy_blk_id >= totalBlocks()) {
                break;
            }
            if (reverse_block_mapping[phy_blk_id].first && reverse_block_mapping[phy_blk_id].second != VIRTUAL_BLOCK) {
                intToBytes(&buf[BLOCK_POINTER_SIZE * pos], reverse_block_mapping[phy_blk_id].second);
            }
        }
        disk.writeBlock(number_of_mapping_blocks + i, true, buf);
    }
}

BufferOperation Buffer::operation(unsigned int max_blocks) {
    auto id = std::this_thread::get_id();

    lock.lock();
    while (true) {
        if (reserved_cache_space + max_blocks <= cache.size() && !thread_is_waiting_to_flush) {
            reserved_cache_space += max_blocks;
            operation_for_thread[id] = {max_blocks};
            if (isDebugging()) {
                std::cout << "OPERATION begin by " << id << " requesting " << max_blocks << " blocks of cache space; " << operation_for_thread.size() << " operations ongoing" << std::endl;
            }
            lock.unlock();
            return {*this};
        }
        else if (thread_is_waiting_to_flush) {
            // wait for flush to end
            ++waiting_for_flush_to_finish;
            lock.unlock();
            wait_for_flush_done.lock();
            // Baton passing style: mutex passed to us.
            --waiting_for_flush_to_finish;
            if (waiting_for_flush_to_finish) {
                wait_for_flush_done.unlock();
            }
        }
        else {
            thread_is_waiting_to_flush = true;
            if (operation_for_thread.size()) {
                lock.unlock();
                wait_for_ops_done.lock();
                // Baton passing style: mutex passed to us.
            }
            unlocked_flush();
            thread_is_waiting_to_flush = false;
            reserved_cache_space = 0;
            if (waiting_for_flush_to_finish) {
                wait_for_flush_done.unlock();
            }
        }
    }
}

void Buffer::end_operation() {
    std::lock_guard<std::mutex> lg(lock);
    auto id = std::this_thread::get_id();
    auto& data = operation_for_thread[id];
    reserved_cache_space -= data.max_blocks - data.blocks.size();
    if (isDebugging()) {
        std::cout << "OPERATION ended by " << id << "; max usage " << data.max_cache_takeup << " (max predicted " << data.max_blocks << "), "
            << data.block_requests << " requests, "
            << data.block_writes << " writes." << std::endl;

        std::cout << "Current state: "
            << "Hidden Alloc = " << hidden_blocks_allocated << ", Cover Alloc = " << cover_blocks_allocated
            << ", Hidden Changed = " << hidden_blocks_changed << ", Cover Changed = " << cover_blocks_changed << std::endl;
    }
    operation_for_thread.erase(id);
    if (thread_is_waiting_to_flush && !operation_for_thread.size()) {
        wait_for_ops_done.unlock();
    }
}

BufferOperationData& Buffer::current_operation() {
    auto id = std::this_thread::get_id();
    auto iter = operation_for_thread.find(id);
    ensure(iter != operation_for_thread.end(), "Buffer::current_operation") << "No operation for current thread " << id;
    return iter->second;
}

BufferOperation::BufferOperation(Buffer& buf) :
        buffer(buf) {
}

BufferOperation::~BufferOperation() {
    buffer.end_operation();
}

void Buffer::op_requested(unsigned int block_id, bool hid) {
    auto& data = current_operation();
    if (data.hidden != 2) {
        ensure(data.hidden == hid, "BufferOperation::requested") << "Requested block is in a different aspect from the previous one";
    }
    data.hidden = hid;
    data.blocks.insert(block_id);
    ++data.block_requests;
    data.max_cache_takeup = std::max(data.max_cache_takeup, static_cast<unsigned int>(data.blocks.size()));
    ensure(data.blocks.size() <= data.max_blocks, "BufferOperation::requested") << "Too many blocks requested";
}

void Buffer::op_released(unsigned int block_id, bool hid, bool dirty) {
    auto& data = current_operation();
    ensure(data.hidden == hid, "BufferOperation::released") << "Released block wasn't requested (hidden deviation)";
    ensure(data.blocks.count(block_id), "BufferOperation::released") << "Released block wasn't requested (block_id deviation)";
    if (!dirty) {
        data.blocks.erase(block_id);
    }
    else {
        ++data.block_writes;
    }
}

bool Buffer::allowed(bool hidden, unsigned int allocated, unsigned int changed, unsigned int deallocated) {
    long after_cover_blocks_allocated = cover_blocks_allocated,
         after_hidden_blocks_allocated = hidden_blocks_allocated,
         after_cover_blocks_changed = cover_blocks_changed,
         after_hidden_blocks_changed = hidden_blocks_changed;

    if (hidden) {
        after_hidden_blocks_allocated += allocated - static_cast<long>(deallocated);
        after_hidden_blocks_changed += allocated + changed - static_cast<long>(deallocated);
    }
    else {
        after_cover_blocks_allocated += allocated - static_cast<long>(deallocated);
        after_cover_blocks_changed += allocated + changed - static_cast<long>(deallocated);
    }
    after_cover_blocks_allocated = std::max(after_cover_blocks_allocated, 0l);
    after_hidden_blocks_allocated = std::max(after_hidden_blocks_allocated, 0l);
    after_cover_blocks_changed = std::max(after_cover_blocks_changed, 0l);
    after_hidden_blocks_changed = std::max(after_hidden_blocks_changed, 0l);

    if (isDebugging()) {
        std::cout << "Evaluating operation: "
            << "Hidden Alloc = " << after_hidden_blocks_allocated << ", Cover Alloc = " << after_cover_blocks_allocated
            << ", Hidden Changed = " << after_hidden_blocks_changed << ", Cover Changed = " << after_cover_blocks_changed << std::endl;
    }

    return after_hidden_blocks_allocated <= after_cover_blocks_allocated && after_hidden_blocks_changed <= after_cover_blocks_changed;
}
