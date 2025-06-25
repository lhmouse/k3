// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_DATA_ROLE_INFORMATION_
#define K32_AGENT_DATA_ROLE_INFORMATION_

#include "../../fwd.hpp"
namespace k32::agent {

struct Role_Information
  {
    int64_t roid = 0;
    phcow_string username;
    phcow_string nickname;
    system_time update_time;
    cow_string avatar;
    cow_string profile;
    cow_string whole;

#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    Role_Information() noexcept = default;
#endif
    Role_Information(const Role_Information&) = default;
    Role_Information(Role_Information&&) = default;
    Role_Information& operator=(const Role_Information&) & = default;
    Role_Information& operator=(Role_Information&&) & = default;
    ~Role_Information();

    explicit operator bool() const noexcept { return this->roid == 0;  }
  };

extern const Role_Information& null_role_information;

}  // namespace k32::agent
#endif
