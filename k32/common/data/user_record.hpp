// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_DATA_USER_RECORD_
#define K32_COMMON_DATA_USER_RECORD_

#include "../../fwd.hpp"
namespace k32 {

struct User_Record
  {
    phcow_string username;
    ::poseidon::IPv6_Address login_address;
    system_time creation_time;
    system_time login_time;
    system_time logout_time;
    system_time banned_until;

#ifdef K32_FRIENDS_6A8BAC8C_B8F6_4BDA_BD7F_B90D5BF07B81_
    User_Record() noexcept = default;
#endif
    User_Record(const User_Record&) = default;
    User_Record(User_Record&&) = default;
    User_Record& operator=(const User_Record&) & = default;
    User_Record& operator=(User_Record&&) & = default;
    ~User_Record();

    static const User_Record null;
    explicit operator bool() const noexcept { return this->username.length() != 0;  }
  };

}  // namespace k32
#endif
