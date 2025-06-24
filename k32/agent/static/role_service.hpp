// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_STATIC_ROLE_SERVICE_
#define K32_AGENT_STATIC_ROLE_SERVICE_

#include "../../fwd.hpp"
#include "../data/role_snapshot.hpp"
namespace k32::agent {

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


  };

}  // namespace k32::agent
#endif
