#!/bin/bash
set -x
make -j $(nproc) -C test all
cd test/build && sudo ./run_tests