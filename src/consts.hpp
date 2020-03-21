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

#ifndef CONSTS_HPP
#define CONSTS_HPP

const unsigned int PHYSICAL_BLOCK_SIZE = 4096;
const unsigned int CIPHER_BLOCK_SIZE = 16;
const unsigned int IV_SIZE = CIPHER_BLOCK_SIZE;
const unsigned int KEY_SIZE = 16;
const unsigned int LOGICAL_BLOCK_SIZE = PHYSICAL_BLOCK_SIZE - IV_SIZE;
const unsigned int BLOCK_MAPPING_ENTRY_SIZE = 16;

const unsigned char FILE_TYPE = 'F';
const unsigned char DIR_TYPE = 'D';

const unsigned int NUM_HEADER_BLOCK_TREE_ENTRIES = 8;
const unsigned int NUM_TREE_BLOCK_TREE_ENTRIES = LOGICAL_BLOCK_SIZE / 4;
const unsigned int BLOCK_TREE_OFFSET = 1;

const unsigned int DATA_OFFSET = BLOCK_TREE_OFFSET + 4 + 4 * NUM_HEADER_BLOCK_TREE_ENTRIES;
const unsigned int DATA_SIZE = LOGICAL_BLOCK_SIZE - DATA_OFFSET;
const unsigned int FILE_HEADER_SIZE = 4;

const unsigned int FILE_NAME_SIZE = 255;
const unsigned int FILE_PTR_SIZE = 4;
const unsigned int BTREE_RECORD_SIZE = FILE_NAME_SIZE + FILE_PTR_SIZE;

#endif // CONSTS_HPP
