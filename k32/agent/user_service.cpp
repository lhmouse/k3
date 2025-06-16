// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_FRIENDS_FC7DDE3B_4A8E_11F0_BB68_5254005015D2_
#include "user_service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_ws_server.hpp>
#include <poseidon/easy/easy_wss_server.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/abstract_fiber.hpp>
#include <poseidon/static/task_executor.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <poseidon/http/http_query_parser.hpp>
namespace k32::agent {
namespace {

struct Implementation
  {
    cow_dictionary<User_Service::handler_type> handlers;

    // local data
    cow_string mysql_account_service_uri;
    cow_string mysql_account_password;
    uint16_t client_port_tcp = 0;
    uint16_t client_port_ssl = 0;
    uint32_t client_rate_limit;
    seconds client_ping_timeout;

    ::poseidon::Easy_WS_Server user_server_tcp;
    ::poseidon::Easy_WSS_Server user_server_ssl;
    ::poseidon::Easy_Timer user_ping_timer;

    // connections from clients
    cow_dictionary<User_Information> users;
  };

void
do_server_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Server_Session>& session,
                      ::poseidon::Abstract_Fiber& fiber,
                      ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_FATAL(("TCP $1: $2"), event, data);
  }

void
do_server_wss_callback(const shptr<Implementation>& impl,
                       const shptr<::poseidon::WSS_Server_Session>& session,
                       ::poseidon::Abstract_Fiber& fiber,
                       ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_FATAL(("SSL $1: $2"), event, data);
  }

void
do_server_ping_timer_callback(const shptr<Implementation>& impl)
  {
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(User_Service,
    Implementation);

User_Service::
User_Service()
  {
  }

User_Service::
~User_Service()
  {
  }

void
User_Service::
add_handler(const phcow_string& code, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.try_emplace(code, handler);
    if(!r.second)
      POSEIDON_THROW(("A handler for `$1` already exists"), code);
  }

bool
User_Service::
set_handler(const phcow_string& code, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.insert_or_assign(code, handler);
    return r.second;
  }

bool
User_Service::
remove_handler(const phcow_string& code) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->handlers.erase(code);
  }

const User_Information*
User_Service::
find_user_opt(const phcow_string& username) const noexcept
  {
    if(!this->m_impl)
      return nullptr;

    return this->m_impl->users.ptr(username);
  }

void
User_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    cow_string mysql_account_service_uri, mysql_account_password;
    int64_t client_port_tcp = 0, client_port_ssl = 0, client_rate_limit = 0;
    int64_t client_ping_timeout = 0;

    // `mysql.k32_account_service_uri`
    auto conf_value = conf_file.query(&"mysql.k32_account_service_uri");
    if(conf_value.is_string())
      mysql_account_service_uri = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `mysql.k32_account_service_uri`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    // `mysql.k32_account_password`
    conf_value = conf_file.query(&"mysql.k32_account_password");
    if(conf_value.is_string())
      mysql_account_password = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `mysql.k32_account_password`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    // `k32.agent.client_port_tcp`
    conf_value = conf_file.query(&"k32.agent.client_port_tcp");
    if(conf_value.is_integer())
      client_port_tcp = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.agent.client_port_tcp`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_port_tcp < 0) || (client_port_tcp > 65535))
      POSEIDON_THROW((
          "Invalid `k32.agent.client_port_tcp`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_port_tcp, conf_file.path());

    // `k32.agent.client_port_ssl`
    conf_value = conf_file.query(&"k32.agent.client_port_ssl");
    if(conf_value.is_integer())
      client_port_ssl = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.agent.client_port_tcp`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_port_ssl < 0) || (client_port_ssl > 65535))
      POSEIDON_THROW((
          "Invalid `k32.agent.client_port_ssl`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_port_ssl, conf_file.path());

    // `k32.agent.client_rate_limit`
    conf_value = conf_file.query(&"k32.agent.client_rate_limit");
    if(conf_value.is_integer())
      client_rate_limit = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.agent.client_rate_limit`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_rate_limit < 1) || (client_rate_limit > 99999))
      POSEIDON_THROW((
          "Invalid `k32.agent.client_rate_limit`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_rate_limit, conf_file.path());

    // `k32.agent.client_ping_timeout`
    conf_value = conf_file.query(&"k32.agent.client_ping_timeout");
    if(conf_value.is_integer())
      client_ping_timeout = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.agent.client_ping_timeout`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_ping_timeout < 1) || (client_ping_timeout > 99999))
      POSEIDON_THROW((
          "Invalid `k32.agent.client_ping_timeout`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_ping_timeout, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->mysql_account_service_uri = mysql_account_service_uri;
    this->m_impl->mysql_account_password = mysql_account_password;
    this->m_impl->client_port_tcp = static_cast<uint16_t>(client_port_tcp);
    this->m_impl->client_port_ssl = static_cast<uint16_t>(client_port_ssl);
    this->m_impl->client_rate_limit = static_cast<uint32_t>(client_rate_limit);
    this->m_impl->client_ping_timeout = seconds(client_ping_timeout);

    // Restart the service.
    if(this->m_impl->client_port_tcp == 0)
      this->m_impl->user_server_tcp.stop();
    else
      this->m_impl->user_server_tcp.start_any(
           this->m_impl->client_port_tcp,
           ::poseidon::Easy_WS_Server::callback_type(
              [weak_impl = wkptr<Implementation>(this->m_impl)]
                 (const shptr<::poseidon::WS_Server_Session>& session,
                  ::poseidon::Abstract_Fiber& fiber,
                  ::poseidon::Easy_WS_Event event, linear_buffer&& data)
                {
                  if(const auto impl = weak_impl.lock())
                    do_server_ws_callback(impl, session, fiber, event, move(data));
                }));

    if(this->m_impl->client_port_ssl == 0)
      this->m_impl->user_server_ssl.stop();
    else
      this->m_impl->user_server_ssl.start_any(
           this->m_impl->client_port_ssl,
           ::poseidon::Easy_WSS_Server::callback_type(
              [weak_impl = wkptr<Implementation>(this->m_impl)]
                 (const shptr<::poseidon::WSS_Server_Session>& session,
                  ::poseidon::Abstract_Fiber& fiber,
                  ::poseidon::Easy_WS_Event event, linear_buffer&& data)
                {
                  if(const auto impl = weak_impl.lock())
                    do_server_wss_callback(impl, session, fiber, event, move(data));
                }));

    this->m_impl->user_ping_timer.start(
         1000ms, 7001ms,
         ::poseidon::Easy_Timer::callback_type(
            [weak_impl = wkptr<Implementation>(this->m_impl)]
               (const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                ::poseidon::Abstract_Fiber& /*fiber*/, steady_clock::time_point /*now*/)
              {
                if(const auto impl = weak_impl.lock())
                  do_server_ping_timer_callback(impl);
              }));
  }

}  // namespace k32::agent
