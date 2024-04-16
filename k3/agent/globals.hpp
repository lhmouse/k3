// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#ifndef K3VR5NZE_AGENT_GLOBALS_
#define K3VR5NZE_AGENT_GLOBALS_

#include "../fwd.hpp"
#include "../common/service.hpp"
#include <poseidon/base/config_file.hpp>
namespace k3::agent {

extern const ::poseidon::Config_File& config;
extern const Service& service;

}  // namespace k3::agent
#endif
