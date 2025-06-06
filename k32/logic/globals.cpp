// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/static/main_config.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/socket/tcp_acceptor.hpp>
namespace k32::logic {
namespace {

void
do_synchronize_service(const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                       ::poseidon::Abstract_Fiber& fiber,
                       steady_time /*now*/)
  {
    service.synchronize_services_with_redis(fiber, 60s);
  }

void
do_on_service_event(const shptr<::poseidon::WS_Server_Session>& session,
                    ::poseidon::Abstract_Fiber& fiber,
                    ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_FATAL(("service [$1]: $2 $3"), session->remote_address(), event, data);
  }

::poseidon::Easy_Timer s_service_timer(do_synchronize_service);
::poseidon::Easy_HWS_Server s_service_acceptor(do_on_service_event);

}  // namespace

Service service;
Clock clock;

}  // namespace k32::logic

void
poseidon_module_main(void)
  {
    using namespace k32;
    using namespace k32::logic;

    const auto config = ::poseidon::main_config.copy();
    ::asteria::Value conf_val;

    // Start the service.
    conf_val = config.query(&"k32.application_name");
    POSEIDON_LOG_DEBUG(("* `application_name` = $1"), conf_val);
    service.set_application_name(conf_val.as_string());
    service.set_private_type(&"logic");
    auto lc = s_service_acceptor.start_any(0);
    service.set_private_port(lc->local_address().port());
    s_service_timer.start(1s, 30s);
  }
