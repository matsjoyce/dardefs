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

#include "disk.hpp"

#include "cryptopp/modes.h"
#include "cryptopp/aes.h"
#include "cryptopp/filters.h"
#include "cryptopp/osrng.h"

#include "consts.hpp"
#include "utilities.hpp"

#include <iostream>


Disk::Disk(std::string fname, secure_string cover_key, secure_string hidden_key) :
        cover_key(cover_key), hidden_key(hidden_key) {
    ensure(cover_key.size() == KEY_SIZE, "Disk::Disk") << "Cover key is the wrong size";
    ensure(hidden_key.size() == KEY_SIZE, "Disk::Disk") << "Hidden key is the wrong size";

    // unbuffered
    file.rdbuf()->pubsetbuf(0, 0);
    file.open(fname, std::ios::ate | std::ios::out | std::ios::in | std::ios::binary);
    ensure(!!file, "Disk::Disk") << "File could not be opened";
    file_size = file.tellg();
    ensure(file_size % PHYSICAL_BLOCK_SIZE == 0, "Disk::Disk") << "File size is not a multiple of the block size";
    number_of_blocks = file_size / PHYSICAL_BLOCK_SIZE;
}

void Disk::readRawBlock(unsigned int location, secure_string& out) {
    std::lock_guard<std::mutex> guard(file_lock);

    file.seekg(location);
    file.read(reinterpret_cast<char*>(&out[0]), out.size());

    ensure(static_cast<unsigned int>(file.gcount()) == out.size(), "Disk::readRawBlock") << "Did not read enough bytes";
}

void Disk::writeRawBlock(unsigned int location, const secure_string& in) {
    std::lock_guard<std::mutex> guard(file_lock);

    file.seekg(location);
    file.write(reinterpret_cast<const char*>(&in[0]), in.size());
}

void Disk::decryptBlock(const secure_string& in, secure_string& out, bool hidden) const {
    ensure(in.size() % CIPHER_BLOCK_SIZE == 0, "Disk::decryptBlock") << "Input is not a multiple of the cipher block size";
    ensure(out.size() == in.size() - IV_SIZE, "Disk::decryptBlock") << "Output is not the correct size";

    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption d;
    if (hidden) {
        d.SetKeyWithIV(&hidden_key[0], hidden_key.size(), &in[0]);
    }
    else {
        d.SetKeyWithIV(&cover_key[0], cover_key.size(), &in[0]);
    }

    CryptoPP::StringSource ss(&in[IV_SIZE], LOGICAL_BLOCK_SIZE, true,
        new CryptoPP::StreamTransformationFilter(
            d,
            new CryptoPP::ArraySink(&out[0], LOGICAL_BLOCK_SIZE),
            CryptoPP::StreamTransformationFilter::NO_PADDING
        )
    );
}

void Disk::encryptBlock(const secure_string& in, secure_string& out, bool hidden) const {
    ensure(in.size() % CIPHER_BLOCK_SIZE == 0, "Disk::encryptBlock") << "Input is not a multiple of the cipher block size";
    ensure(out.size() == in.size() + IV_SIZE, "Disk::encryptBlock") << "Output is not the correct size";

    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption e;

    CryptoPP::OS_GenerateRandomBlock(false, &out[0], IV_SIZE);

    if (hidden) {
        e.SetKeyWithIV(&hidden_key[0], hidden_key.size(), &out[0]);
    }
    else {
        e.SetKeyWithIV(&cover_key[0], cover_key.size(), &out[0]);
    }

    CryptoPP::StringSource ss(&in[0], LOGICAL_BLOCK_SIZE, true,
        new CryptoPP::StreamTransformationFilter(
            e,
            new CryptoPP::ArraySink(&out[IV_SIZE], LOGICAL_BLOCK_SIZE),
            CryptoPP::StreamTransformationFilter::NO_PADDING
        )
    );
}

void Disk::readBlock(unsigned int location, bool hidden, secure_string& buffer) {
    ensure(buffer.size() == LOGICAL_BLOCK_SIZE, "Disk::readBlock") << "Output is not the correct size";

    secure_string physical_block_buffer(PHYSICAL_BLOCK_SIZE, '\0');

    readRawBlock(location * PHYSICAL_BLOCK_SIZE, physical_block_buffer);
    decryptBlock(physical_block_buffer, buffer, hidden);
}

void Disk::writeBlock(unsigned int location, bool hidden, const secure_string& buffer) {
    ensure(buffer.size() == LOGICAL_BLOCK_SIZE, "Disk::writeBlock") << "Input is not the correct size";

    secure_string physical_block_buffer(PHYSICAL_BLOCK_SIZE, '\0');

    encryptBlock(buffer, physical_block_buffer, hidden);
    writeRawBlock(location * PHYSICAL_BLOCK_SIZE, physical_block_buffer);
}

unsigned int Disk::numberOfBlocks() const {
    return number_of_blocks;
}
