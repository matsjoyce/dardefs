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

#include "types.hpp"


secure_string operator "" _ss(const char* str, size_t len) {
    return secure_string(reinterpret_cast<const unsigned char*>(str), len);
}

std::ostream& operator<<(std::ostream& stream, const secure_string& str) {
    stream.write(reinterpret_cast<const char*>(&str[0]), str.size());
    return stream;
}

secure_string string_to_ss(const std::string& s) {
    return secure_string(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}
