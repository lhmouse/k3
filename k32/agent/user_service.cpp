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
#include <poseidon/fiber/mysql_check_table_future.hpp>
#include <poseidon/static/task_executor.hpp>
#include <poseidon/fiber/mysql_query_future.hpp>
#include <poseidon/mysql/mysql_connection.hpp>
#include <poseidon/static/mysql_connector.hpp>
namespace k32::agent {
namespace {

struct User_Connection_Information
  {
    wkptr<::poseidon::WS_Server_Session> weak_session;
    steady_time rate_time;
    uint32_t rate_counter = 0;
    steady_time pong_time;
  };

struct Implementation
  {
    cow_dictionary<User_Service::http_handler_type> http_handlers;
    cow_dictionary<User_Service::ws_authenticator_type> ws_authenticators;
    cow_dictionary<User_Service::ws_handler_type> ws_handlers;

    // local data
    uint16_t client_port = 0;
    uint32_t client_rate_limit;
    seconds client_ping_interval;

    ::poseidon::Easy_HWS_Server user_server;
    ::poseidon::Easy_Timer user_service_timer;

    // connections from clients
    bool db_ready = false;
    cow_dictionary<User_Connection_Information> connections;
    cow_dictionary<User_Information> users;
    ::std::vector<phcow_string> expired_connections;
  };

void
do_server_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Server_Session>& session,
                      ::poseidon::Abstract_Fiber& fiber,
                      ::poseidon::Easy_HWS_Event event, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_hws_open:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          phcow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          // Wait for database verification.
          if(impl->db_ready == false) {
            POSEIDON_LOG_DEBUG(("User database verification in progress"));
            session->ws_shut_down(::poseidon::websocket_status_try_again_later);
            return;
          }

          // Search for an authenticator handler.
          auto authenticator = impl->ws_authenticators.ptr(path);
          if(!authenticator) {
            POSEIDON_LOG_DEBUG(("No WebSocket authenticator for `$1`"), path);
            session->ws_shut_down(::poseidon::websocket_status_forbidden);
            return;
          }

          // Call the user-defined handler to get the username.
          User_Information uinfo;
          try {
            (*authenticator) (fiber, uinfo.username, cow_string(uri.query));
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Authentication error from `$1`"), session->remote_address());
            session->ws_shut_down(::poseidon::websocket_status_forbidden);
            return;
          }

          if(uinfo.username.size() < 3) {
            POSEIDON_LOG_DEBUG(("Authenticated for `$1`"), path);
            session->ws_shut_down(::poseidon::websocket_status_unauthorized);
            return;
          }

          // Create the user if one doesn't exist. Ensure they can only log in
          // on a single instance.
          uinfo.login_address = session->remote_address();
          uinfo.login_time = system_clock::now();

          static constexpr char insert_into_user[] =
            R"!!!(
              INSERT INTO `user`
                SET `username` = ?,
                    `login_address` = ?,
                    `creation_time` = ?,
                    `login_time` = ?,
                    `logout_time` = ?
                ON DUPLICATE KEY
                UPDATE `login_address` = ?,
                       `login_time` = ?
            )!!!";

          cow_vector<::poseidon::MySQL_Value> sql_args;
          sql_args.emplace_back(uinfo.username.rdstr());                  // SET `username` = ?,
          sql_args.emplace_back(uinfo.login_address.print_to_string());   //     `login_address` = ?,
          sql_args.emplace_back(uinfo.login_time);                        //     `creation_time` = ?,
          sql_args.emplace_back(uinfo.login_time);                        //     `login_time` = ?,
          sql_args.emplace_back(uinfo.logout_time);                       //     `logout_time` = ?
          sql_args.emplace_back(uinfo.login_address.print_to_string());   // UPDATE `login_address` = ?,
          sql_args.emplace_back(uinfo.login_time);                        //        `login_time` = ?

          auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &insert_into_user, sql_args);
          ::poseidon::task_executor.enqueue(task1);
          ::poseidon::fiber_scheduler.yield(fiber, task1);

          static constexpr char select_from_user[] =
            R"!!!(
              SELECT `creation_time`,
                     `logout_time`
                FROM `user`
                WHERE `username` = ?
            )!!!";

          sql_args.clear();
          sql_args.emplace_back(uinfo.username.rdstr());

          task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &select_from_user, sql_args);
          ::poseidon::task_executor.enqueue(task1);
          ::poseidon::fiber_scheduler.yield(fiber, task1);

          uinfo.creation_time = task1->result_rows().at(0).at(0).as_system_time();
          uinfo.logout_time = task1->result_rows().at(0).at(1).as_system_time();

          // Set up connection.
          auto& uconn = impl->connections.open(uinfo.username);
          if(auto other_session = uconn.weak_session.lock()) {
            POSEIDON_LOG_DEBUG(("Conflict login `$1` from `$2`"), uinfo.username, session->remote_address());
            other_session->ws_shut_down(static_cast<::poseidon::WebSocket_Status>(4001), "c/riiSh5uy");
          }

          uconn.weak_session = session;
          uconn.rate_time = steady_clock::now();
          uconn.rate_counter = 0;
          uconn.pong_time = steady_clock::now();

          // Authentication complete.
          session->mut_session_user_data() = uinfo.username.rdstr();
          impl->users.insert_or_assign(uinfo.username, uinfo);
          POSEIDON_LOG_INFO(("Authenticated `$1` from `$2`"), uinfo.username, session->remote_address());
          break;
        }

      case ::poseidon::easy_hws_text:
      case ::poseidon::easy_hws_binary:
        {
          if(session->session_user_data().is_null())
            return;

          phcow_string username = session->session_user_data().as_string();
          auto uconn = impl->connections.mut_ptr(username);
          if(!uconn)
            return;

          tinybuf_ln buf(move(data));
          ::taxon::Parser_Context pctx;
          ::taxon::Value root;
          root.parse_with(pctx, buf, ::taxon::option_json_mode);
          if(pctx.error || !root.is_object()) {
            POSEIDON_LOG_ERROR(("Invalid TAXON object from `$1`"), session->remote_address());
            session->ws_shut_down(::poseidon::websocket_status_not_acceptable);
            return;
          }

          phcow_string opcode;
          ::taxon::Value request_data;
          ::taxon::Value serial;

          for(const auto& r : root.as_object())
            if(r.first == &"opcode")
              opcode = r.second.as_string();
            else if(r.first == &"data")
              request_data = r.second;
            else if(r.first == &"serial")
              serial = r.second;

          // Search for a handler.
          auto handler = impl->ws_handlers.ptr(opcode);
          if(!handler) {
            POSEIDON_LOG_WARN(("Unknown opcode `$1` from user `$2`"), opcode, username);
            session->ws_shut_down(::poseidon::websocket_status_policy_violation);
            return;
          }

          // Call the user-defined handler to get response data.
          ::taxon::Value response_data;
          try {
            (*handler) (fiber, username, response_data, move(request_data));
            uconn->rate_counter ++;
            uconn->pong_time = steady_clock::now();
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1`: $2"), opcode, stdex);
            session->ws_shut_down(::poseidon::websocket_status_unexpected_error);
            return;
          }

          if(serial.is_null())
            break;

          // The client expects a response, so send it.
          root.open_object().clear();
          root.open_object().try_emplace(&"serial", serial);
          if(!response_data.is_null())
            root.open_object().try_emplace(&"data", response_data);

          buf.clear_buffer();
          root.print_to(buf, ::taxon::option_json_mode);

          session->ws_send(::poseidon::websocket_TEXT, buf);
          break;
        }

      case ::poseidon::easy_hws_pong:
        {
          if(session->session_user_data().is_null())
            return;

          phcow_string username = session->session_user_data().as_string();
          auto uconn = impl->connections.mut_ptr(username);
          if(!uconn)
            return;

          uconn->pong_time = steady_clock::now();
          POSEIDON_LOG_TRACE(("PONG: username `$1`"), username);
          break;
        }

      case ::poseidon::easy_hws_close:
        {
          if(session->session_user_data().is_null())
            return;

          phcow_string username = session->session_user_data().as_string();
          auto uconn = impl->connections.mut_ptr(username);
          if(!uconn)
            return;

          auto uinfo = impl->users.mut_ptr(username);
          if(!uinfo)
            return;

          static constexpr char update_user_logout_time[] =
            R"!!!(
              UPDATE `user`
                SET `logout_time` = ?
                WHERE `username` = ?
            )!!!";

          cow_vector<::poseidon::MySQL_Value> sql_args;
          sql_args.emplace_back(system_clock::now());
          sql_args.emplace_back(username.rdstr());

          auto task2 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &update_user_logout_time, sql_args);
          ::poseidon::task_executor.enqueue(task2);
          ::poseidon::fiber_scheduler.yield(fiber, task2);

          POSEIDON_LOG_INFO(("`$1` logged out from `$2`"), username, session->remote_address());
          break;
        }

      case ::poseidon::easy_hws_get:
      case ::poseidon::easy_hws_head:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          phcow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          // Search for a handler.
          auto handler = impl->http_handlers.ptr(path);
          if(!handler) {
            POSEIDON_LOG_DEBUG(("No HTTP handler for `$1`"), path);
            session->http_shut_down(::poseidon::http_status_not_found);
            return;
          }

          // Call the user-defined handler to get response data.
          cow_string response_content_type = &"application/octet-stream";
          cow_string response_data;
          try {
            (*handler) (fiber, response_content_type, response_data, cow_string(uri.query));
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1`: $2"), data, stdex);
            session->http_shut_down(::poseidon::http_status_internal_server_error);
            return;
          }

          // Make an HTTP response.
          ::poseidon::HTTP_S_Headers resp;
          resp.status = ::poseidon::http_status_ok;
          resp.headers.emplace_back(&"Content-Type", response_content_type);
          resp.headers.emplace_back(&"Cache-Control", &"no-cache");
          if(event == ::poseidon::easy_hws_head) {
            resp.headers.emplace_back(&"Content-Length", static_cast<int64_t>(response_data.size()));
            session->http_response_headers_only(move(resp));
          } else
            session->http_response(move(resp), response_data);
          break;
        }
      }
  }

void
do_service_timer_callback(const shptr<Implementation>& impl,
                          ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(impl->db_ready == false) {
      ::poseidon::MySQL_Table_Structure table;
      table.name = &"user";
      table.engine = ::poseidon::mysql_engine_innodb;

      ::poseidon::MySQL_Table_Column column;
      column.name = &"username";
      column.type = ::poseidon::mysql_column_varchar;
      column.nullable = false;
      table.columns.emplace_back(column);

      column.clear();
      column.name = &"login_address";
      column.type = ::poseidon::mysql_column_varchar;
      column.nullable = false;
      table.columns.emplace_back(column);

      column.clear();
      column.name = &"creation_time";
      column.type = ::poseidon::mysql_column_datetime;
      column.nullable = false;
      table.columns.emplace_back(column);

      column.clear();
      column.name = &"login_time";
      column.type = ::poseidon::mysql_column_datetime;
      column.nullable = false;
      table.columns.emplace_back(column);

      column.clear();
      column.name = &"logout_time";
      column.type = ::poseidon::mysql_column_datetime;
      column.nullable = false;
      table.columns.emplace_back(column);

      ::poseidon::MySQL_Table_Index index;
      index.name = &"PRIMARY";
      index.type = ::poseidon::mysql_index_unique;
      index.columns.emplace_back(&"username");
      table.indexes.emplace_back(index);

      // Get a connection to user database.
      auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector,
                              ::poseidon::mysql_connector.allocate_tertiary_connection(), table);
      ::poseidon::task_executor.enqueue(task);
      ::poseidon::fiber_scheduler.yield(fiber, task);

      impl->db_ready = true;
      POSEIDON_LOG_INFO(("User database verification complete"));
    }

    // Poll clients.
    for(auto it = impl->connections.mut_begin();  it != impl->connections.end();  ++it) {
      auto session = it->second.weak_session.lock();
      if(!session) {
        POSEIDON_LOG_TRACE(("CLOSED: username `$1`"), it->first);
        impl->expired_connections.emplace_back(it->first);
        continue;
      }

      if(it->second.rate_counter > impl->client_rate_limit * 60) {
        POSEIDON_LOG_DEBUG(("Rate limit: username `$1`"), it->first);
        session->ws_shut_down(static_cast<::poseidon::WebSocket_Status>(4001), "c/uSaefae4");
        continue;
      }

      if(now - it->second.pong_time > impl->client_ping_interval * 2) {
        POSEIDON_LOG_DEBUG(("PING timed out: username `$1`"), it->first);
        session->ws_shut_down(static_cast<::poseidon::WebSocket_Status>(4001), "c/Ieree7un");
        continue;
      }

      if(now - it->second.rate_time > 60s) {
        POSEIDON_LOG_TRACE(("Rate counter reset: username `$1`"), it->first);
        it->second.rate_time = now;
        it->second.rate_counter = 0;
      }

      if(now - it->second.pong_time > impl->client_ping_interval) {
        POSEIDON_LOG_TRACE(("PING: username `$1`"), it->first);
        session->ws_send(::poseidon::websocket_PING, "");
      }
    }

    while(impl->expired_connections.size() != 0) {
      phcow_string username = move(impl->expired_connections.back());
      impl->expired_connections.pop_back();

      POSEIDON_LOG_INFO(("Unloading user information: username `$1`"), username);
      impl->connections.erase(username);
      impl->users.erase(username);
    }
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

const User_Information&
User_Service::
find_user(const phcow_string& username) const noexcept
  {
    if(!this->m_impl)
      return null_user_information;

    auto ptr = this->m_impl->users.ptr(username);
    if(!ptr)
      return null_user_information;

    return *ptr;
  }

void
User_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    int64_t client_port = 0;
    int64_t client_rate_limit = 0, client_ping_interval = 0;

    // `k32.agent.client_port`
    auto conf_value = conf_file.query(&"k32.agent.client_port");
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

    // `k32.agent.client_ping_interval`
    conf_value = conf_file.query(&"k32.agent.client_ping_interval");
    if(conf_value.is_integer())
      client_ping_interval = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.agent.client_ping_interval`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_ping_interval < 1) || (client_ping_interval > 99999))
      POSEIDON_THROW((
          "Invalid `k32.agent.client_ping_interval`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_ping_interval, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->client_port = static_cast<uint16_t>(client_port);
    this->m_impl->client_rate_limit = static_cast<uint32_t>(client_rate_limit);
    this->m_impl->client_ping_interval = seconds(client_ping_interval);

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

    this->m_impl->user_service_timer.start(
         1000ms, 7001ms,
         ::poseidon::Easy_Timer::callback_type(
            [weak_impl = wkptr<Implementation>(this->m_impl)]
               (const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                ::poseidon::Abstract_Fiber& fiber, steady_time now)
              {
                if(const auto impl = weak_impl.lock())
                  do_service_timer_callback(impl, fiber, now);
              }));
  }

void
User_Service::
notify_one(const phcow_string& username, const cow_string& opcode,
           const ::taxon::Value& notification_data)
  {
    if(!this->m_impl)
      return;

    shptr<::poseidon::WS_Server_Session> session;
    if(auto uconn = this->m_impl->connections.ptr(username))
      session = uconn->weak_session.lock();

    if(session == nullptr)
      return;

    // Encode the message and send it.
    ::taxon::Value root;
    root.open_object().try_emplace(&"opcode", opcode);
    if(!notification_data.is_null())
      root.open_object().try_emplace(&"data", notification_data);

    tinybuf_ln buf;
    root.print_to(buf, ::taxon::option_json_mode);

    session->ws_send(::poseidon::websocket_TEXT, buf);
  }

void
User_Service::
notify_some(const cow_vector<phcow_string>& username_list, const cow_string& opcode,
            const ::taxon::Value& notification_data)
  {
    if(!this->m_impl)
      return;

    cow_dictionary<shptr<::poseidon::WS_Server_Session>> session_map;
    for(const auto& username : username_list)
      if(auto uconn = this->m_impl->connections.ptr(username))
        if(auto session = uconn->weak_session.lock())
          session_map.try_emplace(username, move(session));

    if(session_map.size() == 0)
      return;

    // Encode the message and send it.
    ::taxon::Value root;
    root.open_object().try_emplace(&"opcode", opcode);
    if(!notification_data.is_null())
      root.open_object().try_emplace(&"data", notification_data);

    tinybuf_ln buf;
    root.print_to(buf, ::taxon::option_json_mode);

    for(const auto& r : session_map)
      r.second->ws_send(::poseidon::websocket_TEXT, buf);
  }

void
User_Service::
notify_all(const cow_string& opcode, const ::taxon::Value& notification_data)
  {
    if(!this->m_impl)
      return;

    if(this->m_impl->connections.size() == 0)
      return;

    // Encode the message and send it.
    ::taxon::Value root;
    root.open_object().try_emplace(&"opcode", opcode);
    if(!notification_data.is_null())
      root.open_object().try_emplace(&"data", notification_data);

    tinybuf_ln buf;
    root.print_to(buf, ::taxon::option_json_mode);

    for(const auto& r : this->m_impl->connections)
      if(auto session = r.second.weak_session.lock())
        session->ws_send(::poseidon::websocket_TEXT, buf);
  }

}  // namespace k32::agent
