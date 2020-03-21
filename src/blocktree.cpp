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

#include "blocktree.hpp"
#include "utilities.hpp"
#include "consts.hpp"

#include <iostream>

// Pair of offset, size of node to the insertion / deletion point
std::vector<unsigned int> levels(unsigned int num_blocks) {
    std::vector<unsigned int> res;
    unsigned int offset = num_blocks;

    while (offset >= NUM_HEADER_BLOCK_TREE_ENTRIES) {
        res.push_back(offset % NUM_TREE_BLOCK_TREE_ENTRIES);
        offset /= NUM_TREE_BLOCK_TREE_ENTRIES;
    }
    res.push_back(offset);
//     std::reverse(res.begin(), res.end());
    return res;
}

BlockTree::BlockTree(Buffer& buf, BlockAccessor& acc, unsigned int offset) :
        buffer(buf), acc(acc), offset(offset) {
}

unsigned int BlockTree::numberOfBlocks() const {
    return intFromBytes(&acc.read()[offset]);
}

void BlockTree::add(unsigned int value) {
    auto path = levels(numberOfBlocks());
    auto before_path = levels(numberOfBlocks() - 1);
    auto hidden = acc.block_id().first;
    intToBytes(&acc.writable()[offset], numberOfBlocks() + 1);

    if (path.size() == 1) {
        intToBytes(&acc.writable()[offset + 4 + 4 * path.back()], value);
        return;
    }

    if (path.size() != before_path.size()) {
        // Split
        auto node_acc = buffer.allocateBlock(hidden);
        node_acc.writable().replace(0, 4 * NUM_HEADER_BLOCK_TREE_ENTRIES, &acc.read()[offset + 4], 4 * NUM_HEADER_BLOCK_TREE_ENTRIES);
        // XXX: Safety feature below, remove when confident.
        acc.writable().replace(offset + 4, 4 * NUM_HEADER_BLOCK_TREE_ENTRIES, 4 * NUM_HEADER_BLOCK_TREE_ENTRIES, '\xff');
        intToBytes(&acc.writable()[offset + 4], node_acc.block_id().second);
        before_path.push_back(0);
    }

    unsigned int block_id;
    bool need_to_allocate = false;

    if (path.back() != before_path.back()) {
        need_to_allocate = true;
        block_id = buffer.allocateBlock(hidden).block_id().second;
        intToBytes(&acc.writable()[offset + 4 + 4 * path.back()], block_id);
    }
    else {
        block_id = intFromBytes(&acc.read()[offset + 4 + 4 * path.back()]);
    }

    for (unsigned int level = 1; level < path.size() - 1; ++level) {
        auto node_acc = buffer.block(block_id, hidden);
        auto pos = path.size() - level - 1;
        if (pos && (need_to_allocate || path[pos] != before_path[pos])) {
            need_to_allocate = true;
            block_id = buffer.allocateBlock(hidden).block_id().second;
            intToBytes(&node_acc.writable()[4 * path[pos]], block_id);
        }
        else {
            block_id = intFromBytes(&node_acc.read()[4 * path[pos]]);
        }
    }

    auto node_acc = buffer.block(block_id, hidden);
    intToBytes(&node_acc.writable()[4 * path.front()], value);
}

unsigned int BlockTree::remove() {
    ensure(numberOfBlocks(), "BlockTree::remove") << "Remove from an empty tree";
    auto path = levels(numberOfBlocks() - 1);
    auto after_path = levels(numberOfBlocks() - 2);
    auto hidden = acc.block_id().first;
    intToBytes(&acc.writable()[offset], numberOfBlocks() - 1);

    if (path.size() == 1) {
        auto value = intFromBytes(&acc.read()[offset + 4 + 4 * path.back()]);
        intToBytes(&acc.writable()[offset + 4 + 4 * path.back()], -1);
        return value;
    }

    auto block_id = intFromBytes(&acc.read()[offset + 4 + 4 * path.back()]);
    std::vector<unsigned int> deallocate;

    for (unsigned int level = 1; level < path.size() - 1; ++level) {
        auto node_acc = buffer.block(block_id, hidden);
        auto pos = path.size() - level - 1;
        if (!path[pos]) {
            deallocate.push_back(block_id);
        }
        else {
            deallocate.clear();
        }
        block_id = intFromBytes(&node_acc.read()[4 * path[pos]]);
    }

    unsigned int value;
    {
        auto node_acc = buffer.block(block_id, hidden);
        value = intFromBytes(&node_acc.read()[4 * path.front()]);
        if (!path.front()) {
            deallocate.push_back(block_id);
        }
        else {
            deallocate.clear();
        }
    }

    for (auto deall_id : deallocate) {
        buffer.deallocateBlock(deall_id, hidden);
    }

    if (path.size() != after_path.size()) {
        auto deall_id = intFromBytes(&acc.read()[offset + 4]);
        {
            auto node_acc = buffer.block(deall_id, hidden);
            acc.writable().replace(offset + 4, 4 * NUM_HEADER_BLOCK_TREE_ENTRIES, &node_acc.read()[0], 4 * NUM_HEADER_BLOCK_TREE_ENTRIES);
        }
        buffer.deallocateBlock(deall_id, hidden);
    }

    return value;
}

void print_node_single(unsigned int block_id, unsigned int levels, unsigned int total_levels) {
    for (unsigned int i = 0; i < total_levels - levels; ++i) {
        std::cout << "    ";
    }
    if (!levels) {
        std::cout << "VALUE: " << block_id << std::endl;
    }
    else {
        std::cout << "LEVEL " << levels << ": BLKID " << block_id << std::endl;
    }
}

void print_node_full(Buffer& buf, unsigned int block_id, bool hidden, unsigned int levels, unsigned int total_levels) {
    print_node_single(block_id, levels, total_levels);
    if (levels) {
        auto acc = buf.block(block_id, hidden);
        for (unsigned int i = 0; i < NUM_TREE_BLOCK_TREE_ENTRIES; ++i) {
            print_node_full(buf, intFromBytes(&acc.read()[4 * i]), hidden, levels - 1, total_levels);
        }
    }
}

void BlockTree::debug() {
    if (!numberOfBlocks()) {
        std::cout << "Empty!" << std::endl;
        return;
    }
    auto path = levels(numberOfBlocks() - 1);
    std::cout << "PATH ";
    for (auto v : path) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
    print_node_single(acc.block_id().second, path.size(), path.size());
    for (unsigned int i = 0; i < path.back(); ++i) {
        print_node_full(buffer, intFromBytes(&acc.read()[offset + 4 + 4 * i]), acc.block_id().first, path.size() - 1, path.size());
    }
    auto block_id = intFromBytes(&acc.read()[offset + 4 + 4 * path.back()]);
    for (unsigned int level = 1; level <= path.size(); ++level) {
        print_node_single(block_id, path.size() - level, path.size());
        if (level != path.size()) {
            auto node_acc = buffer.block(block_id, acc.block_id().first);
            for (unsigned int i = 0; i < path[path.size() - level - 1]; ++i) {
                print_node_full(buffer, intFromBytes(&node_acc.read()[4 * i]), acc.block_id().first, path.size() - 1 - level, path.size());
            }
            block_id = intFromBytes(&node_acc.read()[4 * path[path.size() - level - 1]]);
        }
    }
}

BlockTreeIterator BlockTree::iter(unsigned int start) const {
    return {*this, start};
}

BlockTreeIterator BlockTree::begin() const {
    return {*this, 0};
}

BlockTreeIterator BlockTree::end() const {
    return {*this, numberOfBlocks()};
}

BlockTreeIterator::BlockTreeIterator(const BlockTree& tree, unsigned int start) :
        tree(tree), position_(start) {
    if (tree.numberOfBlocks()) {
        auto num_levels = levels(tree.numberOfBlocks() - 1).size();
        positions = levels(start);
        while (positions.size() < num_levels) {
            positions.push_back(0);
        }
        updateAccessors(0);
    }
}

void BlockTreeIterator::updateAccessors(unsigned int start_level) {
//     std::cout << "S" << std::endl;
//     std::cout << "PATH ";
//     for (auto x : positions) {
//         std::cout << x << " ";
//     }
//     std::cout << std::endl;
    if (position_ == tree.numberOfBlocks()) {
        accessors.clear();
        return;
    }
    else if (!accessors.size()) {
        start_level = 0;
    }
    auto hidden = tree.acc.block_id().first;
    unsigned int block_id;
    for (unsigned int i = start_level; i < positions.size() - 1 && accessors.size(); ++i) {
        accessors.pop_back();
    }
    if (!start_level) {
        block_id = intFromBytes(&tree.acc.read()[tree.offset + 4 + 4 * positions.back()]);
    }
    else {
        block_id = intFromBytes(&accessors.back().read()[4 * positions[positions.size() - start_level - 1]]);
    }
//     std::cout << "S " << block_id << " " << start_level << std::endl;
    for (unsigned int i = start_level + 1; i < positions.size(); ++i) {
        auto node_acc = tree.buffer.block(block_id, hidden);
        block_id = intFromBytes(&node_acc.read()[4 * positions[positions.size() - i - 1]]);
        accessors.emplace_back(std::move(node_acc));
//         std::cout << "F " << block_id << std::endl;
    }
//     std::cout << "E" << std::endl;
}

bool BlockTreeIterator::operator==(const BlockTreeIterator& other) const {
    return other.position_ == position_;
}

unsigned int BlockTreeIterator::operator*() const {
    if (positions.size() == 1) {
        return intFromBytes(&tree.acc.read()[tree.offset + 4 + 4 * positions.front()]);
    }
    std::cout << positions.front() << " " << accessors.back().block_id().second << std::endl;
    return intFromBytes(&accessors.back().read()[4 * positions.front()]);
}

BlockTreeIterator& BlockTreeIterator::operator++() {
    ensure(position_ < tree.numberOfBlocks(), "BlockTreeIterator::operator++") << "Incrementing the end iterator";
    ++position_;
//     std::cout << position << std::endl;

    unsigned int level = 0;
    for (; level < positions.size(); ++level) {
        ++positions[level];
        if (positions[level] == NUM_TREE_BLOCK_TREE_ENTRIES) {
            positions[level] = 0;
        }
        else {
            break;
        }
    }
    updateAccessors(positions.size() - level - 1);
    return *this;
}

BlockTreeIterator& BlockTreeIterator::operator--() {
    ensure(position_ > 0, "BlockTreeIterator::operator--") << "Incrementing the begin iterator";
    --position_;

    unsigned int level = 0;
    for (; level < positions.size(); ++level) {
        if (!positions[level]) {
            positions[level] = NUM_TREE_BLOCK_TREE_ENTRIES - 1;
        }
        else {
            --positions[level];
            break;
        }
    }
    updateAccessors(positions.size() - level - 1);
    return *this;
}

bool BlockTreeIterator::at_end() const {
    return position_ == tree.numberOfBlocks();
}

bool BlockTreeIterator::at_start() const {
    return !position_;
}

unsigned int BlockTreeIterator::position() const {
    return position_;
}
