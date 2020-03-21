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

#ifndef DIR_HPP
#define DIR_HPP

#include "buffer.hpp"

class Dir;

class DirIterator {
    const Dir& dir;
    std::vector<BlockAccessor> accessors;
    std::vector<unsigned int> positions;
    bool hidden;

public:
    DirIterator(const Dir& dir, std::vector<BlockAccessor>&& accessors, std::vector<unsigned int>&& positions, bool hidden);
    std::pair<secure_string, unsigned int> operator*() const;
    DirIterator& operator++();
    void operator++(int) { ++*this; }
    bool operator==(const DirIterator& other) const;
    bool operator!=(const DirIterator& other) const { return !(*this == other); }
};

class Dir {
    Buffer& buffer;
    BlockAccessor header_acc;

    unsigned int height() const;
    unsigned int blocks() const;

    bool node_add(const secure_string& fname, unsigned int value, unsigned int blck_id, unsigned int height);
    std::pair<unsigned int, bool> node_remove(const secure_string& fname, unsigned int blck_id, unsigned int height);
    std::tuple<unsigned int, secure_string, unsigned int> split_node(unsigned int blck_id, unsigned int height);
    void refill_node(BlockAccessor& top_acc, unsigned int uf_pos, unsigned int uf_byte_pos, unsigned int height, unsigned int top_offset, unsigned int top_size);
    void split_root();
    void unsplit_root();
    void merge_nodes();

public:
    Dir(Buffer& buf, BlockAccessor&& header_acc);
    Dir(Buffer& buf, unsigned int block_id, bool hidden);
    Dir(Dir&& other);

    std::pair<bool, unsigned int> block_id() const;

    unsigned int size() const;

    void add(const secure_string& fname, unsigned int value);
    unsigned int remove(const secure_string& fname);
    void debug();

    DirIterator find(const secure_string& start) const;
    DirIterator begin() const;
    DirIterator end() const;

    static Dir newDir(Buffer& buffer, bool hidden);

    friend class DirIterator;
};

#endif // DIR_HPP
