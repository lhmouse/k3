// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_FRIENDS_FC7DDE3B_4A8E_11F0_BB68_5254005015D2_
#include "user_service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/abstract_fiber.hpp>
#include <poseidon/base/abstract_task.hpp>
#include <poseidon/static/task_executor.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <poseidon/http/http_query_parser.hpp>
namespace k32::agent {
namespace {

struct User_Connection_Information
  {
    wkptr<::poseidon::WS_Server_Session> weak_session;
    steady_clock::time_point time_last_ping;
    steady_clock::time_point time_last_pong;
    steady_clock::time_point time_last_message;
  };

struct Implementation
  {
    cow_dictionary<User_Service::handler_type> handlers;

    // local data
    cow_string mysql_account_service_uri;
    cow_string mysql_account_password;
    uint16_t client_port = 0;
    uint32_t client_rate_limit;
    seconds client_ping_timeout;

    ::poseidon::Easy_HWS_Server user_server;
    ::poseidon::Easy_Timer user_ping_timer;

    // connections from clients
    cow_dictionary<User_Information> users;
    cow_dictionary<User_Connection_Information> user_connections;
  };

struct Push_Message_Task final : ::poseidon::Abstract_Task
  {
    shptr<Push_Message_Task> m_self_lock;
    wkptr<::poseidon::WS_Server_Session> m_weak_session;
    cow_string m_opcode;
    ::taxon::Value m_request_data;

    Push_Message_Task(const shptr<::poseidon::WS_Server_Session>& session,
                      const cow_string& opcode, const ::taxon::Value& request_data)
      :
        m_weak_session(session), m_opcode(opcode), m_request_data(request_data)
      {
      }

    virtual
    void
    do_on_abstract_task_execute() override
      {
        this->m_self_lock.reset();
        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        ::taxon::Value root;
        root.mut_object()[&"opcode"] = this->m_opcode;

        if(!this->m_request_data.is_null())
          root.mut_object()[&"data"] = this->m_request_data;

        session->ws_send(::poseidon::websocket_TEXT, root.print_to_string());
      }
  };

void
do_server_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Server_Session>& session,
                      ::poseidon::Abstract_Fiber& fiber,
                      ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
  {
    POSEIDON_LOG_FATAL(("TCP $1: $2"), event, data);
  }

void
do_server_ping_timer_callback(const shptr<Implementation>& impl,
                              steady_clock::time_point now)
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
add_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.try_emplace(opcode, handler);
    if(!r.second)
      POSEIDON_THROW(("A handler for `$1` already exists"), opcode);
  }

bool
User_Service::
set_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.insert_or_assign(opcode, handler);
    return r.second;
  }

bool
User_Service::
remove_handler(const phcow_string& opcode) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->handlers.erase(opcode);
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
    int64_t client_port = 0, client_rate_limit = 0, client_ping_timeout = 0;

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

    // `k32.agent.client_port`
    conf_value = conf_file.query(&"k32.agent.client_port");
    if(conf_value.is_integer())
      client_port = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.agent.client_port`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_port < 1) || (client_port > 65535))
      POSEIDON_THROW((
          "Invalid `k32.agent.client_port`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_port, conf_file.path());

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
    this->m_impl->client_port = static_cast<uint16_t>(client_port);
    this->m_impl->client_rate_limit = static_cast<uint32_t>(client_rate_limit);
    this->m_impl->client_ping_timeout = seconds(client_ping_timeout);

    // Restart the service.
    this->m_impl->user_server.start_any(
         this->m_impl->client_port,
         ::poseidon::Easy_HWS_Server::callback_type(
            [weak_impl = wkptr<Implementation>(this->m_impl)]
               (const shptr<::poseidon::WS_Server_Session>& session,
                ::poseidon::Abstract_Fiber& fiber,
                ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
              {
                if(const auto impl = weak_impl.lock())
                  do_server_ws_callback(impl, session, fiber, event, move(data));
              }));

    this->m_impl->user_ping_timer.start(
         1000ms, 7001ms,
         ::poseidon::Easy_Timer::callback_type(
            [weak_impl = wkptr<Implementation>(this->m_impl)]
               (const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                ::poseidon::Abstract_Fiber& /*fiber*/, steady_clock::time_point now)
              {
                if(const auto impl = weak_impl.lock())
                  do_server_ping_timer_callback(impl, now);
              }));
  }

bool
User_Service::
push_message(const phcow_string& username, const cow_string& opcode,
             const ::taxon::Value& data)
  {
    if(!this->m_impl)
      return false;

    auto conn = this->m_impl->user_connections.ptr(username);
    if(!conn)
      return false;

    auto session = conn->weak_session.lock();
    if(!session)
      return false;

    // Send the message asynchronously.
    auto task1 = new_sh<Push_Message_Task>(session, opcode, data);
    ::poseidon::task_executor.enqueue(task1);
    task1->m_self_lock = task1;
    return true;
  }

}  // namespace k32::agent
