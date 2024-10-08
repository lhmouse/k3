## Copyright (C) 2024 by LH_Mouse <lh_mouse@126.com>
##
## Permission to use, copy, modify, and/or distribute this
## software for any purpose with or without fee is hereby granted.
##
## THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
## WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
## WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
## THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
## CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
## LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
## NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
## CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

project('k3', 'cpp', version: '0.0.internal')

#===========================================================
# Configuration
#===========================================================
if get_option('enable-avx')
  add_project_arguments('-mavx', language: [ 'c', 'cpp' ])
endif

if get_option('enable-debug-checks')
  add_project_arguments('-D_GLIBCXX_DEBUG', '-D_LIBCPP_DEBUG', language: 'cpp')
endif

add_project_arguments(
    '-Wall', '-Wextra', '-masm=intel', '-msse4.2', '-ffast-math',
    '-D_FILE_OFFSET_BITS=64', '-D_POSIX_C_SOURCE=200809', '-D_DEFAULT_SOURCE',
    '-fstrict-aliasing', '-fstrict-overflow', '-fmerge-all-constants',
    '-fno-devirtualize-speculatively', '-fno-semantic-interposition',
    '-Werror=conversion', '-Werror=sign-compare', '-Werror=sign-conversion',
    '-Werror=write-strings', '-Werror=return-type', '-Werror=double-promotion',
    '-Wmissing-declarations', '-Wmissing-field-initializers', '-Wshadow',
    '-Wsuggest-attribute=noreturn', '-Wswitch-enum', '-Wunused-function',
    '-Wunused-label', '-Wunused-local-typedefs', '-Wunused-but-set-variable',
    '-Wunused-but-set-parameter', '-Wno-stringop-overflow', '-Wno-array-bounds',
    language: [ 'c', 'cpp' ])

add_project_arguments(
    '-std=c++17', '-fno-elide-constructors', '-fvisibility-inlines-hidden',
    '-fnothrow-opt', '-fno-enforce-eh-specs', '-Wzero-as-null-pointer-constant',
    '-Wno-redundant-move', '-Werror=non-virtual-dtor',
    language: 'cpp')

add_project_dependencies(
    dependency('threads'),
    dependency('asteria'),
    dependency('poseidon'),
    dependency('taxon'),
    language: 'cpp')

#===========================================================
# Modules
#===========================================================
k3_common = shared_library('k3_common',
    cpp_pch: 'k3/xprecompiled.hpp',
    sources: [
      'k3/common/service.cpp',
    ])

shared_module('k3_agent',
    cpp_pch: 'k3/xprecompiled.hpp',
    link_with: k3_common,
    sources: [
      'k3/agent/globals.cpp',
    ])

shared_module('k3_logic',
    cpp_pch: 'k3/xprecompiled.hpp',
    link_with: k3_common,
    sources: [
      'k3/logic/globals.cpp',
    ])
