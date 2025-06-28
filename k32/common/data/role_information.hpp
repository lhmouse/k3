// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_DATA_ROLE_INFORMATION_
#define K32_COMMON_DATA_ROLE_INFORMATION_

#include "../../fwd.hpp"
namespace k32 {

struct Role_Information
  {
    int64_t roid = 0;
    phcow_string username;
    cow_string nickname;
    system_time update_time;
    cow_string avatar;
    cow_string profile;
    cow_string whole;

    cow_string home_host;
    cow_string home_db;

#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    Role_Information() noexcept = default;
#endif
    Role_Information(const Role_Information&) = default;
    Role_Information(Role_Information&&) = default;
    Role_Information& operator=(const Role_Information&) & = default;
    Role_Information& operator=(Role_Information&&) & = default;
    ~Role_Information();

    static const Role_Information& null;
    explicit operator bool() const noexcept { return this->roid == 0;  }
  };

}  // namespace k32
#endif
