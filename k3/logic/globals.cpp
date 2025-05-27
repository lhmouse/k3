// This file is part of k3.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/socket/tcp_acceptor.hpp>
namespace k3::logic {
namespace {

::poseidon::Config_File s_config;
Service s_service;
Clock s_clock;

void
do_synchronize_service(shptrR<::poseidon::Abstract_Timer> /*timer*/,
                       ::poseidon::Abstract_Fiber& fiber, steady_time /*now*/)
  {
    POSEIDON_LOG_TRACE(("Synchronizing services"));
    s_service.synchronize_services_with_redis(fiber, 60s);
  }

void
do_accept_server_connection(shptrR<::poseidon::WS_Server_Session> session,
                            ::poseidon::Abstract_Fiber& fiber,
                            ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_FATAL(("service [$1]: $2 $3"), session->remote_address(), event, data);
  }

::poseidon::Easy_Timer s_service_timer(do_synchronize_service);
::poseidon::Easy_HWS_Server s_service_acceptor(do_accept_server_connection);

}  // namespace

::poseidon::Config_File config;
Service service;
Clock clock;

}  // namespace k3::logic

void
poseidon_module_main(void)
  {
    using namespace k3;
    using namespace k3::logic;

    POSEIDON_LOG_INFO(("Loading configuration from 'k3.conf'..."));
    s_config.reload(&"k3.conf");

    // Start the service.
    auto conf_val = s_config.query(&"application_name");
    POSEIDON_LOG_DEBUG(("* `application_name` = $1"), conf_val);
    s_service.set_application_name(conf_val.as_string());
    s_service.set_private_type(&"logic");
    auto lc = s_service_acceptor.start("[::]:0");
    s_service.set_private_port(lc->local_address().port());
    s_service_timer.start(1s, 30s);
  }
