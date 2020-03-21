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

#ifndef BLOCKFILE_HPP
#define BLOCKFILE_HPP

#include "buffer.hpp"
#include "blocktree.hpp"

class BlockFile;

struct FileBlock {
    BlockAccessor& acc;
    unsigned int offset, size;
};

class BlockFileIterator {
    BlockFile& file;
    bool very_start = true;
    BlockTreeIterator iter;
    std::unique_ptr<BlockAccessor> node_acc;

public:
    BlockFileIterator(BlockFile& file, unsigned int start);
    FileBlock operator*();
    BlockFileIterator& operator++();
    void operator++(int) { ++*this; }
    BlockFileIterator& operator--();
    void operator--(int) { --*this; }
    bool operator==(const BlockFileIterator& other) const;
    bool operator!=(const BlockFileIterator& other) const { return !(*this == other); }
    bool at_end() const;
    bool at_start() const;
    unsigned int position() const;
};

class BlockFile {
    Buffer& buffer;
    BlockAccessor header_acc;
    BlockTree tree;

public:
    BlockFile(Buffer& buf, BlockAccessor&& header_acc);
    BlockFile(Buffer& buf, unsigned int block_id, bool hidden);
    BlockFile(BlockFile&& other);

    unsigned int numberOfBlocks() const;
    unsigned int numberOfBytes() const;
    std::pair<unsigned int, unsigned int> positionForByte(unsigned int pos) const;
    std::pair<bool, unsigned int> block_id() const;

    void addBlock();
    void removeBlock();
    void truncate();

    BlockFileIterator iter(unsigned int start);
    BlockFileIterator begin();
    BlockFileIterator end();

    static BlockFile newFile(Buffer& buffer, bool hidden);

    friend class BlockFileIterator;
};

#endif // BLOCKFILE_HPP
