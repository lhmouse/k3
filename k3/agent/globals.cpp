// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/easy/easy_hwss_server.hpp>
namespace k3::agent {
namespace {

::poseidon::Config_File s_config;
Service s_service;
constexpr seconds s_service_ttl = 60s;

::poseidon::Easy_HWS_Server s_private_acceptor(
    static_cast<::poseidon::Easy_HWS_Server::thunk_type::function_type*>(
      [](shptrR<::poseidon::WS_Server_Session> session, ::poseidon::Abstract_Fiber& fiber,
         ::poseidon::Easy_HWS_Event event, ::rocket::linear_buffer&& data)
      {
        POSEIDON_LOG_FATAL(("service [$1]: $2 $3"), session->remote_address(), event, data);
      }
    ));

::poseidon::Easy_HWS_Server s_client_acceptor_tcp(
    static_cast<::poseidon::Easy_HWS_Server::thunk_type::function_type*>(
      [](shptrR<::poseidon::WS_Server_Session> session, ::poseidon::Abstract_Fiber& fiber,
         ::poseidon::Easy_HWS_Event event, ::rocket::linear_buffer&& data)
      {
        POSEIDON_LOG_WARN(("client [$1]: $2 $3"), session->remote_address(), event, data);
      }
    ));

::poseidon::Easy_HWSS_Server s_client_acceptor_ssl(
    static_cast<::poseidon::Easy_HWSS_Server::thunk_type::function_type*>(
      [](shptrR<::poseidon::WSS_Server_Session> session,
         ::poseidon::Abstract_Fiber& fiber, ::poseidon::Easy_HWS_Event event,
         ::rocket::linear_buffer&& data)
      {
        POSEIDON_LOG_WARN(("client(ssl) [$1]: $2 $3"), session->remote_address(), event, data);
      }
    ));

::poseidon::Easy_Timer s_service_update_timer(
    static_cast<::poseidon::Easy_Timer::thunk_type::function_type*>(
      [](shptrR<::poseidon::Abstract_Timer> /*timer*/, ::poseidon::Abstract_Fiber& fiber,
         steady_time /*now*/)
      { s_service.synchronize_services_with_redis(fiber, s_service_ttl);  }
    ));

}  // namespace

const ::poseidon::Config_File& config = s_config;
const Service& service = s_service;
const ::poseidon::Easy_HWS_Server& private_acceptor = s_private_acceptor;

const ::poseidon::Easy_HWS_Server& client_acceptor_tcp = s_client_acceptor_tcp;
const ::poseidon::Easy_HWSS_Server& client_acceptor_ssl = s_client_acceptor_ssl;

}  // namespace k3::agent

using namespace k3;
using namespace k3::agent;

void
poseidon_module_main(void)
  {
    POSEIDON_LOG_INFO(("Loading configuration from 'k3.conf'..."));
    s_config.reload(&"k3.conf");

    auto conf_val = s_config.query("application_name");
    POSEIDON_LOG_DEBUG(("* `application_name` = $1"), conf_val);
    s_service.set_application_name(conf_val.as_string());
    s_service.set_application_type(&"agent");
    s_private_acceptor.start("[::]:0");
    s_service.set_private_port(s_private_acceptor.local_address().port());

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

    // Publish myself to Redis.
    s_service_update_timer.start(0s, s_service_ttl / 2);
  }
