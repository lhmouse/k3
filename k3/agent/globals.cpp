// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/easy/easy_timer.hpp>
namespace k3::agent {
namespace {

::poseidon::Config_File s_config;
Service s_service;

::poseidon::Easy_Timer s_service_update_timer(
  static_cast<::poseidon::Easy_Timer::thunk_type::function_type*>(
    [](shptrR<::poseidon::Abstract_Timer> /*timer*/,
       ::poseidon::Abstract_Fiber& fiber,
       ::poseidon::steady_time /*now*/)
    { s_service.synchronize_services(fiber, 60s);  }));

}  // namespace

const ::poseidon::Config_File& config = s_config;
const Service& service = s_service;

}  // namespace k3::agent

void
poseidon_module_main(void)
  {
    using namespace k3;
    using namespace k3::agent;

    s_config.reload(&"k3.conf");
    s_service.set_application_name(s_config.query("application_name").as_string());
    s_service_update_timer.start(0s, 30s);
  }
