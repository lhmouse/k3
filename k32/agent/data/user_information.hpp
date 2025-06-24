// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_DATA_USER_INFORMATION_
#define K32_AGENT_DATA_USER_INFORMATION_

#include "../../fwd.hpp"
namespace k32::agent {

struct User_Information
  {
    phcow_string username;
    ::poseidon::IPv6_Address login_address;
    system_time creation_time;
    system_time login_time;
    system_time logout_time;

#ifdef K32_FRIENDS_FC7DDE3B_4A8E_11F0_BB68_5254005015D2_
    User_Information() noexcept = default;
#endif
    User_Information(const User_Information&) = default;
    User_Information(User_Information&&) = default;
    User_Information& operator=(const User_Information&) & = default;
    User_Information& operator=(User_Information&&) & = default;
    ~User_Information();

    explicit operator bool() const noexcept { return !this->username.empty();  }
  };

extern const User_Information& null_user_information;

}  // namespace k32::agent
#endif
