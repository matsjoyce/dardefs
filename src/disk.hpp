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

#ifndef DISK_HPP
#define DISK_HPP

#include <string>
#include <tuple>
#include <fstream>
#include <mutex>

#include "types.hpp"

enum class BlockMappingType {
    COVER,
    HIDDEN,
    NEITHER
};

class Disk {
    std::fstream file;
    std::mutex file_lock;
    unsigned int number_of_blocks;
    unsigned int file_size;
    secure_string cover_key, hidden_key;

    void readRawBlock(unsigned int location, secure_string& out);
    void writeRawBlock(unsigned int location, const secure_string& in);
    void decryptBlock(const secure_string& in, secure_string& out, bool hidden) const;
    void encryptBlock(const secure_string& in, secure_string& out, bool hidden) const;

public:
    Disk(std::string fname, secure_string cover_key, secure_string hidden_key);

    void readBlock(unsigned int location, bool hidden, secure_string& buffer);
    void writeBlock(unsigned int location, bool hidden, const secure_string& buffer);

    unsigned int numberOfBlocks() const;
};

#endif // DISK_HPP
