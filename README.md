# dardefs

A filesystem supporting deniable encryption. This filesystem defends against differential attacks which compromise similar filesystems such as VeraCrypt or stegfs. For a thorougher breakdown of these attacks and the inner workings of this filesystem, please read the [report](https://github.com/matsjoyce/dardefs/blob/master/report.pdf).

## Building

This project requires CMake to build, as well as Crypto++, libFUSE 3 and docopt. Once these dependencies are installed, run:

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

To create an instance of this filesystem, use `fs init <fname> <numBlocks>` where `<fname>` is the file or block device to create the filesystem on. `<numBlocks>` specifies the size of the filesystem in 4K blocks.

To mount the filesystem, run `fs mount <fname> <path>` where `<fname>` is the file containing the filesystem, and `<path>` is an empty directory to use as the mount point.
