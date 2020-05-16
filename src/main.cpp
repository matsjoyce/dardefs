#include <iostream>

#include "docopt/docopt.h"

#include "disk.hpp"
#include "buffer.hpp"
#include "consts.hpp"
#include "file.hpp"
#include "dir.hpp"
#include "fuse_interface.hpp"


static const char USAGE[] =
R"(fs

    Usage:
        fs mount <fname> <path> [--debug] [--cache-size=<cache-size>] [--no-hidden]
        fs init <fname> <numBlocks> [--debug] [--cache-size=<cache-size>] [--no-hidden]
        fs (-h | --help)
        fs --version

    Options:
        -h --help                        Show this screen.
        --version                        Show version.
        -c, --cache-size=<cache-size>    Size of file system cache in blocks [default: 1024].
)";


int main(int argc, const char** argv) {
    auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, "fs 0.1");

    if (args["init"].asBool()) {
        std::ofstream f(args["<fname>"].asString(), std::ios::out | std::ios::binary | std::ios::trunc);
        std::ifstream urandom("/dev/urandom");
        auto amnt = args["<numBlocks>"].asLong() * PHYSICAL_BLOCK_SIZE;
        char buf[4096];
        while (amnt) {
            urandom.read(buf, std::min(4096l, amnt));
            auto num = urandom.gcount();
            amnt -= num;
            f.write(buf, num);
        }
        f.close();
        urandom.close();
    }

    auto hidden_key = "\xc7n\xbdI][\xf7\x85\x17\xad\x92\xba\xee\n\xe3V"_ss;
    if (args["--no-hidden"].asBool()) {
        std::ifstream urandom("/dev/urandom");
        urandom.read(reinterpret_cast<char*>(hidden_key.data()), KEY_SIZE);
    }

    auto disk = Disk(args["<fname>"].asString(), "6/\x11L\x18,\xc2zx\x03\xf6\x8e\xae\xa3\t\xc6"_ss, hidden_key);
    auto buffer = Buffer(disk, args["--cache-size"].asLong(), args["init"].asBool(), !args["init"].asBool(), args["--debug"].asBool(), args["--no-hidden"].asBool());

    if (args["mount"].asBool()) {
        auto ret = run_fuse(buffer, args["<path>"].asString());
        buffer.flush();
        return ret;
    }
    else if (args["init"].asBool()) {
        {
            auto cover_root = Dir::newDir(buffer, false);
        }
        {
            auto hidden_root = Dir::newDir(buffer, true);
        }
        buffer.flush();
    }
    return 0;
}
