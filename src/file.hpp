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

#ifndef FILE_HPP
#define FILE_HPP

#include "blockfile.hpp"

class Buffer;

class File {
    BlockFile bf;

    File(BlockFile&& bf);

public:
    File(Buffer& buf, BlockAccessor&& acc);
    File(Buffer& buf, unsigned int block_id, bool hidden);
    File(File&& other);

    std::pair<bool, unsigned int> block_id() const;

    unsigned int size(); // XXX const this
    unsigned int numberOfBlocks() const;
    unsigned int blocksForSize(unsigned int size) const;

    unsigned int read(unsigned int pos, unsigned int n, unsigned char* buf);
    unsigned int write(unsigned int pos, unsigned int n, const unsigned char* buf);
    void truncate(unsigned int pos);

    static File newFile(Buffer& buffer, bool hidden);
};

#endif // FILE_HPP
