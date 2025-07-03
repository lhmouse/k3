// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_LOGIC_STATIC_ROLE_SERVICE_
#define K32_LOGIC_STATIC_ROLE_SERVICE_

#include "../../fwd.hpp"
#include "../hydrated/role.hpp"
namespace k32::logic {

class Role_Service
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    Role_Service();

  public:
    Role_Service(const Role_Service&) = delete;
    Role_Service& operator=(const Role_Service&) & = delete;
    ~Role_Service();

    // Gets a hydrated role on this service.
    shptr<Role>
    find_online_role_opt(int64_t roid) const noexcept;

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);
  };

}  // namespace k32::logic
#endif
