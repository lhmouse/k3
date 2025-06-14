// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_USER_SERVICE_
#define K32_AGENT_USER_SERVICE_

#include "../fwd.hpp"
namespace k32 {

class User_Service
  {
  public:

  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    User_Service();

  public:
    User_Service(const User_Service&) = delete;
    User_Service& operator=(const User_Service&) & = delete;
    ~User_Service();


  };

}  // namespace k32
#endif
