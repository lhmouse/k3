// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_MONITOR_STATIC_ROLE_SERVICE_
#define K32_MONITOR_STATIC_ROLE_SERVICE_

#include "../../fwd.hpp"
#include "../../common/data/role_information.hpp"
namespace k32::monitor {

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

    // Gets properties of a role.
    const Role_Information&
    find_role(int64_t roid) const noexcept;

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);
  };

}  // namespace k32::monitor
#endif
