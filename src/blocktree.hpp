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

#ifndef BLOCKTREE_HPP
#define BLOCKTREE_HPP

#include <iterator>

#include "buffer.hpp"

class BlockTree;

class BlockTreeIterator {
    const BlockTree& tree;
    std::vector<BlockAccessor> accessors;
    std::vector<unsigned int> positions;
    unsigned int position_;

    void updateAccessors(unsigned int start_level);

public:
    BlockTreeIterator(const BlockTree& tree, unsigned int start);
    unsigned int operator*() const;
    BlockTreeIterator& operator++();
    void operator++(int) { ++*this; }
    BlockTreeIterator& operator--();
    void operator--(int) { --*this; }
    bool operator==(const BlockTreeIterator& other) const;
    bool operator!=(const BlockTreeIterator& other) const { return !(*this == other); }
    bool at_end() const;
    bool at_start() const;
    unsigned int position() const;
};

class BlockTree {
    Buffer& buffer;
    BlockAccessor& acc;
    unsigned int offset;
public:
    BlockTree(Buffer& buf, BlockAccessor& acc, unsigned int offset);

    unsigned int numberOfBlocks() const;

    void add(unsigned int value);
    unsigned int remove();
    void debug();

    BlockTreeIterator iter(unsigned int start) const;
    BlockTreeIterator begin() const;
    BlockTreeIterator end() const;

    friend class BlockTreeIterator;
};

#endif // BLOCKTREE_HPP
