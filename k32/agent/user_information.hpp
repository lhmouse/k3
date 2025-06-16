// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_USER_INFORMATION_
#define K32_COMMON_USER_INFORMATION_

#include "../fwd.hpp"
#include <poseidon/socket/ipv6_address.hpp>
namespace k32::agent {

struct User_Information
  {
    uint64_t uid = 0;
    phcow_string username;
    ::poseidon::IPv6_Address remote_address;
    system_time time_created;
    system_time time_signed_in;
    system_time time_signed_out;

#ifdef K32_FRIENDS_FC7DDE3B_4A8E_11F0_BB68_5254005015D2_
    User_Information() noexcept = default;
#endif
    User_Information(const User_Information&) = default;
    User_Information(User_Information&&) = default;
    User_Information& operator=(const User_Information&) & = default;
    User_Information& operator=(User_Information&&) & = default;
    ~User_Information();
  };

}  // namespace k32::agent
#endif
