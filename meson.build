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

project('k3',
        'cpp',
        version: '0.0.internal',
        license: 'BSD-3-Clause',
        meson_version: '>= 1.3.0')

#===========================================================
# Configuration
#===========================================================
cxx = meson.get_compiler('cpp')
cxx_is_i386 = cxx.has_define('__i386__')
cxx_is_amd64 = cxx.has_define('__amd64__')

add_project_arguments(
    '-Wall', '-Wextra', '-Winvalid-pch', '-std=c++17', '-D_FILE_OFFSET_BITS=64',
    '-D_POSIX_C_SOURCE=200809', '-D_DEFAULT_SOURCE', '-D_DARWIN_C_SOURCE',
    cxx.get_supported_arguments(
      '-fno-elide-constructors', '-fvisibility-inlines-hidden', '-fstrict-aliasing',
      '-fno-stack-protector', '-fstrict-overflow', '-fmerge-all-constants',
      '-frounding-math', '-fno-math-errno', '-fno-ident', '-fno-trapping-math',
      '-fno-devirtualize-speculatively', '-fno-semantic-interposition', '-fno-plt',
      '-fno-omit-frame-pointer', '-momit-leaf-frame-pointer', '-masm=intel',
      '-mno-outline-atomics', '-Wno-option-ignored',
      '-Werror=conversion', '-Werror=sign-compare', '-Werror=sign-conversion',
      '-Werror=write-strings', '-Werror=return-type', '-Werror=double-promotion',
      '-Werror=non-virtual-dtor', '-Wzero-as-null-pointer-constant',
      '-Wmissing-declarations', '-Wmissing-field-initializers', '-Wshadow',
      '-Wsuggest-attribute=noreturn', '-Wswitch-enum', '-Wunused-function',
      '-Wunused-label', '-Wunused-local-typedefs', '-Wunused-but-set-variable',
      '-Wunused-but-set-parameter', '-Wno-array-bounds', '-Wno-stringop-overflow',
      '-Wno-redundant-move', '-Wno-unqualified-std-cast-call', '-Wno-string-plus-int',
      '-Wno-bitwise-instead-of-logical', '-Wno-missing-braces'),
    language: 'cpp')

if get_option('enable-avx') and (cxx_is_i386 or cxx_is_amd64)
  add_project_arguments('-mavx', language: 'cpp')
endif

if get_option('enable-debug-checks')
  add_project_arguments('-D_GLIBCXX_DEBUG', '-D_LIBCPP_DEBUG', language: 'cpp')
endif

add_project_dependencies(
    dependency('threads'), dependency('openssl'), dependency('libunwind'),
    dependency('asteria'), dependency('poseidon'), dependency('taxon'),
    language: 'cpp')

#===========================================================
# Modules
#===========================================================
k3_common = shared_library('k3_common',
    cpp_pch: 'k3/xprecompiled.hpp',
    sources: [ 'k3/common/service.cpp' ],
    install: true)

shared_module('k3_agent',
    cpp_pch: 'k3/xprecompiled.hpp',
    link_with: k3_common,
    sources: [ 'k3/agent/globals.cpp' ],
    install: true)

shared_module('k3_logic',
    cpp_pch: 'k3/xprecompiled.hpp',
    link_with: k3_common,
    sources: [ 'k3/logic/globals.cpp' ],
    install: true)
