// This file is part of k3.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K3VR5NZE_LOGIC_GLOBALS_
#define K3VR5NZE_LOGIC_GLOBALS_

#include "../fwd.hpp"
#include "../common/service.hpp"
#include "../common/clock.hpp"
#include <poseidon/base/config_file.hpp>
namespace k3::logic {

extern const ::poseidon::Config_File& config;
extern const Service& service;
extern const Clock& clock;

}  // namespace k3::logic
#endif
