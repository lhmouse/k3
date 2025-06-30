// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_LOGIC_HYDRATED_ROLE_
#define K32_LOGIC_HYDRATED_ROLE_

#include "../../fwd.hpp"
namespace k32::logic {

class Role
  {
  private:
    // basic information
    ::poseidon::UUID m_agent_service_uuid;
    int64_t m_roid = 0;
    cow_string m_nickname;
    phcow_string m_username;

  public:
#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    friend class Role_Service;
    Role() noexcept = default;
#endif
    Role(const Role&) = delete;
    Role& operator=(const Role&) & = delete;
    ~Role();

    // Get basic information. These fields are read-only.
    const ::poseidon::UUID&
    agent_service_uuid() const noexcept
      { return this->m_agent_service_uuid;  }

    int64_t
    roid() const noexcept
      { return this->m_roid;  }

    const cow_string&
    username() const noexcept
      { return this->m_username;  }

    const cow_string&
    nickname() const noexcept
      { return this->m_nickname;  }

    // Create an avatar of this role. This is what others can see outdoors.
    void
    make_avatar(::taxon::V_object& avatar);

    // Create a profile snapshot of this role. This is what others can see on
    // the profile page.
    void
    make_profile(::taxon::V_object& profile);

#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    // Load role cultivation data from the database. This is an internal function
    // called by the role service, after basic information has been filled in.
    void
    hydrate(const ::taxon::V_object& db_data);
#endif

    // Serialize role cultivation data which can then be stored into the database.
    void
    hibernate(::taxon::V_object& db_data);
  };

inline
tinyfmt&
operator<<(tinyfmt& fmt, const Role& role)
  {
    return format(fmt, "role `$1` (`$2`)", role.roid(), role.nickname());
  }

}  // namespace k32::logic
#endif
