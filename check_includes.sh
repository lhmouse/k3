#!/bin/bash -e

export CPPFLAGS="-D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Ibuild_debug -mavx"
export CXX=${CXX:-"g++"}

find ${PWD}/"k32" -name "*.[hc]pp"  \
  | xargs -n 1 -P $(nproc) --  \
    sh -xec '${CXX} ${CPPFLAGS} -x c++ -std=c++17 $1 -S -o /dev/null' 0
