// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/static/main_config.hpp>
namespace k32::logic {

Service service;
Clock clock;

}  // namespace k32::logic

using namespace k32;
using namespace k32::logic;

void
poseidon_module_main(void)
  {
    const auto conf_file = ::poseidon::main_config.copy();
    service.reload(conf_file, &"logic");
  }
