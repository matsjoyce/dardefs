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

#include <cstring>
#include <iostream>

#include "dir.hpp"
#include "consts.hpp"
#include "utilities.hpp"

const unsigned int NO_BLOCK = -1;
const unsigned int BTREE_HEADER_OFFSET = 9;
const unsigned int BTREE_HEADER_SIZE = LOGICAL_BLOCK_SIZE - BTREE_HEADER_OFFSET;
const unsigned int BTREE_NODE_OFFSET = 0;
const unsigned int BTREE_NODE_SIZE = LOGICAL_BLOCK_SIZE;


Dir::Dir(Buffer& buf, unsigned int block_id, bool hidden) :
        Dir(buf, buf.block(block_id, hidden)) {
}

Dir::Dir(Buffer& buf, BlockAccessor&& header_acc) :
        buffer(buf), header_acc(std::move(header_acc)) {

    ensure(this->header_acc.read()[0] == DIR_TYPE, "Dir::Dir") << "This block is not a file header";
}

Dir::Dir(Dir&& other) :
        Dir(other.buffer, std::move(other.header_acc)) {
}

std::pair<bool, unsigned int> Dir::block_id() const {
    return header_acc.block_id();
}

unsigned int Dir::size() const {
    return (intFromBytes(&header_acc.read()[1]) + 1) * LOGICAL_BLOCK_SIZE;
}

unsigned int Dir::blocks() const {
    return intFromBytes(&header_acc.read()[1]);
}

unsigned int Dir::height() const {
    return intFromBytes(&header_acc.read()[5]);
}

constexpr unsigned int record_size(bool is_leaf) {
    return is_leaf ? BTREE_RECORD_SIZE : BTREE_RECORD_SIZE + 4;
}

constexpr unsigned int num_keys(unsigned int size, bool is_leaf) {
    return is_leaf ? size / record_size(is_leaf) : (size - 4) / record_size(is_leaf);
}

constexpr unsigned int fname_location(unsigned int offset, unsigned int idx, bool is_leaf) {
    return is_leaf ? offset + record_size(is_leaf) * idx : offset + 4 + record_size(is_leaf) * idx;
}

bool node_full(const BlockAccessor& acc, unsigned int offset, unsigned int size, bool is_leaf) {
    return intFromBytes(&acc.read()[fname_location(offset, num_keys(size, is_leaf) - 1, is_leaf) + FILE_NAME_SIZE]) != NO_BLOCK;
}

secure_string fname_from_bytes(const secure_string& bytes, unsigned int pos) {
    auto full = bytes.substr(pos, FILE_NAME_SIZE);
    auto zero_pos = full.find('\0');
    if (zero_pos != secure_string::npos) {
        full.erase(zero_pos);
    }
    return full;
}

unsigned int node_used(const BlockAccessor& acc, unsigned int offset, unsigned int size, bool is_leaf) {
    for (unsigned int i = 0; i < num_keys(size, is_leaf); ++i) {
        auto pos = fname_location(offset, i, is_leaf);
        if (intFromBytes(&acc.read()[pos + FILE_NAME_SIZE]) == NO_BLOCK) {
            return i;
        }
    }
    return num_keys(size, is_leaf);
}

unsigned int find_key_pos(const secure_string& fname, const BlockAccessor& acc, unsigned int offset, unsigned int size, bool is_leaf) {
    for (unsigned int i = 0; i < num_keys(size, is_leaf); ++i) {
        auto pos = fname_location(offset, i, is_leaf);
        if (intFromBytes(&acc.read()[pos + FILE_NAME_SIZE]) == NO_BLOCK) {
            return NO_BLOCK;
        }
        if (memcmp(fname.c_str(), &acc.read()[pos], std::min(fname.size() + 1, static_cast<unsigned long>(FILE_NAME_SIZE))) <= 0) {
            return i;
        }
    }
    return NO_BLOCK;
}

bool fname_equals(const secure_string& fname, const BlockAccessor& acc, unsigned int byte_pos) {
    if (intFromBytes(&acc.read()[byte_pos + FILE_NAME_SIZE]) == NO_BLOCK) {
        return false;
    }
    return memcmp(fname.c_str(), &acc.read()[byte_pos], std::min(fname.size() + 1, static_cast<unsigned long>(FILE_NAME_SIZE))) == 0;
}

unsigned int find_key_pos_or_end(const secure_string& fname, const BlockAccessor& acc, unsigned int offset, unsigned int size, bool is_leaf) {
    auto pos = find_key_pos(fname, acc, offset, size, is_leaf);
    return pos == NO_BLOCK ? node_used(acc, offset, size, is_leaf) : pos;
}

void clean_node_block(BlockAccessor& acc, unsigned int offset, unsigned int size, bool is_leaf) {
    // Note that this function DOES NOT clean the first child pointer for inner nodes
    for (unsigned int i = 0; i < num_keys(size, is_leaf); ++i) {
        auto pos = fname_location(offset, i, is_leaf);
        intToBytes(&acc.writable()[pos + FILE_NAME_SIZE], NO_BLOCK);
    }
}

void insert_to_node(const secure_string& fname, unsigned int value, unsigned int child, unsigned int idx,
                    BlockAccessor& acc, unsigned int offset, unsigned int size, bool is_leaf) {
    auto no_keys = num_keys(size, is_leaf);
    ensure(!node_full(acc, offset, size, is_leaf), "insert_to_node")
        << "No space in this node!";

    auto bytes_pos = fname_location(offset, idx, is_leaf);

    memmove(&acc.writable()[bytes_pos + record_size(is_leaf)],
            &acc.writable()[bytes_pos],
            record_size(is_leaf) * (no_keys - idx - 1));

    acc.writable().replace(bytes_pos, FILE_NAME_SIZE, FILE_NAME_SIZE, '\0');
    acc.writable().replace(bytes_pos, std::min(fname.size(), static_cast<unsigned long>(FILE_NAME_SIZE)), fname);
    intToBytes(&acc.writable()[bytes_pos + FILE_NAME_SIZE], value);
    if (!is_leaf) {
        intToBytes(&acc.writable()[bytes_pos + FILE_NAME_SIZE + 4], child);
    }
}

void remove_from_node(unsigned int idx, BlockAccessor& acc, unsigned int offset, unsigned int size, bool is_leaf) {
    auto no_keys = num_keys(size, is_leaf);
    auto bytes_pos = fname_location(offset, idx, is_leaf);
    ensure(intFromBytes(&acc.read()[bytes_pos + FILE_NAME_SIZE]) != NO_BLOCK, "remove_from_node") << "Nothing to remove!";

    memmove(&acc.writable()[bytes_pos],
            &acc.writable()[bytes_pos + record_size(is_leaf)],
            record_size(is_leaf) * (no_keys - idx - 1));

    auto end_pos = fname_location(offset, no_keys - 1, is_leaf);
    intToBytes(&acc.writable()[end_pos + FILE_NAME_SIZE], NO_BLOCK);
}

void Dir::split_root() {
    auto new_block_acc = buffer.allocateBlock(block_id().first);
    clean_node_block(new_block_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, height() == 0);
    memcpy(&new_block_acc.writable()[0], &header_acc.read()[BTREE_HEADER_OFFSET], BTREE_HEADER_SIZE);
    clean_node_block(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false);
    intToBytes(&header_acc.writable()[BTREE_HEADER_OFFSET], new_block_acc.block_id().second);
    intToBytes(&header_acc.writable()[1], blocks() + 1);
    intToBytes(&header_acc.writable()[5], height() + 1);
}

void Dir::unsplit_root() {
    ensure(height() > 0, "Dir::unsplit_root") << "Can't unsplit tree of height 0";
    ensure(node_used(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false) == 0, "Dir::unsplit_root")
        << "Can't unsplit root when it has " << node_used(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false) + 1 << " children";

    auto old_block_id = intFromBytes(&header_acc.read()[BTREE_HEADER_OFFSET]);
    {
        auto old_block_acc = buffer.block(old_block_id, block_id().first);
        ensure(node_used(old_block_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, height() == 1) <= num_keys(BTREE_HEADER_SIZE, height() == 1), "Dir::unsplit_root")
            << "Can't unsplit root when it's only child has " << node_used(old_block_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, height() == 1) + 1 << " children";
        memcpy(&header_acc.writable()[BTREE_HEADER_OFFSET], &old_block_acc.read()[BTREE_NODE_OFFSET], BTREE_HEADER_SIZE);
    }
    buffer.deallocateBlock(old_block_id, block_id().first);
    intToBytes(&header_acc.writable()[1], blocks() - 1);
    intToBytes(&header_acc.writable()[5], height() - 1);
}

std::tuple<unsigned int, secure_string, unsigned int> Dir::split_node(unsigned int blck_id, unsigned int height) {
    auto acc = buffer.block(blck_id, block_id().first);
    auto new_acc = buffer.allocateBlock(block_id().first);
    auto is_leaf = height == 0;
    auto size = num_keys(BTREE_NODE_SIZE, is_leaf);
    auto middle_idx = size / 2;
    auto byte_middle_pos = fname_location(BTREE_NODE_OFFSET, middle_idx, is_leaf);
    clean_node_block(new_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, is_leaf);
    memcpy(&new_acc.writable()[BTREE_NODE_OFFSET], &acc.read()[byte_middle_pos + FILE_NAME_SIZE + 4], 4 + record_size(is_leaf) * (size - middle_idx - 1));
    auto middle_fname = fname_from_bytes(acc.read(), byte_middle_pos);
    auto middle_value = intFromBytes(&acc.read()[byte_middle_pos] + FILE_NAME_SIZE);
    clean_node_block(acc, byte_middle_pos - (4 * !is_leaf), BTREE_NODE_SIZE - byte_middle_pos + BTREE_NODE_OFFSET, is_leaf);
    intToBytes(&header_acc.writable()[1], blocks() + 1);
    return {new_acc.block_id().second, middle_fname, middle_value};
}

bool Dir::node_add(const secure_string& fname, unsigned int value, unsigned int blck_id, unsigned int height) {
    auto acc = buffer.block(blck_id, block_id().first);
    if (height == 0) {
        // Leaf
        if (node_full(acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, true)) {
            return false;
        }

        auto pos = find_key_pos_or_end(fname, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, true);
        ensure(!fname_equals(fname, acc, fname_location(BTREE_NODE_OFFSET, pos, true)), "Dir::add")
            << "Duplicate entry " << fname;
        insert_to_node(fname, value, 0, pos, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, true);
        return true;
    }

    // Inner
    auto pos = find_key_pos_or_end(fname, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false);
    auto v = intFromBytes(&acc.read()[fname_location(BTREE_NODE_OFFSET, pos, false) - 4]);
    if (node_add(fname, value, v, height - 1)) {
        return true;
    }

    if (node_full(acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false)) {
        return false;
    }
    // Couldn't insert, split the block
    auto [spl_blkid, spl_fname, spl_value] = split_node(v, height - 1);
    pos = find_key_pos_or_end(spl_fname, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false);
    insert_to_node(spl_fname, spl_value, spl_blkid, pos, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false);

    pos = find_key_pos_or_end(fname, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false);
    v = intFromBytes(&acc.read()[fname_location(BTREE_NODE_OFFSET, pos, false) - 4]);
    auto complete = node_add(fname, value, v, height - 1);
    ensure(complete, "Dir::add") << "Insert not complete after block split!";
    return true;
}

void Dir::add(const secure_string& fname, unsigned int value) {
    if (height() == 0) {
        // Leaf
        if (!node_full(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true)) {
            auto pos = find_key_pos_or_end(fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true);
            ensure(!fname_equals(fname, header_acc, fname_location(BTREE_HEADER_OFFSET, pos, true)), "Dir::add")
                << "Duplicate entry " << fname;
            insert_to_node(fname, value, 0, pos, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true);
            return;
        }
        split_root();
    }

    // Inner
    auto pos = find_key_pos_or_end(fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false);
    auto v = intFromBytes(&header_acc.read()[fname_location(BTREE_HEADER_OFFSET, pos, false) - 4]);
    if (node_add(fname, value, v, height() - 1)) {
        return;
    }
    if (node_full(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false)) {
        // Couldn't insert, split the root
        split_root();
        auto pos = find_key_pos_or_end(fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false);
        auto v = intFromBytes(&header_acc.read()[fname_location(BTREE_HEADER_OFFSET, pos, false) - 4]);
        if (node_add(fname, value, v, height() - 1)) {
            return;
        }
    }
    // Couldn't insert, split the block
    auto [spl_blkid, spl_fname, spl_value] = split_node(v, height() - 1);
    pos = find_key_pos_or_end(spl_fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false);
    insert_to_node(spl_fname, spl_value, spl_blkid, pos, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false);

    pos = find_key_pos_or_end(fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false);
    v = intFromBytes(&header_acc.read()[fname_location(BTREE_HEADER_OFFSET, pos, false) - 4]);
    auto complete = node_add(fname, value, v, height() - 1);
    ensure(complete, "Dir::add") << "Insert not complete after block split!";
}

void Dir::refill_node(BlockAccessor& top_acc, unsigned int uf_pos, unsigned int uf_byte_pos, unsigned int height, unsigned int top_offset, unsigned int top_size) {
    if (top_acc.buffer().isDebugging()) {
        std::cout << "Refilling node " << std::endl;
    }
    auto uf_blk_id = intFromBytes(&top_acc.read()[uf_byte_pos - 4]);
    {
        auto uf_acc = buffer.block(uf_blk_id, block_id().first);
        auto top_used = node_used(top_acc, top_offset, top_size, false);
        auto child_is_leaf = !(height - 1);
        auto uf_used = node_used(uf_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, child_is_leaf);
        auto half_child_num_keys = num_keys(BTREE_NODE_SIZE, child_is_leaf) / 2;

        if (uf_pos) {
            auto left_byte_pos = uf_byte_pos - record_size(false);
            auto left_acc = buffer.block(intFromBytes(&top_acc.read()[left_byte_pos - 4]), block_id().first);
            auto left_used = node_used(left_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, child_is_leaf);
            if (left_used - 1 > half_child_num_keys) {
                if (top_acc.buffer().isDebugging()) {
                    std::cout << "Rotate from left" << std::endl;
                }
                // Rotate from left
                // Move UF entries down one
                memmove(
                    &uf_acc.writable()[BTREE_NODE_OFFSET + record_size(child_is_leaf)],
                    &uf_acc.read()[BTREE_NODE_OFFSET],
                    record_size(child_is_leaf) * uf_used + 4 * !child_is_leaf
                );
                // Copy top record to UF
                memcpy(
                    &uf_acc.writable()[fname_location(BTREE_NODE_OFFSET, 0, child_is_leaf)],
                    &top_acc.read()[uf_byte_pos - record_size(false)],
                    BTREE_RECORD_SIZE
                );
                // Copy left record to top
                memcpy(
                    &top_acc.writable()[uf_byte_pos - record_size(false)],
                    &left_acc.read()[fname_location(BTREE_NODE_OFFSET, left_used - 1, child_is_leaf)],
                    BTREE_RECORD_SIZE
                );
                // Copy left pointer to UF
                if (!child_is_leaf) {
                    memcpy(
                        &uf_acc.writable()[BTREE_NODE_OFFSET],
                        &left_acc.read()[fname_location(BTREE_NODE_OFFSET, left_used - 1, child_is_leaf) + FILE_NAME_SIZE + 4],
                        4
                    );
                }
                // Clear left
                intToBytes(&left_acc.writable()[fname_location(BTREE_NODE_OFFSET, left_used - 1, child_is_leaf) + FILE_NAME_SIZE], NO_BLOCK);
                return;
            }
        }

        if (uf_pos < top_used) {
            auto right_byte_pos = uf_byte_pos + record_size(false);
            auto right_acc = buffer.block(intFromBytes(&top_acc.read()[right_byte_pos - 4]), block_id().first);
            auto right_used = node_used(right_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, child_is_leaf);
            if (right_used - 1 > half_child_num_keys) {
                if (top_acc.buffer().isDebugging()) {
                    std::cout << "Rotate from right" << std::endl;
                }
                // Rotate from right
                // Copy top record to UF
                memcpy(
                    &uf_acc.writable()[fname_location(BTREE_NODE_OFFSET, uf_used, child_is_leaf)],
                    &top_acc.read()[uf_byte_pos],
                    BTREE_RECORD_SIZE
                );
                // Copy right record to top
                memcpy(
                    &top_acc.writable()[uf_byte_pos],
                    &right_acc.read()[fname_location(BTREE_NODE_OFFSET, 0, child_is_leaf)],
                    BTREE_RECORD_SIZE
                );
                // Copy right pointer to UF
                if (!child_is_leaf) {
                    memcpy(
                        &uf_acc.writable()[fname_location(BTREE_NODE_OFFSET, uf_used, child_is_leaf) + FILE_NAME_SIZE + 4],
                        &right_acc.read()[BTREE_NODE_OFFSET],
                        4
                    );
                }
                // Move right entries down one
                memmove(
                    &right_acc.writable()[BTREE_NODE_OFFSET],
                    &right_acc.read()[BTREE_NODE_OFFSET + record_size(child_is_leaf)],
                    record_size(child_is_leaf) * (right_used - 1) + 4 * !child_is_leaf
                );
                // Clear right
                intToBytes(&right_acc.writable()[fname_location(BTREE_NODE_OFFSET, right_used - 1, child_is_leaf) + FILE_NAME_SIZE], NO_BLOCK);
                return;
            }
        }

        // Must merge

        if (uf_pos) {
            if (top_acc.buffer().isDebugging()) {
                std::cout << "Merge with left" << std::endl;
            }
            auto left_byte_pos = uf_byte_pos - record_size(false);
            auto left_acc = buffer.block(intFromBytes(&top_acc.read()[left_byte_pos - 4]), block_id().first);
            auto left_used = node_used(left_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, child_is_leaf);
            // Top -> Left
            memcpy(
                &left_acc.writable()[fname_location(BTREE_NODE_OFFSET, left_used, child_is_leaf)],
                &top_acc.read()[left_byte_pos],
                BTREE_RECORD_SIZE
            );
            ++left_used;
            // UF -> Left
            memcpy(
                &left_acc.writable()[fname_location(BTREE_NODE_OFFSET, left_used, child_is_leaf) - 4 * !child_is_leaf],
                &uf_acc.read()[BTREE_NODE_OFFSET],
                record_size(child_is_leaf) * uf_used + 4 * !child_is_leaf
            );
            // Delete from parent
            remove_from_node(uf_pos - 1, top_acc, top_offset, top_size, false);
        }
        else if (uf_pos < top_used) {
            if (top_acc.buffer().isDebugging()) {
                std::cout << "Merge with right" << std::endl;
            }
            auto right_byte_pos = uf_byte_pos + record_size(false);
            auto right_acc = buffer.block(intFromBytes(&top_acc.read()[right_byte_pos - 4]), block_id().first);
            auto right_used = node_used(right_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, child_is_leaf);

            // Move right entries down to make space
            memmove(
                &right_acc.writable()[BTREE_NODE_OFFSET + record_size(child_is_leaf) * (uf_used + 1)],
                &right_acc.read()[BTREE_NODE_OFFSET],
                record_size(child_is_leaf) * right_used + 4 * !child_is_leaf
            );
            // Move left entries
            memmove(
                &right_acc.writable()[BTREE_NODE_OFFSET],
                &uf_acc.read()[BTREE_NODE_OFFSET],
                record_size(child_is_leaf) * uf_used + 4 * !child_is_leaf
            );
            // Move top entry
            memcpy(
                &right_acc.writable()[fname_location(BTREE_NODE_OFFSET, uf_used, child_is_leaf)],
                &top_acc.read()[uf_byte_pos],
                BTREE_RECORD_SIZE
            );
            // Delete from parent
            memmove(&top_acc.writable()[uf_byte_pos - 4],
                    &top_acc.writable()[uf_byte_pos + record_size(false) - 4],
                    record_size(false) * (top_used - uf_pos - 1) + 4);

            auto end_pos = fname_location(top_offset, top_used - 1, false);
            intToBytes(&top_acc.writable()[end_pos + FILE_NAME_SIZE], NO_BLOCK);
        }
        else {
            ensure(false, "Dir::refill_node") << "Could not refill node!";
        }
    }
    buffer.deallocateBlock(uf_blk_id, block_id().first);
    intToBytes(&header_acc.writable()[1], blocks() - 1);
}

bool Dir::pop_left_largest_and_replace_rec(BlockAccessor& orig_acc, unsigned int orig_byte_pos,
                                           BlockAccessor& acc, unsigned int byte_pos, unsigned int height) {
    auto blk_id = intFromBytes(&acc.read()[byte_pos-4]);
    --height;
    auto left_acc = buffer.block(blk_id, acc.block_id().first);
    if (!height) {
        auto left_pos = node_used(left_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, !height) - 1;
        auto left_byte_pos = fname_location(BTREE_NODE_OFFSET, left_pos, !height);
        memcpy(&orig_acc.writable()[orig_byte_pos], &left_acc.read()[left_byte_pos], BTREE_RECORD_SIZE);
        intToBytes(&left_acc.writable()[left_byte_pos + FILE_NAME_SIZE], NO_BLOCK);
        return left_pos < num_keys(BTREE_NODE_SIZE, height) / 2;
    }
    else {
        auto left_pos = node_used(left_acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, !height);
        auto left_byte_pos = fname_location(BTREE_NODE_OFFSET, left_pos, !height);
        auto res = pop_left_largest_and_replace_rec(orig_acc, orig_byte_pos, left_acc, left_byte_pos, height);
        if (height + 1 == this->height()) {
            return res;
        }
        else if (res) {
            refill_node(left_acc, left_pos, left_byte_pos, height, BTREE_NODE_OFFSET, BTREE_NODE_SIZE);
            return false;
        }
        return res;
    }
}

bool Dir::pop_left_largest_and_replace(BlockAccessor& acc, unsigned int byte_pos, unsigned int height) {
    return pop_left_largest_and_replace_rec(acc, byte_pos, acc, byte_pos, height);
}

std::pair<unsigned int, bool> Dir::node_remove(const secure_string& fname, unsigned int blck_id, unsigned int height) {
    auto acc = buffer.block(blck_id, block_id().first);
    if (height == 0) {
        // Leaf
        auto pos = find_key_pos(fname, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, true);
        ensure(pos != NO_BLOCK, "Dir::node_remove") << "Key " << fname << " not in the tree";
        auto bytes_pos = fname_location(BTREE_NODE_OFFSET, pos, true);
        ensure(fname_equals(fname, acc, bytes_pos), "Dir::node_remove") << "Key " << fname << " not in the tree";
        auto value = intFromBytes(&acc.read()[bytes_pos + FILE_NAME_SIZE]);
        remove_from_node(pos, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, true);
        return {value, node_used(acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, true) < num_keys(BTREE_NODE_SIZE, true) / 2};
    }

    // Inner
    auto pos = find_key_pos_or_end(fname, acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false);
    auto bytes_pos = fname_location(BTREE_NODE_OFFSET, pos, false);
    if (fname_equals(fname, acc, bytes_pos)) {
        auto value = intFromBytes(&acc.read()[bytes_pos + FILE_NAME_SIZE]);
        pop_left_largest_and_replace(acc, bytes_pos, height);
        return {value, node_used(acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false) < num_keys(BTREE_NODE_SIZE, false) / 2};
    }
    else {
        auto v = intFromBytes(&acc.read()[fname_location(BTREE_NODE_OFFSET, pos, false) - 4]);
        auto [value, underfilled] = node_remove(fname, v, height - 1);
        if (underfilled) {
            refill_node(acc, pos, bytes_pos, height, BTREE_NODE_OFFSET, BTREE_NODE_SIZE);
        }
        return {value, node_used(acc, BTREE_NODE_OFFSET, BTREE_NODE_SIZE, false) < num_keys(BTREE_NODE_SIZE, false) / 2};
    }
}

unsigned int Dir::remove(const secure_string& fname) {
    if (height() == 0) {
        // Leaf
        auto pos = find_key_pos(fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true);
        ensure(pos != NO_BLOCK, "Dir::remove") << "Key " << fname << " not in the tree";
        auto bytes_pos = fname_location(BTREE_HEADER_OFFSET, pos, true);
        ensure(fname_equals(fname, header_acc, bytes_pos), "Dir::remove") << "Key " << fname << " not in the tree";
        auto value = intFromBytes(&header_acc.read()[bytes_pos + FILE_NAME_SIZE]);
        remove_from_node(pos, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true);
        return value;
    }

    // Inner
    auto pos = find_key_pos_or_end(fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false);
    auto bytes_pos = fname_location(BTREE_HEADER_OFFSET, pos, false);
    if (fname_equals(fname, header_acc, bytes_pos)) {
        auto value = intFromBytes(&header_acc.read()[bytes_pos + FILE_NAME_SIZE]);
        auto underfilled = pop_left_largest_and_replace(header_acc, bytes_pos, height());
        if (underfilled) {
            if (!node_used(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false)) {
                unsplit_root();
            }
        }
        return value;
    }
    else {
        auto v = intFromBytes(&header_acc.read()[fname_location(BTREE_HEADER_OFFSET, pos, false) - 4]);
        auto [value, underfilled] = node_remove(fname, v, height() - 1);
        if (underfilled) {
            if (!node_used(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, false)) {
                unsplit_root();
            }
            else {
                refill_node(header_acc, pos, bytes_pos, height(), BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE);
            }
        }
        return value;
    }
}

void print_single(const BlockAccessor& acc, unsigned int byte_pos, unsigned int height, unsigned int total_height) {
    for (unsigned int i = 0; i < total_height - height + 1; ++i) {
        std::cout << "    ";
    }
    auto value = intFromBytes(&acc.read()[byte_pos + FILE_NAME_SIZE]);
    if (value != NO_BLOCK) {
        std::cout << fname_from_bytes(acc.read(), byte_pos) << " - " << value << std::endl;
    }
    else {
        std::cout << "Empty!" << std::endl;
    }
}

void rec_print_node(Buffer& buf, unsigned int block_id, bool hidden, unsigned int height, unsigned int total_height) {
    auto is_leaf = height == 0;
    auto acc = buf.block(block_id, hidden);
    if (!is_leaf) {
        rec_print_node(buf, intFromBytes(&acc.read()[BTREE_NODE_OFFSET]), hidden, height - 1, total_height);
    }
    for (unsigned int i = 0; i < num_keys(BTREE_NODE_SIZE, is_leaf); ++i) {
        auto pos = fname_location(BTREE_NODE_OFFSET, i, is_leaf);
        print_single(acc, pos, height, total_height);
        auto value = intFromBytes(&acc.read()[pos + FILE_NAME_SIZE]);
        if (value != NO_BLOCK && !is_leaf) {
            rec_print_node(buf, intFromBytes(&acc.read()[pos + FILE_NAME_SIZE + 4]), hidden, height - 1, total_height);
        }
    }
}

void Dir::debug() {
    std::cout << "Root" << std::endl;
    auto root_is_leaf = height() == 0;
    if (!root_is_leaf) {
        rec_print_node(buffer, intFromBytes(&header_acc.read()[BTREE_HEADER_OFFSET]), block_id().first, height() - 1, height());
    }
    for (unsigned int i = 0; i < num_keys(BTREE_HEADER_SIZE, root_is_leaf); ++i) {
        auto pos = fname_location(BTREE_HEADER_OFFSET, i, root_is_leaf);
        print_single(header_acc, pos, height(), height());
        auto value = intFromBytes(&header_acc.read()[pos + FILE_NAME_SIZE]);
        if (value != NO_BLOCK && !root_is_leaf) {
            rec_print_node(buffer, intFromBytes(&header_acc.read()[pos + FILE_NAME_SIZE + 4]), block_id().first, height() - 1, height());
        }
    }
}

Dir Dir::newDir(Buffer& buffer, bool hidden) {
    auto acc = buffer.allocateBlock(hidden);
    auto& data = acc.writable();
    intToBytes(&data[0], DIR_TYPE);
    intToBytes(&data[BLOCK_TREE_OFFSET], 0);
    intToBytes(&data[BLOCK_TREE_OFFSET + 4], 0);
    clean_node_block(acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true);
    return {buffer, std::move(acc)};
}

DirIterator Dir::begin() const {
    if (!height() && !node_used(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true)) {
        return end();
    }

    std::vector<BlockAccessor> accessors;
    std::vector<unsigned int> positions;
    positions.push_back(0);
    auto blk_id = intFromBytes(&header_acc.read()[BTREE_HEADER_OFFSET]);
    for (auto h = 0u; h < height(); ++h) {
        accessors.emplace_back(buffer.block(blk_id, block_id().first));
        positions.push_back(0);
        blk_id = intFromBytes(&accessors.back().read()[BTREE_NODE_OFFSET]);
    }
    return {*this, std::move(accessors), std::move(positions), block_id().first};
}

DirIterator Dir::end() const {
    return {*this, {}, {}, block_id().first};
}

DirIterator Dir::find(const secure_string& fname) const {
    if (!height() && !node_used(header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, true)) {
        return end();
    }

    std::vector<BlockAccessor> accessors;
    std::vector<unsigned int> positions;
    auto height_ = height();
    positions.push_back(find_key_pos_or_end(fname, header_acc, BTREE_HEADER_OFFSET, BTREE_HEADER_SIZE, !height_));
    auto pos = fname_location(BTREE_HEADER_OFFSET, positions.back(), !height_);
    auto blk_id = intFromBytes(&header_acc.read()[pos - 4]);
    for (auto h = 0u; h < height_; ++h) {
        auto& acc = accessors.size() ? accessors.back() : header_acc;
        if (fname_equals(fname, acc, pos)) {
            return {*this, std::move(accessors), std::move(positions), block_id().first};
        }
        accessors.emplace_back(buffer.block(blk_id, block_id().first));
        positions.push_back(find_key_pos_or_end(fname, accessors.back(), BTREE_NODE_OFFSET, BTREE_NODE_SIZE, height_ == h + 1));
        pos = fname_location(BTREE_NODE_OFFSET, positions.back(), height_ == h + 1);
        if (height_ != h + 1) {
            blk_id = intFromBytes(&accessors.back().read()[pos - 4]);
        }
    }
    auto& acc = accessors.size() ? accessors.back() : header_acc;
    if (fname_equals(fname, acc, pos)) {
        return {*this, std::move(accessors), std::move(positions), block_id().first};
    }
    return end();
}

DirIterator::DirIterator(const Dir& dir, std::vector<BlockAccessor>&& accessors, std::vector<unsigned int>&& positions, bool hidden) :
        dir(dir), accessors(std::move(accessors)), positions(positions), hidden(hidden) {
}

std::pair<secure_string, unsigned int> DirIterator::operator*() const {
    ensure(positions.size(), "DirIterator::operator*") << "Cannot dereference end";
    auto& acc = accessors.size() ? accessors.back() : dir.header_acc;
    auto pos = fname_location(accessors.size() ? BTREE_NODE_OFFSET : BTREE_HEADER_OFFSET, positions.back(), accessors.size() == dir.height());
    return {fname_from_bytes(acc.read(), pos), intFromBytes(&acc.read()[pos + FILE_NAME_SIZE])};
}

DirIterator& DirIterator::operator++() {
    ensure(positions.size(), "DirIterator::operator++()") << "Cannot increment end";
    auto start_size = positions.size();
    for (auto i = 0u; i < start_size; ++i) {
        auto level = start_size - i - 1;
        auto offset = level ? BTREE_NODE_OFFSET : BTREE_HEADER_OFFSET;
        auto size = level ? BTREE_NODE_SIZE : BTREE_HEADER_SIZE;
        auto is_leaf = level == dir.height();
        auto& accessor = level ? accessors[level - 1] : dir.header_acc;
        if (i == 0 && !is_leaf && positions.back() < node_used(accessor, offset, size, is_leaf)) {
            auto pos = fname_location(offset, positions.back(), is_leaf);
            auto blk_id = intFromBytes(&accessor.read()[pos + FILE_NAME_SIZE + 4]);
            for (auto j = 0u; level + j < dir.height(); ++j) {
                accessors.push_back(dir.buffer.block(blk_id, hidden));
                positions.push_back(0);
                blk_id = intFromBytes(&accessors.back().read()[BTREE_NODE_OFFSET]);
            }
            ++positions[level];
            break;
        }
        else if (i == 0) {
            ++positions[level];
        }
        if (positions[level] >= node_used(accessor, offset, size, is_leaf)) {
            positions.pop_back();
            if (level) {
                accessors.pop_back();
            }
        }
        else {
            break;
        }
    }
    return *this;
}

bool DirIterator::operator==(const DirIterator& other) const {
    return other.positions == positions;
}
