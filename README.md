# ozo

[![Build Status](https://travis-ci.org/YandexMail/ozo.svg?branch=master)](https://travis-ci.org/YandexMail/ozo)
[![codecov](https://codecov.io/gh/YandexMail/ozo/branch/master/graph/badge.svg)](https://codecov.io/gh/YandexMail/ozo)

## Dependencies

These things are needed:

* **CMake** is used as build system
* **GCC** or **Clang** C++ compiler with C++17 support (tested with GCC 7.0, Clang 5.0 and Apple LLVM version 9.0.0)
* **Boost** >= 1.66
* **libpq** >= 9.3

If you want to run integration tests and/or build inside Docker container:
* **Docker** >= 1.13.0
* **Docker Compose** >= 1.10.0

## Build

The library is header-only, but if you want to build and run unit-tests you can do it as listed below.

### Build and run tests on custom environment

First of all you need to satsfy requirements listed above. You can run tests using these commands.

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
ctest -V
```

Or use [build.sh](scripts/build.sh) which accepts folowing commands:

```bash
scripts/build.sh help
```

prints help.

```bash
scripts/build.sh <compiler> <target>
```

build and run tests with specified **compiler** and **target**, the **compiler** parameter can be:

* **gcc** - for build with gcc,
* **clang** - for build with clang.

The **target** parameter depends on **compiler**.
For **gcc**:

* **debug** - for debug build and tests
* **release** - for release build and tests
* **coverage** - for code coverage calculation

For **clang**:

* **debug** - for debug build and tests
* **release** - for release build and tests
* **asan** - for [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) launch
* **ubsan** - for [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) launch
* **tsan** - for [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) launch

```bash
scripts/build.sh all
```

build all possible configuration.

### Build and run tests on MacOS 10.X

For MacOS the best way to satisfy minimum requirements is [brew](https://brew.sh/)

```bash
brew install cmake boost libpq postresql
```

### Build and run tests within Docker

To build code and run tests inside docker container:

```bash
scripts/build.sh docker <compiler> <target>
```

### Test against a local postgres

You can use `scripts/build.sh` but add `pg` first:

```bash
scripts/build.sh pg <compiler> <target>
```

or if you want build code in docker:

```bash
scripts/build.sh pg docker <compiler> <target>
```

This will attempt to launch postgres:alpine from your Docker registry.
Or you can point ozo tests to a postgres of your choosing by setting these environment variables prior to building:

```bash
export OZO_BUILD_PG_TESTS=ON
export OZO_PG_TEST_CONNINFO='your conninfo (connection string)'

scripts/build.sh gcc debug
```
