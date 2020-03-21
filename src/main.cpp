#include <iostream>

#include "docopt/docopt.h"

#include "disk.hpp"
#include "buffer.hpp"
#include "consts.hpp"
#include "file.hpp"
#include "dir.hpp"
// #include "utilities.hpp"
#include "fuse_interface.hpp"


static const char USAGE[] =
R"(fs

    Usage:
        fs mount <fname> <path> [<accesscontroller>] [--debug] [--fuse-debug]
        fs init <fname> <numBlocks>
        fs (-h | --help)
        fs --version

    Options:
        -h --help     Show this screen.
        --version     Show version.
)";


int main(int argc, const char** argv) {
    auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, "fs 0.1");

    if (args["init"].asBool()) {
        std::ofstream f(args["<fname>"].asString(), std::ios::out | std::ios::binary | std::ios::trunc);
        std::ifstream urandom("/dev/urandom");
        auto amnt = args["<numBlocks>"].asLong() * (PHYSICAL_BLOCK_SIZE + BLOCK_MAPPING_ENTRY_SIZE);
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

    auto disk = Disk(args["<fname>"].asString(), "6/\x11L\x18,\xc2zx\x03\xf6\x8e\xae\xa3\t\xc6"_ss, "\xc7n\xbdI][\xf7\x85\x17\xad\x92\xba\xee\n\xe3V"_ss);
    auto buffer = Buffer(disk);

    if (args["mount"].asBool()) {
        return run_fuse(buffer, args["<path>"].asString());
    }
    else if (args["init"].asBool()) {
        {
            auto cover_root = Dir::newDir(buffer, false);
            auto hidden_root = Dir::newDir(buffer, true);
        }
        buffer.flush();
    }

//     std::cout << "Disk has " << disk.numberOfBlocks() << " blocks" << std::endl;
//     {
//         auto f = BlockFile::newFile(buffer, BFType::FILE, false);
//         std::cout << "BLKID " << f.block_id().second << std::endl;
//         std::cout << "TYPE " << static_cast<unsigned int>(f.type()) << std::endl;
//         std::cout << "NoB " << f.numberOfBlocks() << std::endl;
//         f.addBlock();
//         f.addBlock();
//         for (auto fb : f) {
//             std::cout << fb.size << "@" << fb.offset << std::endl;
//             fb.acc.writable().replace(fb.offset, 5, "hello"_ss);
//         }
//     }
//
//     {
//         auto f = BlockFile(buffer, 0, false);
//         std::cout << "TYPE " << static_cast<unsigned int>(f.type()) << std::endl;
//         std::cout << "NoB " << f.numberOfBlocks() << std::endl;
//         std::cout << "NoBy " << f.numberOfBytes() << std::endl;
//         for (auto fb : f) {
//             std::cout << fb.size << std::endl;
//             std::cout << fb.acc.read().substr(fb.offset, 5) << std::endl;
//         }
//         f.truncate();
//     }
//     {
//         auto f = File::newFile(buffer, false);
//         std::cout << f.size() << std::endl;
//         auto data = secure_string(5000, 'A');
//         f.write(0, data.size(), &data[0]);
//         std::cout << f.size() << std::endl;
//     }
//     {
//         auto f = File(buffer, 0, false);
//         std::cout << f.size() << std::endl;
//         auto data = secure_string(5001, 'X');
//         f.read(0, data.size(), &data[0]);
//         std::cout << data << std::endl;
//     }

//     {
//         auto d = Dir::newDir(buffer, false);
//         std::cout << d.size() << std::endl;
//         std::vector<secure_string> items;
//         for (auto i = 0; i < 500; ++i) {
//             d.add(string_to_ss(std::to_string(i)), i);
//             items.push_back(string_to_ss(std::to_string(i)));
//         }
//         d.debug();
// //         std::sort(items.begin(), items.end());
// //         std::reverse(items.begin(), items.end());
//         std::random_shuffle(items.begin(), items.end());
//         auto count = 0u;
//         for (auto [fname, val] : d) {
// //             std::cout << fname << " " << val << std::endl;
//             ++count;
//         }
//         std::cout << "N = " << count << std::endl;
// //         {
// //             auto iter = d.find("150"_ss);
// //             std::cout << (iter == d.end()) << std::endl;
// //             if (iter != d.end()) {
// //                 auto [fname, val] = *iter;
// //                 std::cout << fname << " " << val << std::endl;
// //                 ++iter;
// //                 auto [fname2, val2] = *iter;
// //                 std::cout << fname2 << " " << val2 << std::endl;
// //             }
// //         }
//         for (auto i : items) {
//             std::cout << "REM " << i << std::endl;
//             auto val = d.remove(i);
//             ensure(string_to_ss(std::to_string(val)) == i, "main") << val << " != " << i;
//             d.debug();
// //             if (val == 17) {
// //                 break;
// //             }
//         }
// //         d.debug();
//     }

    return 0;
}
