// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_FWD_
#define K32_FWD_

#include <asteria/value.hpp>
#include <asteria/utils.hpp>
#include <poseidon/base/uuid.hpp>
#include <poseidon/socket/ipv6_address.hpp>
#include <poseidon/fiber/abstract_fiber.hpp>
#include <poseidon/utils.hpp>
#include <taxon.hpp>
#include <array>
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
namespace k32 {
namespace noadl = k32;

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
using ::rocket::cow_bstring;
using ::rocket::cow_u16string;
using ::rocket::cow_u32string;
using ::rocket::phcow_string;
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
using ::rocket::shared_function;

POSEIDON_USING cow_bivector = cow_vector<pair<Ts...>>;
POSEIDON_USING cow_dictionary = cow_hashmap<phcow_string, Ts..., phcow_string::hash>;
POSEIDON_USING cow_uint32_dictionary = cow_hashmap<uint32_t, Ts..., ::std::hash<uint32_t>>;
POSEIDON_USING cow_uint64_dictionary = cow_hashmap<uint64_t, Ts..., ::std::hash<uint64_t>>;
POSEIDON_USING cow_uuid_dictionary = cow_hashmap<::poseidon::UUID, Ts..., ::poseidon::UUID::hash>;

using ::std::static_pointer_cast;
using ::std::dynamic_pointer_cast;
using ::std::const_pointer_cast;
using ::std::chrono::duration_cast;
using ::std::chrono::time_point_cast;

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

using ::asteria::format;
using ::asteria::sformat;

using ::poseidon::chars_view;
using ::poseidon::opt;
using ::poseidon::vfn;
using ::poseidon::uniptr;
using ::poseidon::shptr;
using ::poseidon::wkptr;
using ::poseidon::new_uni;
using ::poseidon::new_sh;

template<typename xSelf, typename xOther, typename... xArgs>
ROCKET_ALWAYS_INLINE
shared_function<void (xArgs...)>
bindw(const shptr<xSelf>& obj, vfn<const shptr<xOther>&, xArgs...>* pfunc)
  {
    return shared_function<void (xArgs...)>(
        [w = wkptr<xSelf>(obj), pfunc] (xArgs&&... args) {
          if(const auto obj2 = w.lock())
            ::std::invoke(*pfunc, obj2, forward<xArgs>(args)...);
        });
  }

// Private WebSocket status codes to clients
enum User_WS_Status : uint16_t
  {
    user_ws_status_authentication_failure    = 4301,
    user_ws_status_login_conflict            = 4302,
    user_ws_status_unknown_opcode            = 4303,
    user_ws_status_message_rate_limit        = 4304,
    user_ws_status_ping_timeout              = 4305,
    user_ws_status_ban                       = 4306,
  };

}  // namespace k32
#endif
