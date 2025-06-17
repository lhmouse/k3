// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_FRIENDS_FC7DDE3B_4A8E_11F0_BB68_5254005015D2_
#include "user_service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/abstract_fiber.hpp>
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
    cow_dictionary<User_Service::http_handler_type> http_handlers;
    cow_dictionary<User_Service::ws_authenticator_type> ws_authenticators;
    cow_dictionary<User_Service::ws_handler_type> ws_handlers;

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

void
do_server_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Server_Session>& session,
                      ::poseidon::Abstract_Fiber& fiber,
                      ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
  {
    switch(static_cast<uint32_t>(event))
      {
      case ::poseidon::easy_hws_open:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          phcow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          phcow_string username;

          auto authenticator = impl->ws_authenticators.ptr(path);
          if(!authenticator) {
            POSEIDON_LOG_DEBUG(("No WebSocket authenticator for `$1`"), path);
            session->ws_shut_down(::poseidon::websocket_status_forbidden);
            return;
          }
          else
            try {
              (*authenticator) (fiber, username, cow_string(uri.query.p));
            }
            catch(exception& stdex) {
              POSEIDON_LOG_ERROR(("Authentication error from `$1`"), session->remote_address());
              session->ws_shut_down(::poseidon::websocket_status_forbidden);
              return;
            }

          if(username.size() < 3) {
            POSEIDON_LOG_DEBUG(("Authenticated for `$1`"), path);
            session->ws_shut_down(::poseidon::websocket_status_unauthorized);
            return;
          }

/*TODO*/

          session->mut_session_user_data() = username.rdstr();
          POSEIDON_LOG_INFO(("`$1` signed in from `$2`"), username, session->remote_address());
          break;
        }
      case ::poseidon::easy_hws_text:
      case ::poseidon::easy_hws_binary:
        {
          if(session->session_user_data().is_null())
            return;

          ::taxon::Value root;
          ::taxon::Parser_Context pctx;
          ::rocket::tinybuf_ln buf(move(data));
          root.parse_with(pctx, buf);
          POSEIDON_CHECK(!pctx.error);
          POSEIDON_CHECK(root.is_object());

          phcow_string opcode;
          ::taxon::Value request_data;
          ::taxon::Value request_id;

          for(const auto& r : root.as_object())
            if(r.first == &"opcode")
              opcode = r.second.as_string();
            else if(r.first == &"data")
              request_data = r.second;
            else if(r.first == &"id")
              request_id = r.second;

          phcow_string username = session->session_user_data().as_string();
          ::taxon::Value response_data;
          tinyfmt_str error_fmt;

          auto handler = impl->ws_handlers.ptr(opcode);
          if(!handler)
            format(error_fmt, "Unknown opcode `$1`", opcode);
          else
            try {
              (*handler) (fiber, username, response_data, move(request_data));
            }
            catch(exception& stdex) {
              POSEIDON_LOG_ERROR(("Unhandled exception in `$1`: $2"), opcode, stdex);
              format(error_fmt, "Internal error");
            }

          if(request_id.is_null())
            break;

          // The client expects a response, so send it.
          root.mut_object().clear();
          root.mut_object()[&"id"] = request_id;

          if(!response_data.is_null())
            root.mut_object()[&"data"] = response_data;

          if(error_fmt.length() != 0)
            root.mut_object()[&"error"] = error_fmt.get_string();

          session->ws_send(::poseidon::websocket_TEXT, root.print_to_string());
          break;
        }

      case ::poseidon::easy_hws_close:
        {
          if(session->session_user_data().is_null())
            return;

          phcow_string username = session->session_user_data().as_string();

/*TODO*/

          POSEIDON_LOG_INFO(("`$1` signed out from `$2`"), username, session->remote_address());
          break;
        }

      case ::poseidon::easy_hws_get:
      case ::poseidon::easy_hws_head:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          phcow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          cow_string response_content_type = &"application/octet-stream";
          cow_string response_data;

          auto handler = impl->http_handlers.ptr(path);
          if(!handler) {
            POSEIDON_LOG_DEBUG(("No HTTP handler for `$1`"), path);
            session->http_shut_down(::poseidon::http_status_not_found);
            return;
          }
          else
            try {
              (*handler) (fiber, response_content_type, response_data, cow_string(uri.query));
            }
            catch(exception& stdex) {
              POSEIDON_LOG_ERROR(("Unhandled exception in `$1`: $2"), data, stdex);
              session->http_shut_down(::poseidon::http_status_internal_server_error);
              return;
            }

          // Make an HTTP response.
          ::poseidon::HTTP_Response_Headers resp;
          resp.status = ::poseidon::http_status_ok;
          resp.headers.emplace_back(&"Content-Type", response_content_type);
          resp.headers.emplace_back(&"Cache-Control", &"no-cache");
          session->http_response(move(resp), response_data);
          break;
        }
      }
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
add_http_handler(const phcow_string& path, const http_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->http_handlers.try_emplace(path, handler);
    if(!r.second)
      POSEIDON_THROW(("An HTTP handler for `$1` already exists"), path);
  }

bool
User_Service::
set_http_handler(const phcow_string& path, const http_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->http_handlers.insert_or_assign(path, handler);
    return r.second;
  }

bool
User_Service::
remove_http_handler(const phcow_string& path) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->http_handlers.erase(path);
  }

void
User_Service::
add_ws_authenticator(const phcow_string& path, const ws_authenticator_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->ws_authenticators.try_emplace(path, handler);
    if(!r.second)
      POSEIDON_THROW(("A WebSocket authenticator for `$1` already exists"), path);
  }

bool
User_Service::
set_ws_authenticator(const phcow_string& path, const ws_authenticator_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->ws_authenticators.try_emplace(path, handler);
    return r.second;
  }

bool
User_Service::
remove_ws_authenticator(const phcow_string& path) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->ws_authenticators.erase(path);
  }

void
User_Service::
add_ws_handler(const phcow_string& opcode, const ws_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->ws_handlers.try_emplace(opcode, handler);
    if(!r.second)
      POSEIDON_THROW(("A WebSocket handler for `$1` already exists"), opcode);
  }

bool
User_Service::
set_ws_handler(const phcow_string& opcode, const ws_handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->ws_handlers.insert_or_assign(opcode, handler);
    return r.second;
  }

bool
User_Service::
remove_ws_handler(const phcow_string& opcode) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->ws_handlers.erase(opcode);
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


}  // namespace k32::agent
