// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
#include "role.hpp"
namespace k32::logic {

Role::
~Role()
  {
  }

void
Role::
make_avatar(::taxon::V_object& avatar)
  {
    avatar.try_emplace(&"avatar_field", 42);
  }

void
Role::
make_profile(::taxon::V_object& profile)
  {
    profile.try_emplace(&"profile", &"meow");
  }

void
Role::
hydrate(const ::taxon::V_object& db_data)
  {
    POSEIDON_LOG_FATAL(("HYDRATE: $1: $2"), *this, db_data);
  }

void
Role::
hibernate(::taxon::V_object& db_data)
  {
    db_data.try_emplace(&"db_data", true);
    POSEIDON_LOG_FATAL(("HIBERNATE: $1: $2"), *this, db_data);
  }

}  // namespace k32::logic
