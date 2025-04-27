// This file is part of k3.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K3VR5NZE_FWD_
#define K3VR5NZE_FWD_

#include <asteria/utils.hpp>
#include <poseidon/utils.hpp>
#include <poseidon/base/uuid.hpp>
#include <taxon.hpp>
namespace k3 {
namespace noadl = k3;

using ::std::initializer_list;
using ::std::nullptr_t;
using ::std::max_align_t;
using ::std::int8_t;
using ::std::uint8_t;
using ::std::int16_t;
using ::std::uint16_t;
using ::std::int32_t;
using ::std::uint32_t;
using ::std::int64_t;
using ::std::uint64_t;
using ::std::intptr_t;
using ::std::uintptr_t;
using ::std::intmax_t;
using ::std::uintmax_t;
using ::std::ptrdiff_t;
using ::std::size_t;
using ::std::wint_t;
using ::std::exception;
using ::std::exception_ptr;
using ::std::type_info;
using ::std::pair;
using ::std::array;
using ::std::vector;
using ::std::deque;
using ::std::list;
using ::std::forward_list;
using ::std::map;
using ::std::multimap;
using ::std::set;
using ::std::multiset;
using ::std::unordered_map;
using ::std::unordered_multimap;
using ::std::unordered_set;
using ::std::unordered_multiset;

using namespace ::std::literals;
using ::std::chrono::duration;
using ::std::chrono::time_point;
using ::std::chrono::system_clock;
using system_time = system_clock::time_point;
using ::std::chrono::steady_clock;
using steady_time = steady_clock::time_point;

using nanoseconds = ::std::chrono::duration<int64_t, ::std::nano>;
using milliseconds = ::std::chrono::duration<int64_t, ::std::milli>;
using seconds = ::std::chrono::duration<int, ::std::ratio<1>>;
using minutes = ::std::chrono::duration<int, ::std::ratio<60>>;
using hours = ::std::chrono::duration<int, ::std::ratio<3600>>;
using days = ::std::chrono::duration<int, ::std::ratio<86400>>;
using weeks = ::std::chrono::duration<int, ::std::ratio<604800>>;

using ::std::static_pointer_cast;
using ::std::dynamic_pointer_cast;
using ::std::const_pointer_cast;
using ::std::chrono::duration_cast;
using ::std::chrono::time_point_cast;

using ::rocket::atomic;
using ::rocket::atomic_relaxed;
using ::rocket::atomic_acq_rel;
using ::rocket::atomic_seq_cst;
using plain_mutex = ::rocket::mutex;
using ::rocket::recursive_mutex;
using ::rocket::condition_variable;
using ::rocket::cow_vector;
using ::rocket::cow_hashmap;
using ::rocket::static_vector;
using ::rocket::cow_string;
using phsh_string = ::rocket::prehashed_string;
using ::rocket::cow_bstring;
using ::rocket::cow_u16string;
using ::rocket::cow_u32string;
using ::rocket::linear_buffer;
using ::rocket::tinybuf;
using ::rocket::tinybuf_str;
using ::rocket::tinybuf_ln;
using ::rocket::tinyfmt;
using ::rocket::tinyfmt_str;
using ::rocket::tinyfmt_ln;
using ::rocket::unique_posix_fd;
using ::rocket::unique_posix_file;
using ::rocket::unique_posix_dir;

using ::rocket::begin;
using ::rocket::end;
using ::rocket::swap;
using ::rocket::xswap;
using ::rocket::move;
using ::rocket::forward;
using ::rocket::forward_as_tuple;
using ::rocket::exchange;
using ::rocket::size;
using ::rocket::ssize;
using ::rocket::static_pointer_cast;
using ::rocket::dynamic_pointer_cast;
using ::rocket::const_pointer_cast;
using ::rocket::make_unique_handle;
using ::rocket::min;
using ::rocket::max;
using ::rocket::clamp;
using ::rocket::clamp_cast;
using ::rocket::is_any_of;
using ::rocket::is_none_of;
using ::rocket::all_of;
using ::rocket::any_of;
using ::rocket::none_of;
using ::rocket::nullopt;

using ::poseidon::chars_view;
using ::poseidon::cow_bivector;
using ::poseidon::cow_stringR;
using phsh_stringR = const ::rocket::prehashed_string&;
using ::poseidon::shptrR;
using ::poseidon::opt;
using ::poseidon::uniptr;
using ::poseidon::new_uni;
using ::poseidon::shptr;
using ::poseidon::wkptr;
using ::poseidon::new_sh;

}  // namespace k3
#endif
