// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#ifndef K3VR5NZE_LOGIC_GLOBALS_
#define K3VR5NZE_LOGIC_GLOBALS_

#include "../fwd.hpp"
#include "../common/service.hpp"
#include <poseidon/base/config_file.hpp>
namespace k3::logic {

extern const ::poseidon::Config_File& config;
extern const Service& service;

}  // namespace k3::logic
#endif
