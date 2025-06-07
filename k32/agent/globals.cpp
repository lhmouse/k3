// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/static/main_config.hpp>
namespace k32::agent {
namespace {
/*
void
do_synchronize_service(const shptr<::poseidon::Abstract_Timer>& *timer*,
                       ::poseidon::Abstract_Fiber& fiber,
                       steady_time *now*)
  {
    service.synchronize_services_with_redis(fiber, 60s);
  }

void
do_on_service_event(const shptr<::poseidon::WS_Server_Session>& session,
                    ::poseidon::Abstract_Fiber& fiber,
                    ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_FATAL(("service [$1]: $2 $3"), session->remote_address(), event, data);
  }

void
do_on_client_event_tcp(const shptr<::poseidon::WS_Server_Session>& session,
                       ::poseidon::Abstract_Fiber& fiber,
                       ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_WARN(("client [$1]: $2 $3"), session->remote_address(), event, data);
  }

void
do_on_client_event_ssl(const shptr<::poseidon::WSS_Server_Session>& session,
                       ::poseidon::Abstract_Fiber& fiber,
                       ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_ERROR(("client [$1]: $2 $3"), session->remote_address(), event, data);
  }

::poseidon::Easy_Timer s_service_timer(do_synchronize_service);
::poseidon::Easy_WS_Server s_service_acceptor(do_on_service_event);
::poseidon::Easy_HWS_Server s_client_acceptor_tcp(do_on_client_event_tcp);
::poseidon::Easy_HWSS_Server s_client_acceptor_ssl(do_on_client_event_ssl);
*/
}  // namespace

Service service;
Clock clock;
Account_Supervisor account_supervisor;

}  // namespace k32::agent

void
poseidon_module_main(void)
  {
    using namespace k32;
    using namespace k32::agent;
/*
    const auto config = ::poseidon::main_config.copy();
    ::asteria::Value conf_val;

    // Open ports for incoming connections from clients from public network.
    // These ports are optional.
    conf_val = config.query(&"k32.agent.client_port_tcp");
    POSEIDON_LOG_DEBUG(("* `client_port_tcp` = $1"), conf_val);
    if(!conf_val.is_null()) {
      POSEIDON_CHECK(conf_val.is_integer());
      int64_t client_port_tcp = conf_val.as_integer();
      POSEIDON_CHECK((client_port_tcp >= 1) && (client_port_tcp <= 49151));
      s_client_acceptor_tcp.start_any(static_cast<uint16_t>(client_port_tcp));
      service.set_property(&"client_port_tcp", static_cast<double>(client_port_tcp));
    }

    conf_val = config.query(&"k32.agent.client_port_ssl");
    POSEIDON_LOG_DEBUG(("* `client_port_ssl` = $1"), conf_val);
    if(!conf_val.is_null()) {
      POSEIDON_CHECK(conf_val.is_integer());
      int64_t client_port_ssl = conf_val.as_integer();
      POSEIDON_CHECK((client_port_ssl >= 1) && (client_port_ssl <= 49151));
      s_client_acceptor_ssl.start_any(static_cast<uint16_t>(client_port_ssl));
      service.set_property(&"client_port_ssl", static_cast<double>(client_port_ssl));
    }

    // Start the service.
    conf_val = config.query(&"k32.application_name");
    POSEIDON_LOG_DEBUG(("* `application_name` = $1"), conf_val);
    service.set_application_name(conf_val.as_string());
    service.set_private_type(&"agent");
    auto lc = s_service_acceptor.start_any(0);
    service.set_private_port(lc->local_address().port());
    s_service_timer.start(1s, 10s);
*/
  }
