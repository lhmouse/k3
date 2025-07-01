// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_LOGIC_HYDRATED_ROLE_
#define K32_LOGIC_HYDRATED_ROLE_

#include "../../fwd.hpp"
#include "role_basic_information.hpp"
namespace k32::logic {

class Role
  :
    public Role_Basic_Information
  {
  private:
    // TODO

  public:
#ifdef K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
    Role() noexcept = default;
#endif
    Role(const Role&) = delete;
    Role& operator=(const Role&) & = delete;
    ~Role();

    // Load role cultivation data from the database. This is an internal function
    // called by the role service, after basic information has been filled in.
    void
    hydrate(const ::taxon::V_object& db_data);

    // Create an avatar of this role. This is what others can see outdoors.
    void
    make_avatar(::taxon::V_object& avatar);

    // Create a profile snapshot of this role. This is what others can see on
    // the profile page.
    void
    make_profile(::taxon::V_object& profile);

    // Serialize role cultivation data which can then be stored into the database.
    void
    hibernate(::taxon::V_object& db_data);

    // This function is called right after a role has been loaded from Redis.
    void
    on_login();

    // This function is called just before a role is unloaded.
    void
    on_logout();

    // This function is called right after a role has been loaded from Redis, or
    // right after a client has reconnected.
    void
    on_connect();

    // This function is called right after a client has disconnected, or just
    // before a role is unloaded.
    void
    on_disconnect();

    // This function is called for every clock tick.
    void
    on_every_second();
  };

inline
tinyfmt&
operator<<(tinyfmt& fmt, const Role& role)
  {
    return format(fmt, "role `$1` (`$2`)", role.roid(), role.nickname());
  }

}  // namespace k32::logic
#endif
