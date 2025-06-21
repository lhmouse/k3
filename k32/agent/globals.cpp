// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/base/config_file.hpp>
namespace k32::agent {

Clock clock;
Service service;
User_Service user_service;

}  // namespace k32::agent

using namespace k32;
using namespace k32::agent;

void
poseidon_module_main(void)
  {
    ::poseidon::Config_File conf_file;
    conf_file.reload(&"k32.conf");
    service.reload(&"agent", conf_file);
    user_service.reload(conf_file, service.service_index());
  }
