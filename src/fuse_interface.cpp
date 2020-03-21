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

#include <variant>
#include <iostream>

#include "fuse_interface.hpp"
#include "disk.hpp"
#include "buffer.hpp"
#include "file.hpp"
#include "dir.hpp"
#include "consts.hpp"

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <unistd.h>

const int OK = 0;
const std::string COVER_NAME = "cover";
const std::string HIDDEN_NAME = "hidden";
const fuse_fill_dir_flags FILL_DIR_NULL = static_cast<fuse_fill_dir_flags>(0);

static Buffer* global_buffer;

bool startswith(const char* str, const std::string& prefix) {
    return strncmp(prefix.data(), str, prefix.size()) == 0;
}

std::pair<std::string, std::string> split_path(const char* fname) {
    auto slash_pos = strrchr(fname, '/');
    if (slash_pos) {
        return {std::string(fname, slash_pos - fname), std::string(slash_pos + 1)};
    }
    return {"", std::string(fname)};
}

std::pair<bool, unsigned int> fh_to_location(uint64_t fh) {
    --fh;
    return {fh >> 63, fh};
}

uint64_t fh_from_location(std::pair<bool, unsigned int> location) {
    return (static_cast<uint64_t>(location.first) << 63) + location.second + 1;
}

using DirFileOrError = std::variant<std::monostate, Dir, File, int>;
enum class DFOE_TYPE {
    ROOT,
    DIR,
    FILE,
    ERROR
};

constexpr DFOE_TYPE DFOE_type(const DirFileOrError& dfoe) {
    return static_cast<DFOE_TYPE>(dfoe.index());
}

std::variant<Dir, File, int> dir_or_file_from_blockid(unsigned int blk_id, bool hidden) {
    auto acc = global_buffer->block(blk_id, hidden);
    switch (acc.read()[0]) {
        case FILE_TYPE: {
            return {File(*global_buffer, std::move(acc))};
        }
        case DIR_TYPE: {
            return {Dir(*global_buffer, std::move(acc))};
        }
        default: {
            return {-ENOENT};
        }
    }
}

DirFileOrError get_for_fname(const char* fname, fuse_file_info* fi=nullptr) {
    if (fi && fi->fh) {
        auto [hidden, blk_id] = fh_to_location(fi->fh);
        return std::visit([](auto&& val){ return DirFileOrError(std::move(val)); }, dir_or_file_from_blockid(blk_id, hidden));
    }

    if (!strcmp(fname, "/") || !strcmp(fname, "")) {
        return {};
    }

    ++fname;

    bool hidden;
    if (startswith(fname, COVER_NAME)) {
        hidden = false;
        fname += COVER_NAME.size();
    }
    else if (startswith(fname, HIDDEN_NAME)) {
        hidden = true;
        fname += HIDDEN_NAME.size();
    }
    else {
        return {-ENOENT};
    }

    std::variant<Dir, File> current = Dir(*global_buffer, 0, hidden);

    if (fname[0] == '/') {
        ++fname;
    }
    secure_string part;
    while (strcmp(fname, "") and strcmp(fname, "/")) {
        auto slash_pos = strchr(fname, '/');
        if (slash_pos) {
            part.replace(0, secure_string::npos, reinterpret_cast<const unsigned char*>(fname), slash_pos - fname);
            fname = slash_pos;
            if (fname[0] == '/') {
                ++fname;
            }
        }
        else {
            part.replace(0, secure_string::npos, reinterpret_cast<const unsigned char*>(fname));
            fname += part.size();
        }
        std::cout << part << std::endl;
        if (std::holds_alternative<Dir>(current)) {
            auto& dir = std::get<Dir>(current);
            dir.debug();
            auto iter = dir.find(part);
            if (iter == dir.end()) {
                return {-ENOENT};
            }
            auto [pth, blk_id] = *iter;
            auto dir_or_file = dir_or_file_from_blockid(blk_id, hidden);
            if (std::holds_alternative<Dir>(dir_or_file)) {
                current.emplace<Dir>(std::move(std::get<Dir>(dir_or_file)));
            }
            else if (std::holds_alternative<File>(dir_or_file)) {
                current.emplace<File>(std::move(std::get<File>(dir_or_file)));
            }
            else {
                return std::get<int>(dir_or_file);
            }
        }
        else {
            return {-ENOTDIR};
        }
    }

    return std::visit([](auto&& val){ return DirFileOrError(std::move(val)); }, current);
}

int f_getattr(const char* fname, struct stat* st, fuse_file_info* fi) {
    auto f = get_for_fname(fname, fi);

    st->st_gid = getgid();
    st->st_uid = getuid();
    st->st_atim = {0, 0};
    st->st_mtim = {0, 0};
    st->st_ctim = {0, 0};

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            st->st_mode = S_IFDIR | 0755;
            st->st_nlink = 2;
            break;
        }
        case DFOE_TYPE::DIR: {
            auto& dir = std::get<Dir>(f);
            st->st_mode = S_IFDIR | 0755;
            st->st_size = dir.size();
            st->st_nlink = 2;
            break;
        }
        case DFOE_TYPE::FILE: {
            auto& file = std::get<File>(f);
            st->st_mode = S_IFREG | 0644;
            st->st_size = file.size();
            st->st_nlink = 1;
            break;
        }
    };

    return OK;
}

int f_mkdir(const char* path, mode_t /*mode*/) {
    auto [base, fname] = split_path(path);
    auto f = get_for_fname(base.c_str(), nullptr);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EPERM;
        }
        case DFOE_TYPE::DIR: {
            auto& dir = std::get<Dir>(f);
            if (dir.find(string_to_ss(fname)) != dir.end()) {
                return -EEXIST;
            }
            auto new_d = Dir::newDir(*global_buffer, dir.block_id().first);
            dir.add(string_to_ss(fname), new_d.block_id().second);
            break;
        }
        case DFOE_TYPE::FILE: {
            return -ENOTDIR;
        }
    };

    return OK;
}

int f_unlink(const char* path) {
    auto [base, fname] = split_path(path);
    auto f = get_for_fname(base.c_str(), nullptr);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EPERM;
        }
        case DFOE_TYPE::DIR: {
            auto& dir = std::get<Dir>(f);
            unsigned int blk_id;
            {
                auto iter = dir.find(string_to_ss(fname));
                if (iter == dir.end()) {
                    return -ENOENT;
                }
                blk_id = (*iter).second;
            }
            auto dir_or_file = dir_or_file_from_blockid(blk_id, dir.block_id().first);
            if (std::holds_alternative<Dir>(dir_or_file)) {
                return -EISDIR;
            }
            else if (std::holds_alternative<File>(dir_or_file)) {
                {
                    auto rem_f = std::move(std::get<File>(dir_or_file));
                    rem_f.truncate(0);
                }
                global_buffer->deallocateBlock(blk_id, dir.block_id().first);
                dir.remove(string_to_ss(fname));
                return OK;
            }
            else {
                return std::get<int>(dir_or_file);
            }
        }
        case DFOE_TYPE::FILE: {
            return -ENOTDIR;
        }
    }
    return -EIO;
}

int f_rmdir(const char* path) {
    auto [base, fname] = split_path(path);
    auto f = get_for_fname(base.c_str(), nullptr);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EPERM;
        }
        case DFOE_TYPE::DIR: {
            auto& dir = std::get<Dir>(f);
            unsigned int blk_id;
            {
                auto iter = dir.find(string_to_ss(fname));
                if (iter == dir.end()) {
                    return -ENOENT;
                }
                blk_id = (*iter).second;
            }
            auto dir_or_file = dir_or_file_from_blockid(blk_id, dir.block_id().first);
            if (std::holds_alternative<Dir>(dir_or_file)) {
                {
                    auto rem_d = std::move(std::get<Dir>(dir_or_file));
                    if (rem_d.begin() != rem_d.end()) {
                        return -ENOTEMPTY;
                    }
                }
                global_buffer->deallocateBlock(blk_id, dir.block_id().first);
                dir.remove(string_to_ss(fname));
                return OK;
            }
            else if (std::holds_alternative<File>(dir_or_file)) {
                return -ENOTDIR;
            }
            else {
                return std::get<int>(dir_or_file);
            }
        }
        case DFOE_TYPE::FILE: {
            return -ENOTDIR;
        }
    }
    return -EIO;
}

int f_rename(const char* old_path, const char* new_path, unsigned int flags) {
    switch (flags) {
        case 0:
        case RENAME_EXCHANGE:
        case RENAME_NOREPLACE:
            break;
        default:
            return -EINVAL;
    }

    auto [old_base, old_fname] = split_path(old_path);
    // Note: old_dir may be an ancestor or child of new_dir, so we need to close it and open it again
    bool old_dir_hidden;
    unsigned int old_dir_blk_id, old_blk_id;
    {
        auto old_f = get_for_fname(old_base.c_str(), nullptr);

        switch (DFOE_type(old_f)) {
            case DFOE_TYPE::ERROR: {
                return std::get<int>(old_f);
            }
            case DFOE_TYPE::ROOT: {
                return -EPERM;
            }
            case DFOE_TYPE::DIR: {
                break;
            }
            case DFOE_TYPE::FILE: {
                return -ENOTDIR;
            }
        };

        auto& old_dir = std::get<Dir>(old_f);
        std::tie(old_dir_hidden, old_dir_blk_id) = old_dir.block_id();
        {
            auto old_iter = old_dir.find(string_to_ss(old_fname));
            if (old_iter == old_dir.end()) {
                return -ENOENT;
            }
            old_blk_id = (*old_iter).second;
        }
    }

    auto [new_base, new_fname] = split_path(new_path);
    DirFileOrError new_f;

    // Need to be careful: new_dir and old_dir may be the same!
    if (new_base != old_base) {
        auto new_f_local = get_for_fname(new_base.c_str(), nullptr);

        switch (DFOE_type(new_f_local)) {
            case DFOE_TYPE::ERROR: {
                return std::get<int>(new_f_local);
            }
            case DFOE_TYPE::ROOT: {
                return -EPERM;
            }
            case DFOE_TYPE::DIR: {
                new_f.emplace<Dir>(std::move(std::get<Dir>(new_f_local)));
                break;
            }
            case DFOE_TYPE::FILE: {
                return -ENOTDIR;
            }
        };
    }

    // Reopen old_dir
    auto old_dir = Dir(*global_buffer, old_dir_blk_id, old_dir_hidden);
    auto& new_dir = std::holds_alternative<Dir>(new_f) ? std::get<Dir>(new_f) : old_dir;
    unsigned int new_blk_id = -1;
    if (new_dir.block_id().first != old_dir.block_id().first) {
        return -EINVAL;
    }

    {
        auto new_iter = new_dir.find(string_to_ss(new_fname));
        if (new_iter == new_dir.end()) {
            if (flags == RENAME_EXCHANGE) {
                return -ENOENT;
            }
        }
        else {
            new_blk_id = (*new_iter).second;
        }
    }

    if (new_blk_id != static_cast<unsigned int>(-1)) {
        if (flags == RENAME_NOREPLACE) {
            return -EEXIST;
        }
        else if (flags == 0) {
            auto dir_or_file = dir_or_file_from_blockid(new_blk_id, new_dir.block_id().first);
            if (std::holds_alternative<Dir>(dir_or_file)) {
                {
                    auto rem_d = std::move(std::get<Dir>(dir_or_file));
                    if (rem_d.begin() != rem_d.end()) {
                        return -ENOTEMPTY;
                    }
                }
                global_buffer->deallocateBlock(new_blk_id, new_dir.block_id().first);
                new_dir.remove(string_to_ss(new_fname));
            }
            else if (std::holds_alternative<File>(dir_or_file)) {
                {
                    auto rem_f = std::move(std::get<File>(dir_or_file));
                    rem_f.truncate(0);
                }
                global_buffer->deallocateBlock(new_blk_id, new_dir.block_id().first);
                new_dir.remove(string_to_ss(new_fname));
            }
            else {
                return std::get<int>(dir_or_file);
            }
            new_blk_id = -1;
        }
    }

    if (flags == RENAME_EXCHANGE) {
        old_dir.remove(string_to_ss(old_fname));
        new_dir.remove(string_to_ss(new_fname));
        new_dir.add(string_to_ss(new_fname), old_blk_id);
        old_dir.add(string_to_ss(old_fname), new_blk_id);
    }
    else {
        old_dir.remove(string_to_ss(old_fname));
        new_dir.add(string_to_ss(new_fname), old_blk_id);
    }

    return OK;
}

int f_truncate(const char* fname, off_t size, struct fuse_file_info* fi) {
    auto f = get_for_fname(fname, fi);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EISDIR;
        }
        case DFOE_TYPE::DIR: {
            return -EISDIR;
        }
        case DFOE_TYPE::FILE: {
            auto& file = std::get<File>(f);
            file.truncate(size);
            break;
        }
    }
    return OK;
}

int f_open(const char* fname, fuse_file_info* fi) {
    auto f = get_for_fname(fname, fi);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EISDIR;
        }
        case DFOE_TYPE::DIR: {
            return -EISDIR;
        }
        case DFOE_TYPE::FILE: {
            auto& file = std::get<File>(f);
            fi->fh = fh_from_location(file.block_id());
            break;
        }
    };

    return OK;
}

int f_read(const char* fname, char* buf, size_t size, off_t offset, fuse_file_info* fi) {
    auto f = get_for_fname(fname, fi);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EISDIR;
        }
        case DFOE_TYPE::DIR: {
            return -EISDIR;
        }
        case DFOE_TYPE::FILE: {
            auto& file = std::get<File>(f);
            return file.read(offset, size, reinterpret_cast<unsigned char*>(buf));
        }
    }
    return -EIO;
}

int f_write(const char* fname, const char* buf, size_t size, off_t offset, fuse_file_info* fi) {
    auto f = get_for_fname(fname, fi);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EISDIR;
        }
        case DFOE_TYPE::DIR: {
            return -EISDIR;
        }
        case DFOE_TYPE::FILE: {
            auto& file = std::get<File>(f);
            return file.write(offset, size, reinterpret_cast<const unsigned char*>(buf));
        }
    }
    return -EIO;
}

int f_statfs(const char* fname, struct statvfs* st) {
    auto f = get_for_fname(fname, nullptr);

    st->f_bavail = 0;
    st->f_bsize = LOGICAL_BLOCK_SIZE;
    st->f_favail = -1;
    st->f_ffree = -1;
    st->f_files = -1;
    st->f_flag = ST_NOATIME | ST_NODEV | ST_NODIRATIME | ST_NOEXEC | ST_NOSUID | ST_SYNCHRONOUS;
    st->f_frsize = LOGICAL_BLOCK_SIZE;
    st->f_namemax = FILE_NAME_SIZE;

    bool hidden;
    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            st->f_bfree = global_buffer->totalBlocks() - global_buffer->blocksAllocated();
            st->f_blocks = st->f_bavail = global_buffer->totalBlocks();
            return OK;
        }
        case DFOE_TYPE::DIR: {
            hidden = std::get<Dir>(f).block_id().first;
            break;
        }
        case DFOE_TYPE::FILE: {
            hidden = std::get<File>(f).block_id().first;
            break;
        }
    };
    st->f_bfree = global_buffer->blocksForAspect(hidden) - global_buffer->blocksAllocatedForAspect(hidden);
    st->f_blocks = st->f_bavail = global_buffer->blocksForAspect(hidden);
    return OK;
}

int f_release(const char* /*fname*/, fuse_file_info* /*fi*/) {
    return OK;
}

int f_opendir(const char* fname, fuse_file_info* fi) {
    auto f = get_for_fname(fname, fi);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            break;
        }
        case DFOE_TYPE::DIR: {
            auto& dir = std::get<Dir>(f);
            fi->fh = fh_from_location(dir.block_id());
            break;
        }
        case DFOE_TYPE::FILE: {
            return -ENOTDIR;
        }
    };

    return OK;
}

int f_readdir(const char* fname, void* buf, fuse_fill_dir_t filler, off_t /*offset*/, fuse_file_info* fi, fuse_readdir_flags /*flag*/) {
    auto f = get_for_fname(fname, fi);

    filler(buf, ".", NULL, 0, FILL_DIR_NULL);
    filler(buf, "..", NULL, 0, FILL_DIR_NULL);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            filler(buf, COVER_NAME.c_str(), NULL, 0, FILL_DIR_NULL);
            filler(buf, HIDDEN_NAME.c_str(), NULL, 0, FILL_DIR_NULL);
            break;
        }
        case DFOE_TYPE::DIR: {
            auto& dir = std::get<Dir>(f);
            for (auto [fn, blk_id] : dir) {
                filler(buf, reinterpret_cast<const char*>(fn.c_str()), NULL, 0, FILL_DIR_NULL);
            }
            break;
        }
        case DFOE_TYPE::FILE: {
            return -ENOTDIR;
        }
    };

    return OK;
}

int f_releasedir(const char* /*fname*/, fuse_file_info* /*fi*/) {
    return OK;
}

void* f_init(fuse_conn_info* /*conn*/, fuse_config* /*cfg*/) {
    return NULL;
}

void f_destroy(void*) {
}

int f_access(const char* fname, int flags) {
    auto f = get_for_fname(fname, nullptr);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            break;
        }
        case DFOE_TYPE::DIR: {
            break;
        }
        case DFOE_TYPE::FILE: {
            if (flags & X_OK) {
                return -EACCES;
            }
            break;
        }
    };

    return OK;
}

int f_create(const char* path, mode_t /*mode*/, fuse_file_info* fi) {
    auto [base, fname] = split_path(path);
    auto f = get_for_fname(base.c_str(), nullptr);

    switch (DFOE_type(f)) {
        case DFOE_TYPE::ERROR: {
            return std::get<int>(f);
        }
        case DFOE_TYPE::ROOT: {
            return -EPERM;
        }
        case DFOE_TYPE::DIR: {
            auto& dir = std::get<Dir>(f);
            if (dir.find(string_to_ss(fname)) != dir.end()) {
                return -EEXIST;
            }
            auto new_f = File::newFile(*global_buffer, dir.block_id().first);
            fi->fh = fh_from_location(new_f.block_id());
            dir.add(string_to_ss(fname), new_f.block_id().second);
            break;
        }
        case DFOE_TYPE::FILE: {
            return -ENOTDIR;
        }
    };

    return OK;
}

int run_fuse(Buffer& buf, const std::string& mount_point) {
    global_buffer = &buf;

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

    struct fuse_operations ops = {
        .getattr = f_getattr,
        .mkdir = f_mkdir,
        .unlink = f_unlink,
        .rmdir = f_rmdir,
        .rename = f_rename,
        .truncate = f_truncate,
        .open = f_open,
        .read = f_read,
        .write = f_write,
        .statfs = f_statfs,
        .release = f_release,
        .opendir = f_opendir,
        .readdir = f_readdir,
        .releasedir = f_releasedir,
        .init = f_init,
        .destroy = f_destroy,
        .access = f_access,
        .create = f_create
    };

#pragma GCC diagnostic pop

    std::vector<std::string> args = {"fuse", "-f", mount_point, "-d"};
//         if self.debug:
//             args.append("-d")
    std::vector<char*> char_args;
    for (auto& arg : args) {
        char_args.push_back(const_cast<char*>(arg.c_str()));
    }
    return fuse_main(char_args.size(), &char_args[0], &ops, NULL);
}
