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

#ifndef UTILITIES_HPP
#define UTILITIES_HPP

#include <cstdint>
#include <bit>
#include <sstream>


inline uint32_t intFromBytes(const unsigned char* bytes) {
    uint32_t val = *reinterpret_cast<const uint32_t*>(bytes);
    if constexpr (std::endian::native == std::endian::big) {
        return __builtin_bswap32(val);
    }
    return val;
}

inline void intToBytes(unsigned char* bytes, uint32_t value) {
    if constexpr (std::endian::native == std::endian::big) {
        *reinterpret_cast<uint32_t*>(bytes) = __builtin_bswap32(value);
    }
    else {
        *reinterpret_cast<uint32_t*>(bytes) = value;
    }
}

class ensure {
public:
    ensure(bool ok, std::string location) : ok(ok) {
        if (!ok) {
            stream.reset(new std::stringstream());
            *stream << location << ": ";
        }
    }

    ~ensure() noexcept(false) {
        if (!ok) {
            throw std::logic_error(stream->str());
        }
    }

    template<class T> ensure& operator<<(const T& t) {
        if (!ok) {
            *stream << t;
        }
        return *this;
    }

private:
    bool ok;
    std::unique_ptr<std::stringstream> stream;
};

#endif // UTILITIES_HPP
