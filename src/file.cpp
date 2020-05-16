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

#include "file.hpp"
#include "utilities.hpp"
#include "consts.hpp"
#include <iostream>

File::File(Buffer& buf, BlockAccessor&& acc) : bf(buf, std::forward<BlockAccessor>(acc)) {
}


File::File(BlockFile&& bf) : bf(std::forward<BlockFile>(bf)) {

}

File::File(Buffer& buf, unsigned int block_id, bool hidden) :
        bf(buf, block_id, hidden) {
}

File::File(File&& other) :
        File(std::move(other.bf)) {
}

std::pair<bool, unsigned int> File::block_id() const {
    return bf.block_id();
}

unsigned int File::size() {
    auto iter = bf.begin();
    auto fb = *iter;
    return intFromBytes(&fb.acc.read()[fb.offset]);
}

unsigned int File::numberOfBlocks() const {
    return bf.numberOfBlocks();
}

unsigned int File::blocksForSize(unsigned int size) const {
    auto stop_bytes = size + FILE_HEADER_SIZE;
    auto stop = bf.positionForByte(stop_bytes);
    auto stop_block = stop.second ? stop.first : stop.first - 1;
    return std::max(1u, stop_block);
}

unsigned int File::read(unsigned int pos, unsigned int n, unsigned char* buf) {
    auto bytes_start = std::min(pos, size()) + FILE_HEADER_SIZE;
    auto start = bf.positionForByte(bytes_start);
    auto bytes_stop = std::min(pos + n, size()) + FILE_HEADER_SIZE;
    auto stop = bf.positionForByte(bytes_stop);
    auto range = bytes_stop - bytes_start;
    auto iter = bf.iter(start.first);
    unsigned int copied = 0;
    while (true) {
        auto fb = *iter;
        auto read_start = iter.position() == start.first ? start.second : 0;
        auto read_amt = iter.position() == stop.first ? (start.first == stop.first ? stop.second - read_start : stop.second) : fb.size - read_start;
        memcpy(
            &buf[copied],
            &fb.acc.read()[fb.offset + read_start],
            read_amt
        );
        copied += read_amt;
        if (range == copied) {
            return copied;
        }
        ++iter;
    }
}

unsigned int File::write(unsigned int pos, unsigned int n, const unsigned char* buf) {
    auto start = bf.positionForByte(pos + FILE_HEADER_SIZE);
    auto bytes_stop = pos + n + FILE_HEADER_SIZE;
    while (bf.numberOfBytes() < bytes_stop) {
        bf.addBlock();
    }
    auto stop = bf.positionForByte(bytes_stop);
    auto range = bytes_stop - pos - FILE_HEADER_SIZE;
    {
        auto iter = bf.iter(start.first);
        unsigned int copied = 0;
        while (true) {
            auto fb = *iter;
            auto write_start = iter.position() == start.first ? start.second : 0;
            auto write_amt = iter.position() == stop.first ? (start.first == stop.first ? stop.second - write_start : stop.second) : fb.size - write_start;
            memcpy(
                &fb.acc.writable()[fb.offset + write_start],
                &buf[copied],
                write_amt
            );
            copied += write_amt;
            if (range == copied) {
                break;
            }
            ++iter;
        }
    }
    {
        auto iter2 = bf.begin();
        auto fb = *iter2;
        auto new_size = std::max(intFromBytes(&fb.acc.read()[fb.offset]), bytes_stop - FILE_HEADER_SIZE);
        intToBytes(&fb.acc.writable()[fb.offset], new_size);
    }
    return range;
}

void File::truncate(unsigned int pos) {
    auto num_blocks = blocksForSize(pos);
    auto stop_bytes = std::min(pos, size()) + FILE_HEADER_SIZE;

    while (bf.numberOfBlocks() > num_blocks) {
        bf.removeBlock();
    }
    {
        auto iter2 = bf.begin();
        auto fb = *iter2;
        auto new_size = stop_bytes - FILE_HEADER_SIZE;
        intToBytes(&fb.acc.writable()[fb.offset], new_size);
    }
}

File File::newFile(Buffer& buffer, bool hidden) {
    auto bf = BlockFile::newFile(buffer, hidden);
    {
        auto iter = bf.begin();
        auto fb = *iter;
        intToBytes(&fb.acc.writable()[fb.offset], 0);
    }
    return {std::move(bf)};
}
