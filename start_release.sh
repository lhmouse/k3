#!/bin/bash -e

export LD_LIBRARY_PATH=$(realpath -e build_release)
poseidon ./etc
