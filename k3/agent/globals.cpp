// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/easy/easy_hwss_server.hpp>
#include <poseidon/socket/tcp_acceptor.hpp>
namespace k3::agent {
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

::poseidon::Easy_HWS_Server s_client_acceptor_tcp(
  *[](shptrR<::poseidon::WS_Server_Session> session, ::poseidon::Abstract_Fiber& fiber,
      ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
    {
      POSEIDON_LOG_WARN(("client [$1]: $2 $3"), session->remote_address(), event, data);
    });

::poseidon::Easy_HWSS_Server s_client_acceptor_ssl(
  *[](shptrR<::poseidon::WSS_Server_Session> session, ::poseidon::Abstract_Fiber& fiber,
      ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
    {
      POSEIDON_LOG_WARN(("client(ssl) [$1]: $2 $3"), session->remote_address(), event, data);
    });

}  // namespace

const ::poseidon::Config_File& config = s_config;
const Service& service = s_service;

}  // namespace k3::agent

using namespace k3;
using namespace k3::agent;

void
poseidon_module_main(void)
  {
    POSEIDON_LOG_INFO(("Loading configuration from 'k3.conf'..."));
    s_config.reload(&"k3.conf");

    // Start the service.
    auto conf_val = s_config.query("application_name");
    POSEIDON_LOG_DEBUG(("* `application_name` = $1"), conf_val);
    s_service.set_application_name(conf_val.as_string());
    s_service.set_private_type(&"agent");
    auto lc = s_private_acceptor.start("[::]:0");
    s_service.set_private_port(lc->local_address().port());
    s_service_update_timer.start(0s, 10s);

    // Open ports for incoming connections from clients from public network.
    // These ports are optional.
    conf_val = s_config.query("agent", "client_port_tcp");
    POSEIDON_LOG_DEBUG(("* `agent.client_port_tcp` = $1"), conf_val);
    if(!conf_val.is_null()) {
      tinyfmt_str fmt;
      format(fmt, "[::]:$1", conf_val.as_integer());
      s_client_acceptor_tcp.start(fmt.get_string());
      s_service.set_property(&"client_port_tcp", static_cast<double>(conf_val.as_integer()));
    }

    conf_val = s_config.query("agent", "client_port_ssl");
    POSEIDON_LOG_DEBUG(("* `agent.client_port_ssl` = $1"), conf_val);
    if(!conf_val.is_null()) {
      tinyfmt_str fmt;
      format(fmt, "[::]:$1", conf_val.as_integer());
      s_client_acceptor_ssl.start(fmt.get_string());
      s_service.set_property(&"client_port_ssl", static_cast<double>(conf_val.as_integer()));
    }
  }
