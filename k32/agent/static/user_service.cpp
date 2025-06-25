// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_FC7DDE3B_4A8E_11F0_BB68_5254005015D2_
#include "user_service.hpp"
#include "../globals.hpp"
#include "../../common/static/service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_hws_server.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/http/http_query_parser.hpp>
#include <poseidon/fiber/mysql_check_table_future.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/static/task_scheduler.hpp>
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
    uint16_t client_port;
    uint32_t client_rate_limit;
    seconds client_ping_interval;

    cow_dictionary<User_Service::http_handler_type> http_handlers;
    cow_dictionary<User_Service::ws_authenticator_type> ws_authenticators;
    cow_dictionary<User_Service::ws_handler_type> ws_handlers;

    ::poseidon::Easy_HWS_Server user_server;
    ::poseidon::Easy_Timer user_service_timer;

    // connections from clients
    bool db_ready = false;
    cow_dictionary<User_Information> users;
    cow_dictionary<User_Connection_Information> connections;
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
            session->ws_shut_down(::poseidon::ws_status_try_again_later);
            return;
          }

          // Search for an authenticator handler.
          auto authenticator = impl->ws_authenticators.ptr(path);
          if(!authenticator) {
            POSEIDON_LOG_DEBUG(("No WebSocket authenticator for `$1`"), path);
            session->ws_shut_down(::poseidon::ws_status_forbidden);
            return;
          }

          // Call the user-defined handler to get the username.
          User_Information uinfo;
          try {
            (*authenticator) (fiber, uinfo.username, cow_string(uri.query));
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Authentication error from `$1`"), session->remote_address());
            session->ws_shut_down(::poseidon::ws_status_forbidden);
            return;
          }

          if(uinfo.username.size() < 3) {
            POSEIDON_LOG_DEBUG(("Authenticated for `$1`"), path);
            session->ws_shut_down(user_ws_status_authentication_failure);
            return;
          }

          POSEIDON_LOG_INFO(("Authenticated `$1` from `$2`"), uinfo.username, session->remote_address());
          session->mut_session_user_data() = uinfo.username.rdstr();

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
          sql_args.emplace_back(uinfo.username.rdstr());            // SET `username` = ?,
          sql_args.emplace_back(uinfo.login_address.to_string());   //     `login_address` = ?,
          sql_args.emplace_back(uinfo.login_time);                  //     `creation_time` = ?,
          sql_args.emplace_back(uinfo.login_time);                  //     `login_time` = ?,
          sql_args.emplace_back(uinfo.logout_time);                 //     `logout_time` = ?
          sql_args.emplace_back(uinfo.login_address.to_string());   // UPDATE `login_address` = ?,
          sql_args.emplace_back(uinfo.login_time);                  //        `login_time` = ?

          auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &insert_into_user, sql_args);
          ::poseidon::task_scheduler.launch(task1);
          fiber.yield(task1);

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
          ::poseidon::task_scheduler.launch(task1);
          fiber.yield(task1);

          for(const auto& row : task1->result_rows()) {
            uinfo.creation_time = row.at(0).as_system_time();   // SELECT `creation_time`,
            uinfo.logout_time = row.at(1).as_system_time();     //        `logout_time`
          }

          // Publish my connection.
          ::taxon::Value redis_uinfo;
          redis_uinfo.open_object().try_emplace(&"service_uuid", service.service_uuid().to_string());
          redis_uinfo.open_object().try_emplace(&"login_address", uinfo.login_address.to_string());
          redis_uinfo.open_object().try_emplace(&"login_time", uinfo.login_time);

          cow_vector<cow_string> redis_cmd;
          redis_cmd.emplace_back(&"SET");
          redis_cmd.emplace_back(sformat("$1/user/$2", service.application_name(), uinfo.username));
          redis_cmd.emplace_back(redis_uinfo.to_string());
          redis_cmd.emplace_back(&"GET");

          auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
          ::poseidon::task_scheduler.launch(task2);
          fiber.yield(task2);

          if(task2->result().is_string()) {
            // Parse previous data on Redis. If it is not my service UUID, then
            // kick the user on that service.
            if(!redis_uinfo.parse(task2->result().as_string()) || !redis_uinfo.is_object())
              POSEIDON_LOG_FATAL(("Could not parse user information: $1"), task2->result().as_string());
            else {
              auto pval = redis_uinfo.as_object().ptr(&"service_uuid");
              if(pval && pval->is_string()) {
                ::poseidon::UUID other_service_uuid(pval->as_string());
                if(other_service_uuid != service.service_uuid()) {
                  POSEIDON_LOG_INFO(("Conflict login `$1` from `$2`"), uinfo.username, session->remote_address());
                  auto remote = service.find_remote_service(other_service_uuid);
                  if(remote) {
                    // Kick this user from the other service.
                    ::taxon::V_object srv_args;
                    srv_args.try_emplace(&"username", uinfo.username.rdstr());
                    srv_args.try_emplace(&"ws_status", static_cast<int>(user_ws_status_login_conflict));

                    auto srv_q = new_sh<Service_Future>(remote.service_uuid, &"/user/kick", srv_args);
                    service.launch(srv_q);
                    fiber.yield(srv_q);
                  }
                }
              }
            }
          }

          if(auto uconn = impl->connections.ptr(uinfo.username)) {
            // Like above, but on the same service.
            auto other_session = uconn->weak_session.lock();
            if(other_session) {
              POSEIDON_LOG_INFO(("Conflict login `$1` from `$2`"), uinfo.username, session->remote_address());
              other_session->ws_shut_down(user_ws_status_login_conflict);
            }
          }

          // Find my roles.
          static constexpr char select_avatar_from_role[] =
              R"!!!(
                SELECT `avatar`
                  FROM `role`
                  WHERE `username` = ?
              )!!!";

          sql_args.clear();
          sql_args.emplace_back(uinfo.username.rdstr());       // WHERE `username` = ?

          task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                         &select_avatar_from_role, sql_args);
          ::poseidon::task_scheduler.launch(task1);
          fiber.yield(task1);

          ::taxon::V_array avatar_list;
          for(const auto& row : task1->result_rows())
            if(!avatar_list.emplace_back().parse(row.at(0).as_blob()))
              POSEIDON_THROW(("Could not parse role avatar: $1"), row.at(0));

          uinfo.number_of_roles = static_cast<uint32_t>(avatar_list.size());

          // Send server and role information to client.
          ::taxon::V_object welcome;
          welcome.try_emplace(&"virtual_time", clock.get_double_time_t());
          welcome.try_emplace(&"avatar_list", avatar_list);

          tinybuf_ln buf;
          ::taxon::Value(welcome).print_to(buf, ::taxon::option_json_mode);
          session->ws_send(::poseidon::ws_TEXT, buf);

          // Set up connection.
          User_Connection_Information uconn;
          uconn.weak_session = session;
          uconn.rate_time = steady_clock::now();
          uconn.pong_time = uconn.rate_time;

          impl->users.insert_or_assign(uinfo.username, uinfo);
          impl->connections.insert_or_assign(uinfo.username, uconn);

          POSEIDON_LOG_INFO(("`$1` logged in from `$2`"), uinfo.username, session->remote_address());
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
          ::taxon::Value root;
          if(!root.parse(buf) || !root.is_object()) {
            POSEIDON_LOG_WARN(("Invalid JSON object from `$1`"), session->remote_address());
            session->ws_shut_down(::poseidon::ws_status_not_acceptable);
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
            session->ws_shut_down(user_ws_status_unknown_opcode);
            return;
          }

          // Call the user-defined handler to get response data.
          ::taxon::Value response_data;
          try {
            uconn->rate_counter ++;
            (*handler) (fiber, username, response_data, move(request_data));
            uconn->pong_time = steady_clock::now();
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1`: $2"), opcode, stdex);
            session->ws_shut_down(::poseidon::ws_status_unexpected_error);
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
          session->ws_send(::poseidon::ws_TEXT, buf);
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
          ::poseidon::task_scheduler.launch(task2);
          fiber.yield(task2);

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
            resp.headers.emplace_back(&"Content-Length", response_data.ssize());
            session->http_response_headers_only(move(resp));
          } else
            session->http_response(move(resp), response_data);
          break;
        }
      }
  }

void
do_mysql_check_table_user(::poseidon::Abstract_Fiber& fiber)
  {
    ::poseidon::MySQL_Table_Structure table;
    table.name = &"user";
    table.engine = ::poseidon::mysql_engine_innodb;

    ::poseidon::MySQL_Table_Column column;
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"creation_time";
    column.type = ::poseidon::mysql_column_datetime;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"login_address";
    column.type = ::poseidon::mysql_column_varchar;
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

    // This is in the user database.
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector,
                           ::poseidon::mysql_connector.allocate_tertiary_connection(), table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_mysql_check_table_nickname(::poseidon::Abstract_Fiber& fiber)
  {
    ::poseidon::MySQL_Table_Structure table;
    table.name = &"nickname";
    table.engine = ::poseidon::mysql_engine_innodb;

    ::poseidon::MySQL_Table_Column column;
    column.name = &"nickname";
    column.type = ::poseidon::mysql_column_varchar;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"serial";
    column.type = ::poseidon::mysql_column_auto_increment;
    column.nullable = false;
    column.default_value = 15743;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"creation_time";
    column.type = ::poseidon::mysql_column_datetime;
    column.nullable = false;
    table.columns.emplace_back(column);

    ::poseidon::MySQL_Table_Index index;
    index.name = &"PRIMARY";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"nickname");
    table.indexes.emplace_back(index);

    index.clear();
    index.name = &"serial";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"serial");
    table.indexes.emplace_back(index);

    // This is in the user database.
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector,
                           ::poseidon::mysql_connector.allocate_tertiary_connection(), table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_user_service_timer_callback(const shptr<Implementation>& impl,
                               ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(impl->db_ready == false) {
      // Check tables.
      do_mysql_check_table_user(fiber);
      do_mysql_check_table_nickname(fiber);
      impl->db_ready = true;
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
        session->ws_shut_down(user_ws_status_message_rate_limit);
        continue;
      }

      if(now - it->second.pong_time > impl->client_ping_interval * 2) {
        POSEIDON_LOG_DEBUG(("PING timed out: username `$1`"), it->first);
        session->ws_shut_down(user_ws_status_ping_timeout);
        continue;
      }

      if(now - it->second.rate_time > 60s) {
        POSEIDON_LOG_TRACE(("Rate counter reset: username `$1`"), it->first);
        it->second.rate_time = now;
        it->second.rate_counter = 0;
      }

      if(now - it->second.pong_time > impl->client_ping_interval) {
        POSEIDON_LOG_TRACE(("PING: username `$1`"), it->first);
        session->ws_send(::poseidon::ws_PING, "");
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

void
do_service_user_nickname_acquire(::poseidon::Abstract_Fiber& fiber,
                                 ::taxon::Value& resp_data, ::taxon::Value&& req_data)
  {
    cow_string nickname;
    phcow_string username;

    if(req_data.is_string())
      nickname = req_data.as_string();
    else
      for(const auto& r : req_data.as_object())
        if(r.first == &"nickname")
          nickname = r.second.as_string();
        else if(r.first == &"username")
          username = r.second.as_string();

    ////////////////////////////////////////////////////////////
    //
    static constexpr char insert_into_nickname[] =
        R"!!!(
          INSERT IGNORE INTO `nickname`
            SET `nickname` = ?,
                `username` = ?,
                `creation_time` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(nickname);               // SET `nickname` = ?,
    sql_args.emplace_back(username.rdstr());       //     `username` = ?,
    sql_args.emplace_back(system_clock::now());    //     `login_time` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                            ::poseidon::mysql_connector.allocate_tertiary_connection(),
                            &insert_into_nickname, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    if((task1->affected_rows() == 0) && username.empty()) {
      resp_data.open_object().try_emplace(&"status", &"gs_nickname_exists");
      return;
    }

    if(task1->affected_rows() == 0) {
      // In this case, we still want to return a serial if the nickname belongs
      // to the same user.
      static constexpr char select_from_nickname[] =
          R"!!!(
            SELECT `serial`
              FROM `nickname`
              WHERE `nickname` = ?
                    AND `username` = ?
          )!!!";

      sql_args.clear();
      sql_args.emplace_back(nickname);          //  WHERE `nickname` = ?
      sql_args.emplace_back(username.rdstr());  //        AND `username` = ?

      task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                            ::poseidon::mysql_connector.allocate_tertiary_connection(),
                            &select_from_nickname, sql_args);
      ::poseidon::task_scheduler.launch(task1);
      fiber.yield(task1);

      for(const auto& row : task1->result_rows())
        resp_data.open_object().try_emplace(&"serial", row.at(0).as_integer());  // SELECT `serial`

      resp_data.open_object().try_emplace(&"status", &"gs_nickname_exists");
      return;
    }

    POSEIDON_LOG_INFO(("Acquired nickname `$1`"), nickname);

    resp_data.open_object().try_emplace(&"serial", static_cast<int64_t>(task1->insert_id()));
    resp_data.open_object().try_emplace(&"status", &"gs_ok");
  }

void
do_service_user_nickname_release(::poseidon::Abstract_Fiber& fiber,
                                 ::taxon::Value& resp_data, ::taxon::Value&& req_data)
  {
    cow_string nickname;

    if(req_data.is_string())
      nickname = req_data.as_string();
    else
      for(const auto& r : req_data.as_object())
        if(r.first == &"nickname")
          nickname = r.second.as_string();

    ////////////////////////////////////////////////////////////
    //
    static constexpr char delete_from_nickname[] =
        R"!!!(
          DELETE FROM `nickname`
            WHERE `nickname` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(nickname);               // WHERE `nickname` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                            ::poseidon::mysql_connector.allocate_tertiary_connection(),
                            &delete_from_nickname, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    if(task1->affected_rows() == 0) {
      resp_data.open_object().try_emplace(&"status", &"gs_nickname_not_found");
      return;
    }

    POSEIDON_LOG_INFO(("Released nickname `$1`"), nickname);

    resp_data.open_object().try_emplace(&"status", &"gs_ok");
  }

void
do_service_user_kick(const shptr<Implementation>& impl,
                     ::taxon::Value& resp_data, ::taxon::Value&& req_data)
  {
    phcow_string username;
    int ws_status = 1008;
    cow_string reason;

    if(req_data.is_string())
      username = req_data.as_string();
    else
      for(const auto& r : req_data.as_object())
        if(r.first == &"username")
          username = r.second.as_string();
        else if(r.first == &"ws_status")
          ws_status = clamp_cast<int>(r.second.as_number(), 1000, 4999);
        else if(r.first == &"reason")
          reason = r.second.as_string();

    ////////////////////////////////////////////////////////////
    //
    shptr<::poseidon::WS_Server_Session> session;
    if(auto uconn = impl->connections.ptr(username))
      session = uconn->weak_session.lock();

    if(session == nullptr) {
      resp_data.open_object().try_emplace(&"status", &"gs_user_not_online");
      return;
    }

    session->ws_shut_down(ws_status, reason);

    POSEIDON_LOG_INFO(("Kicked user `$1`: $2 $3"), username, ws_status, reason);

    resp_data.open_object().try_emplace(&"status", &"gs_ok");
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
    ::asteria::V_array client_port_list;
    ::asteria::V_integer client_port = 0;
    ::asteria::V_integer client_rate_limit = 0, client_ping_interval = 0;

    // `agent.client_port_list`
    auto conf_value = conf_file.query(&"agent.client_port_list");
    if(conf_value.is_array())
      client_port_list = conf_value.as_array();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.client_port_list`: expecting an `array`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if(service.service_index() >= client_port_list.size())
      POSEIDON_THROW((
          "No enough values in `agent.client_port_list`: too many processes",
          "[in configuration file '$2']"),
          service.service_index(), conf_file.path());

    conf_value = client_port_list.at(service.service_index());
    if(conf_value.is_integer())
      client_port = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.client_port_list[$3]`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path(), service.service_index());

    if((client_port < 1) || (client_port > 65535))
      POSEIDON_THROW((
          "Invalid `agent.client_port_list[$3]`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_port, conf_file.path(), service.service_index());

    // `agent.client_rate_limit`
    conf_value = conf_file.query(&"agent.client_rate_limit");
    if(conf_value.is_integer())
      client_rate_limit = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.client_rate_limit`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_rate_limit < 1) || (client_rate_limit > 99999))
      POSEIDON_THROW((
          "Invalid `agent.client_rate_limit`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_rate_limit, conf_file.path());

    // `agent.client_ping_interval`
    conf_value = conf_file.query(&"agent.client_ping_interval");
    if(conf_value.is_integer())
      client_ping_interval = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.client_ping_interval`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((client_ping_interval < 1) || (client_ping_interval > 99999))
      POSEIDON_THROW((
          "Invalid `agent.client_ping_interval`: value `$1` out of range",
          "[in configuration file '$2']"),
          client_ping_interval, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->client_port = static_cast<uint16_t>(client_port);
    this->m_impl->client_rate_limit = static_cast<uint32_t>(client_rate_limit);
    this->m_impl->client_ping_interval = seconds(client_ping_interval);

    // Set up request handlers.
    service.set_handler(&"/user/nickname/acquire",
        Service::handler_type(
          [weak_impl = wkptr<Implementation>(this->m_impl)]
             (::poseidon::Abstract_Fiber& fiber,
              const ::poseidon::UUID& /*req_service_uuid*/,
              ::taxon::Value& resp_data, ::taxon::Value&& req_data)
            {
              if(const auto impl = weak_impl.lock())
                do_service_user_nickname_acquire(fiber, resp_data, move(req_data));
            }));

    service.set_handler(&"/user/nickname/release",
        Service::handler_type(
          [weak_impl = wkptr<Implementation>(this->m_impl)]
             (::poseidon::Abstract_Fiber& fiber,
              const ::poseidon::UUID& /*req_service_uuid*/,
              ::taxon::Value& resp_data, ::taxon::Value&& req_data)
            {
              if(const auto impl = weak_impl.lock())
                do_service_user_nickname_release(fiber, resp_data, move(req_data));
            }));

    service.set_handler(&"/user/kick",
        Service::handler_type(
          [weak_impl = wkptr<Implementation>(this->m_impl)]
             (::poseidon::Abstract_Fiber& /*fiber*/,
              const ::poseidon::UUID& /*req_service_uuid*/,
              ::taxon::Value& resp_data, ::taxon::Value&& req_data)
            {
              if(const auto impl = weak_impl.lock())
                do_service_user_kick(impl, resp_data, move(req_data));
            }));

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
                do_user_service_timer_callback(impl, fiber, now);
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
    session->ws_send(::poseidon::ws_TEXT, buf);
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
      r.second->ws_send(::poseidon::ws_TEXT, buf);
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
    for(const auto& target_conn : this->m_impl->connections)
      if(auto session = target_conn.second.weak_session.lock())
        session->ws_send(::poseidon::ws_TEXT, buf);
  }

void
User_Service::
kick_user(const phcow_string& username, User_WS_Status ws_status) noexcept
  {
    if(!this->m_impl)
      return;

    shptr<::poseidon::WS_Server_Session> session;
    if(auto uconn = this->m_impl->connections.ptr(username))
      session = uconn->weak_session.lock();

    if(session == nullptr)
      return;

    session->ws_shut_down(ws_status, "");
  }

}  // namespace k32::agent
