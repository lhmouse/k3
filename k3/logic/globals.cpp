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

::poseidon::Easy_Timer s_service_update_timer(
  *[](shptrR<::poseidon::Abstract_Timer> /*timer*/, ::poseidon::Abstract_Fiber& fiber,
      steady_time /*now*/)
    { s_service.synchronize_services_with_redis(fiber, 60s);  });

::poseidon::Easy_HWS_Server s_private_acceptor(
  *[](shptrR<::poseidon::WS_Server_Session> session, ::poseidon::Abstract_Fiber& fiber,
      ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
    {
      POSEIDON_LOG_FATAL(("service [$1]: $2 $3"), session->remote_address(), event, data);
    });

}  // namespace

const ::poseidon::Config_File& config = s_config;
const Service& service = s_service;

}  // namespace k3::logic

using namespace k3;
using namespace k3::logic;

void
poseidon_module_main(void)
  {
    POSEIDON_LOG_INFO(("Loading configuration from 'k3.conf'..."));
    s_config.reload(&"k3.conf");

    // Start the service.
    auto conf_val = s_config.query(&"application_name");
    POSEIDON_LOG_DEBUG(("* `application_name` = $1"), conf_val);
    s_service.set_application_name(conf_val.as_string());
    s_service.set_private_type(&"logic");
    auto lc = s_private_acceptor.start("[::]:0");
    s_service.set_private_port(lc->local_address().port());
    s_service_update_timer.start(1s, 30s);

    // TODO logic
  }
