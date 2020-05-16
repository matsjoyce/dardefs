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

#include "blockfile.hpp"
#include "utilities.hpp"
#include "consts.hpp"

#include <iostream>


BlockFile::BlockFile(Buffer& buf, unsigned int block_id, bool hidden) :
        BlockFile(buf, buf.block(block_id, hidden)) {
}

BlockFile::BlockFile(Buffer& buf, BlockAccessor&& header_acc) :
        buffer(buf), header_acc(std::move(header_acc)), tree(buf, this->header_acc, BLOCK_TREE_OFFSET) {

    ensure(this->header_acc.read()[0] == FILE_TYPE, "BlockFile::BlockFile") << "This block is not a file header";
}

BlockFile::BlockFile(BlockFile&& other) :
        BlockFile(other.buffer, std::move(other.header_acc)) {
}

unsigned int BlockFile::numberOfBlocks() const {
    return tree.numberOfBlocks() + 1;
}

unsigned int BlockFile::numberOfBytes() const {
    return tree.numberOfBlocks() * LOGICAL_BLOCK_SIZE + DATA_SIZE;
}

std::pair<unsigned int, unsigned int> BlockFile::positionForByte(unsigned int pos) const {
    if (pos < DATA_SIZE) {
        return {0, pos};
    }
    pos -= DATA_SIZE;
    return {pos / LOGICAL_BLOCK_SIZE + 1, pos % LOGICAL_BLOCK_SIZE};
}

std::pair<bool, unsigned int> BlockFile::block_id() const {
    return header_acc.block_id();
}

void BlockFile::addBlock() {
    tree.add(buffer.allocateBlock(header_acc.block_id().first).block_id().second);
}

void BlockFile::removeBlock() {
    buffer.deallocateBlock(tree.remove(), header_acc.block_id().first);
}

void BlockFile::truncate() {
    auto to_delete = tree.numberOfBlocks();
    for (unsigned int i = 0; i < to_delete; ++i) {
        removeBlock();
    }
}

BlockFile BlockFile::newFile(Buffer& buffer, bool hidden) {
    auto acc = buffer.allocateBlock(hidden);
    auto& data = acc.writable();
    intToBytes(&data[0], FILE_TYPE);
    intToBytes(&data[BLOCK_TREE_OFFSET], 0);
    return {buffer, std::move(acc)};
}

BlockFileIterator BlockFile::begin() {
    return {*this, 0};
}

BlockFileIterator BlockFile::iter(unsigned int start) {
    return {*this, start};
}

BlockFileIterator BlockFile::end() {
    return {*this, numberOfBlocks()};
}

BlockFileIterator::BlockFileIterator(BlockFile& file, unsigned int start) :
        file(file), very_start(start == 0), iter(file.tree.iter(start == 0 ? 0 : start - 1)) {
    if (!iter.at_end() && !very_start) {
        node_acc = std::make_unique<BlockAccessor>(file.buffer.block(*iter, file.header_acc.block_id().first));
    }
}

FileBlock BlockFileIterator::operator*() {
    if (very_start) {
        return {file.header_acc, DATA_OFFSET, DATA_SIZE};
    }
    ensure(!!node_acc, "BlockFileIterator::operator*") << "No acc stored!";
    return {*node_acc.get(), 0, LOGICAL_BLOCK_SIZE};
}

BlockFileIterator& BlockFileIterator::operator++() {
    if (very_start) {
        very_start = false;
    }
    else {
        ++iter;
    }
    if (!iter.at_end()) {
        node_acc = std::make_unique<BlockAccessor>(file.buffer.block(*iter, file.header_acc.block_id().first));
    }
    return *this;
}

BlockFileIterator& BlockFileIterator::operator--() {
    if (iter.at_start()) {
        very_start = true;
    }
    else {
        --iter;
    }
    if (very_start) {
        node_acc.reset();
    }
    else if (!iter.at_end()) {
        node_acc = std::make_unique<BlockAccessor>(file.buffer.block(*iter, file.header_acc.block_id().first));
    }
    return *this;
}

bool BlockFileIterator::operator==(const BlockFileIterator& other) const {
    return iter == other.iter && very_start == other.very_start;
}

bool BlockFileIterator::at_end() const {
    return iter.at_end();
}

bool BlockFileIterator::at_start() const {
    return very_start;
}

unsigned int BlockFileIterator::position() const {
    return very_start ? 0 : iter.position() + 1;
}
