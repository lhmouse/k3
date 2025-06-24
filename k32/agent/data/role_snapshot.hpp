// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_DATA_ROLE_SNAPSHOT_
#define K32_AGENT_DATA_ROLE_SNAPSHOT_

#include "../../fwd.hpp"
namespace k32::agent {

struct Role_Snapshot
  {
    int64_t uid = 0;
    phcow_string username;
    phcow_string nickname;
    cow_string avatar;
    cow_string profile;
    cow_string whole;

#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    Role_Snapshot() noexcept = default;
#endif
    Role_Snapshot(const Role_Snapshot&) = default;
    Role_Snapshot(Role_Snapshot&&) = default;
    Role_Snapshot& operator=(const Role_Snapshot&) & = default;
    Role_Snapshot& operator=(Role_Snapshot&&) & = default;
    ~Role_Snapshot();

    explicit operator bool() const noexcept { return this->uid == 0;  }
  };

extern const Role_Snapshot null_role_snapshot;

}  // namespace k32::agent
#endif
