#!/bin/bash -e

meson compile -Cbuild_debug

export LD_LIBRARY_PATH=$(realpath -e build_debug)
valgrind -- poseidon ./etc
