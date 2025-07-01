// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_6A8BAC8C_B8F6_4BDA_BD7F_B90D5BF07B81_
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
#include <unordered_set>
namespace k32::agent {
namespace {

struct User_Connection
  {
    wkptr<::poseidon::WS_Server_Session> weak_session;
    steady_time rate_time;
    steady_time pong_time;
    int rate_counter = 0;

    int64_t current_roid = 0;
    ::poseidon::UUID current_logic_srv;
    ::std::unordered_set<int64_t> available_roid_set;
  };

struct Implementation
  {
    uint16_t client_port;
    uint16_t client_rate_limit;
    uint16_t max_number_of_roles_per_user = 0;
    uint8_t nickname_length_limits[2] = { };
    seconds client_ping_interval;

    cow_dictionary<User_Service::http_handler_type> http_handlers;
    cow_dictionary<User_Service::ws_authenticator_type> ws_authenticators;
    cow_dictionary<User_Service::ws_handler_type> ws_handlers;

    ::poseidon::Easy_Timer ping_timer;
    ::poseidon::Easy_HWS_Server user_server;

    // connections from clients
    bool db_ready = false;
    cow_dictionary<User_Record> users;
    cow_dictionary<User_Connection> connections;
    ::std::vector<phcow_string> expired_connections;
  };

void
do_server_hws_callback(const shptr<Implementation>& impl,
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

          // Call the user-defined authenticator to get the username.
          User_Record uinfo;
          if(auto ptr = impl->ws_authenticators.ptr(path))
            try {
              auto authenticator = *ptr;
              authenticator(fiber, uinfo.username, cow_string(uri.query));
            }
            catch(exception& stdex) {
              POSEIDON_LOG_ERROR(("Authentication error from `$1`"), session->remote_address());
              uinfo.username.clear();
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
                  SET `username` = ?
                      , `login_address` = ?
                      , `creation_time` = ?
                      , `login_time` = ?
                      , `logout_time` = ?
                      , `banned_until` = '1999-01-01'
                  ON DUPLICATE KEY
                  UPDATE `login_address` = ?
                         , `login_time` = ?
              )!!!";

          cow_vector<::poseidon::MySQL_Value> sql_args;
          sql_args.emplace_back(uinfo.username.rdstr());            // SET `username` = ?
          sql_args.emplace_back(uinfo.login_address.to_string());   //     , `login_address` = ?
          sql_args.emplace_back(uinfo.login_time);                  //     , `creation_time` = ?
          sql_args.emplace_back(uinfo.login_time);                  //     , `login_time` = ?
          sql_args.emplace_back(uinfo.login_time);                  //     , `logout_time` = ?
          sql_args.emplace_back(uinfo.login_address.to_string());   // UPDATE `login_address` = ?
          sql_args.emplace_back(uinfo.login_time);                  //        , `login_time` = ?

          auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                      ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                      &insert_into_user, sql_args);
          ::poseidon::task_scheduler.launch(task1);
          fiber.yield(task1);

          static constexpr char select_from_user[] =
              R"!!!(
                SELECT `creation_time`
                       , `logout_time`
                       , `banned_until`
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

          if(task1->result_row_count() == 0) {
            POSEIDON_LOG_FATAL(("Could not find user `$1` in database"), uinfo.username);
            session->ws_shut_down(::poseidon::ws_status_unexpected_error);
            return;
          }

          uinfo.creation_time = task1->result_row(0).at(0).as_system_time();   // SELECT `creation_time`
          uinfo.logout_time = task1->result_row(0).at(1).as_system_time();     //        , `logout_time`
          uinfo.banned_until = task1->result_row(0).at(2).as_system_time();    //        , `banned_until`

          if(uinfo.login_time < uinfo.banned_until) {
            POSEIDON_LOG_DEBUG(("User `$1` is banned until `$2`"), uinfo.username, uinfo.banned_until);
            session->ws_shut_down(user_ws_status_ban);
            return;
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

          if(task2->result().is_string() && redis_uinfo.parse(task2->result().as_string())) {
            // Parse previous data on Redis. If it is not my service UUID, then
            // disconnect the user on that service.
            auto pval = redis_uinfo.as_object().ptr(&"service_uuid");
            if(pval && pval->is_string()) {
              ::poseidon::UUID other_service_uuid(pval->as_string());
              if(other_service_uuid != service.service_uuid()) {
                // Disconnect this user from the other service.
                ::taxon::V_object tx_args;
                tx_args.try_emplace(&"username", uinfo.username.rdstr());
                tx_args.try_emplace(&"ws_status", static_cast<int>(user_ws_status_login_conflict));

                auto srv_q = new_sh<Service_Future>(other_service_uuid, &"*user/kick", tx_args);
                service.launch(srv_q);
                fiber.yield(srv_q);
              }
            }
          }

          User_Connection uconn;
          uconn.weak_session = session;
          uconn.rate_time = steady_clock::now();
          uconn.pong_time = uconn.rate_time;

          if(auto ptr = impl->connections.ptr(uinfo.username)) {
            uconn.current_roid = ptr->current_roid;
            uconn.current_logic_srv = ptr->current_logic_srv;
          }

          if(uconn.current_roid != 0) {
            // If there's an online role, try reconnecting.
            ::taxon::V_object tx_args;
            tx_args.try_emplace(&"roid", uconn.current_roid);
            tx_args.try_emplace(&"agent_service_uuid", service.service_uuid().to_string());

            auto srv_q = new_sh<Service_Future>(uconn.current_logic_srv, &"*role/reconnect", tx_args);
            service.launch(srv_q);
            fiber.yield(srv_q);

            auto status = srv_q->response(0).obj.at(&"status").as_string();
            if(status != "gs_ok") {
              POSEIDON_LOG_WARN(("Could not reconnect to role `$1`: $2"), uconn.current_roid, status);
              uconn.current_roid = 0;
              uconn.current_logic_srv = ::poseidon::UUID();
            }
          }

          if(uconn.current_roid == 0) {
            // No role is online, so send the client a list of roles.
            ::taxon::V_object tx_args;
            tx_args.try_emplace(&"username", uinfo.username.rdstr());

            auto srv_q = new_sh<Service_Future>(randomcast(&"monitor"), &"*role/list", tx_args);
            service.launch(srv_q);
            fiber.yield(srv_q);

            ::taxon::V_array available_role_list;
            for(const auto& r : srv_q->response(0).obj.at(&"role_list").as_array()) {
              // For intermediate servers, an avatar is transferred as a JSON
              // string. We will not send a raw string to the client, so parse it.
              const auto& avatar_raw = r.as_object().at(&"avatar").as_string();
              if(avatar_raw.empty())
                continue;

              int64_t roid = r.as_object().at(&"roid").as_integer();
              POSEIDON_LOG_DEBUG(("Found role `$1` of user `$2`"), roid, uinfo.username);
              uconn.available_roid_set.insert(roid);

              ::taxon::V_object client_role;
              client_role.try_emplace(&"roid", roid);
              POSEIDON_CHECK(client_role.open(&"avatar").parse(avatar_raw));
              available_role_list.emplace_back(move(client_role));
            }

            // Send my role list so the user may choose one.
            tx_args.clear();
            tx_args.try_emplace(&"@opcode", &"=role/list");
            tx_args.try_emplace(&"role_list", available_role_list);

            tinybuf_ln buf;
            ::taxon::Value(tx_args).print_to(buf, ::taxon::option_json_mode);
            session->ws_send(::poseidon::ws_TEXT, buf);
          }

          if(auto ptr = impl->connections.ptr(uinfo.username))
            if(auto old_session = ptr->weak_session.lock())
              old_session->ws_shut_down(user_ws_status_login_conflict);

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

          const phcow_string username = session->session_user_data().as_string();

          tinybuf_ln buf(move(data));
          ::taxon::Value temp_value;
          POSEIDON_CHECK(temp_value.parse(buf, ::taxon::option_json_mode));
          ::taxon::V_object request = temp_value.as_object();
          temp_value.clear();

          phcow_string opcode;
          if(auto ptr = request.ptr(&"@opcode"))
            opcode = ptr->as_string();

          ::taxon::Value serial;
          if(auto ptr = request.ptr(&"@serial"))
            serial = *ptr;

          // Copy the handler, in case of fiber context switches.
          static_vector<User_Service::ws_handler_type, 1> handler;
          if(auto ptr = impl->ws_handlers.ptr(opcode))
            handler.emplace_back(*ptr);

          if(handler.empty()) {
            session->ws_shut_down(user_ws_status_unknown_opcode);
            return;
          }

          // Check message rate.
          double rate_limit = impl->client_rate_limit;
          auto rate_duration = steady_clock::now() - impl->connections.at(username).rate_time;
          if(rate_duration >= 1s)
            rate_limit *= duration_cast<duration<double>>(rate_duration).count();

          if(++ impl->connections.mut(username).rate_counter > rate_limit) {
            session->ws_shut_down(user_ws_status_message_rate_limit);
            return;
          }

          // Call the user-defined handler to get response data.
          ::taxon::V_object response;
          try {
            handler.front() (fiber, username, response, request);
            impl->connections.mut(username).pong_time = steady_clock::now();
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1`: $2\n$3"), opcode, request, stdex);
            session->ws_shut_down(::poseidon::ws_status_unexpected_error);
            return;
          }

          if(serial.is_null())
            break;

          // The client expects a response, so send it.
          response.try_emplace(&"@serial", serial);
          temp_value = response;

          buf.clear_buffer();
          temp_value.print_to(buf, ::taxon::option_json_mode);
          session->ws_send(::poseidon::ws_TEXT, buf);
          break;
        }

      case ::poseidon::easy_hws_pong:
        {
          if(session->session_user_data().is_null())
            return;

          const phcow_string username = session->session_user_data().as_string();

          impl->connections.mut(username).pong_time = steady_clock::now();
          POSEIDON_LOG_TRACE(("PONG: username `$1`"), username);
          break;
        }

      case ::poseidon::easy_hws_close:
        {
          if(session->session_user_data().is_null())
            return;

          const phcow_string username = session->session_user_data().as_string();

          if(impl->connections.at(username).current_roid != 0) {
            // Notify logic server.
            ::taxon::V_object tx_args;
            tx_args.try_emplace(&"roid", impl->connections.at(username).current_roid);

            auto srv_q = new_sh<Service_Future>(impl->connections.at(username).current_logic_srv,
                                                &"*role/disconnect", tx_args);
            service.launch(srv_q);
            fiber.yield(srv_q);
          }

          // Update logout time.
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

          // Copy the handler, in case of fiber context switches.
          static_vector<User_Service::http_handler_type, 1> handler;
          if(auto ptr = impl->http_handlers.ptr(path))
            handler.emplace_back(*ptr);

          if(handler.empty()) {
            session->http_shut_down(::poseidon::http_status_not_found);
            return;
          }

          // Call the user-defined handler to get response data.
          cow_string response_content_type, response_payload;
          try {
            handler.front() (fiber, response_content_type, response_payload, cow_string(uri.query));
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Unhandled exception in `$1`: $2\n$3"), path, uri.query, stdex);
            session->http_shut_down(::poseidon::http_status_bad_request);
            return;
          }

          // Ensure there's `Content-Type`.
          if(response_content_type.empty() && !response_payload.empty())
            response_content_type = &"application/octet-stream";

          // Make an HTTP response.
          ::poseidon::HTTP_S_Headers resp;
          resp.status = ::poseidon::http_status_ok;
          resp.headers.emplace_back(&"Cache-Control", &"no-cache");
          resp.headers.emplace_back(&"Content-Type", response_content_type);
          session->http_response(event == ::poseidon::easy_hws_head, move(resp), response_payload);
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
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"creation_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"login_address";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"login_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"logout_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"banned_until";
    column.type = ::poseidon::mysql_column_datetime;
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
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"serial";
    column.type = ::poseidon::mysql_column_auto_increment;
    column.default_value = 15743;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"creation_time";
    column.type = ::poseidon::mysql_column_datetime;
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
do_star_nickname_acquire(const shptr<Implementation>& /*impl*/,
                         ::poseidon::Abstract_Fiber& fiber,
                         const ::poseidon::UUID& /*request_service_uuid*/,
                         ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    cow_string nickname = request.at(&"nickname").as_string();
    POSEIDON_CHECK(nickname != "");

    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    ////////////////////////////////////////////////////////////
    //
    static constexpr char insert_into_nickname[] =
        R"!!!(
          INSERT IGNORE INTO `nickname`
            SET `nickname` = ?
                , `username` = ?
                , `creation_time` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(nickname);               // SET `nickname` = ?
    sql_args.emplace_back(username.rdstr());       //     , `username` = ?
    sql_args.emplace_back(system_clock::now());    //     , `creation_time` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                               ::poseidon::mysql_connector.allocate_tertiary_connection(),
                               &insert_into_nickname, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    int64_t serial = -1;
    if(task1->match_count() != 0)
      serial = static_cast<int64_t>(task1->insert_id());
    else {
      // In this case, we still want to return a serial if the nickname belongs
      // to the same user. This makes the operation retryable.
      static constexpr char select_from_nickname[] =
          R"!!!(
            SELECT `serial`
              FROM `nickname`
              WHERE `nickname` = ?
                    AND `username` = ?
          )!!!";

      sql_args.clear();
      sql_args.emplace_back(nickname);          // WHERE `nickname` = ?
      sql_args.emplace_back(username.rdstr());  //       AND `username` = ?

      task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                               ::poseidon::mysql_connector.allocate_tertiary_connection(),
                               &select_from_nickname, sql_args);
      ::poseidon::task_scheduler.launch(task1);
      fiber.yield(task1);

      if(task1->result_row_count() == 0) {
        response.try_emplace(&"status", &"gs_nickname_conflict");
        return;
      }

      serial = task1->result_row(0).at(0).as_integer();  // SELECT `serial`
    }

    POSEIDON_LOG_INFO(("Acquired nickname `$1`"), nickname);

    response.try_emplace(&"serial", serial);
    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_nickname_release(const shptr<Implementation>& /*impl*/,
                         ::poseidon::Abstract_Fiber& fiber,
                         const ::poseidon::UUID& /*request_service_uuid*/,
                         ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    cow_string nickname = request.at(&"nickname").as_string();
    POSEIDON_CHECK(nickname != "");

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

    if(task1->match_count() == 0) {
      response.try_emplace(&"status", &"gs_nickname_not_found");
      return;
    }

    POSEIDON_LOG_INFO(("Released nickname `$1`"), nickname);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_ping_timer_callback(const shptr<Implementation>& impl,
                       const shptr<::poseidon::Abstract_Timer>& /*timer*/,
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

      if(now - it->second.pong_time > impl->client_ping_interval * 2) {
        POSEIDON_LOG_DEBUG(("PING timed out: username `$1`"), it->first);
        session->ws_shut_down(user_ws_status_ping_timeout);
        continue;
      }

      if(now - it->second.pong_time > impl->client_ping_interval) {
        POSEIDON_LOG_TRACE(("PING: username `$1`"), it->first);
        session->ws_send(::poseidon::ws_PING, "");
      }

      it->second.rate_time = now;
      it->second.rate_counter = 0;
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
do_star_user_kick(const shptr<Implementation>& impl,
                  ::poseidon::Abstract_Fiber& /*fiber*/,
                  const ::poseidon::UUID& /*request_service_uuid*/,
                  ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    int ws_status = 1008;
    if(auto ptr = request.ptr(&"ws_status"))
      ws_status = clamp_cast<int>(ptr->as_integer(), 1000, 4999);

    cow_string reason;
    if(auto ptr = request.ptr(&"reason"))
      reason = ptr->as_string();

    ////////////////////////////////////////////////////////////
    //
    shptr<::poseidon::WS_Server_Session> session;
    if(auto uconn = impl->connections.ptr(username))
      session = uconn->weak_session.lock();

    if(session == nullptr) {
      response.try_emplace(&"status", &"gs_user_not_online");
      return;
    }

    session->ws_shut_down(ws_status, reason);

    POSEIDON_LOG_INFO(("Kicked user `$1`: $2 $3"), username, ws_status, reason);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_user_check_role(const shptr<Implementation>& impl,
                        ::poseidon::Abstract_Fiber& /*fiber*/,
                        const ::poseidon::UUID& /*request_service_uuid*/,
                        ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    int64_t roid = request.at(&"roid").as_integer();
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    User_Connection uconn;
    if(auto ptr = impl->connections.ptr(username))
      uconn = *ptr;

    if(uconn.weak_session.expired()) {
      response.try_emplace(&"status", &"gs_user_not_online");
      return;
    }

    if(uconn.current_roid != roid) {
      response.try_emplace(&"status", &"gs_roid_not_match");
      return;
    }

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_user_ban_set(const shptr<Implementation>& impl,
                     ::poseidon::Abstract_Fiber& fiber,
                     const ::poseidon::UUID& /*request_service_uuid*/,
                     ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    system_time until = request.at(&"until").as_time();
    POSEIDON_CHECK(until.time_since_epoch() >= 946684800s);  // 2000-1-1

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    static constexpr char update_user_banned_until[] =
        R"!!!(
          UPDATE `user`
            SET `banned_until` = ?
            WHERE `username` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(until);              // SET `banned_until` = ?
    sql_args.emplace_back(username.rdstr());   // WHERE `username` = ?

    auto task2 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                &update_user_banned_until, sql_args);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->match_count() == 0) {
      response.try_emplace(&"status", &"gs_user_not_found");
      return;
    }

    // Also update data in memory.
    if(auto uinfo = impl->users.mut_ptr(username))
      uinfo->banned_until = until;

    if(auto uconn = impl->connections.ptr(username))
      if(auto session = uconn->weak_session.lock())
        session->ws_shut_down(user_ws_status_ban);

    POSEIDON_LOG_INFO(("Set ban on `$1` until `$2`"), username, until);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_star_user_ban_lift(const shptr<Implementation>& impl,
                      ::poseidon::Abstract_Fiber& fiber,
                      const ::poseidon::UUID& /*request_service_uuid*/,
                      ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    phcow_string username = request.at(&"username").as_string();
    POSEIDON_CHECK(username != "");

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    static constexpr char update_user_banned_until[] =
        R"!!!(
          UPDATE `user`
            SET `banned_until` = '1999-01-01'
            WHERE `username` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(username.rdstr());   // WHERE `username` = ?

    auto task2 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                ::poseidon::mysql_connector.allocate_tertiary_connection(),
                                &update_user_banned_until, sql_args);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->match_count() == 0) {
      response.try_emplace(&"status", &"gs_user_not_found");
      return;
    }

    // Also update data in memory.
    if(auto uinfo = impl->users.mut_ptr(username))
      uinfo->banned_until = system_time();

    POSEIDON_LOG_INFO(("Lift ban on `$1`"), username);

    response.try_emplace(&"status", &"gs_ok");
  }

void
do_plus_role_create(const shptr<Implementation>& impl,
                    ::poseidon::Abstract_Fiber& fiber, const phcow_string& username,
                    ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    cow_string nickname = request.at(&"nickname").as_string();
    POSEIDON_CHECK(nickname != "");

    ////////////////////////////////////////////////////////////
    //
    if(impl->connections.at(username).current_roid != 0) {
      response.try_emplace(&"status", &"sc_role_selected");
      return;
    }

    if(impl->connections.at(username).available_roid_set.size() >= impl->max_number_of_roles_per_user) {
      response.try_emplace(&"status", &"sc_too_many_roles");
      return;
    }

    int nickname_length = 0;
    size_t offset = 0;
    while(offset < nickname.size()) {
      char32_t cp;
      if(!::asteria::utf8_decode(cp, nickname, offset)) {
        response.try_emplace(&"status", &"sc_nickname_invalid");
        return;
      }

      int w = ::wcwidth(static_cast<wchar_t>(cp));
      if(w <= 0) {
        response.try_emplace(&"status", &"sc_nickname_invalid");
        return;
      }

      nickname_length += w;
      if(nickname_length > impl->nickname_length_limits[1]) {
        response.try_emplace(&"status", &"sc_nickname_length_error");
        return;
      }
    }

    if(nickname_length < impl->nickname_length_limits[0]) {
      response.try_emplace(&"status", &"sc_nickname_length_error");
      return;
    }

    // Allocate a role ID.
    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"nickname", nickname);
    tx_args.try_emplace(&"username", username.rdstr());

    auto srv_q = new_sh<Service_Future>(loopback_uuid, &"*nickname/acquire", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    auto status = srv_q->response(0).obj.at(&"status").as_string();
    if(status != "gs_ok") {
      POSEIDON_LOG_DEBUG(("Could not acquire nickname `$1`: $2"), nickname, status);
      response.try_emplace(&"status", &"sc_nickname_conflict");
      return;
    }

    int64_t roid = srv_q->response(0).obj.at(&"serial").as_integer();

    // Create the role in database.
    tx_args.clear();
    tx_args.try_emplace(&"roid", roid);
    tx_args.try_emplace(&"nickname", nickname);
    tx_args.try_emplace(&"username", username.rdstr());

    srv_q = new_sh<Service_Future>(randomcast(&"monitor"), &"*role/create", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    if(srv_q->response_count() == 0)
      POSEIDON_THROW(("No monitor service is online"));

    status = srv_q->response(0).obj.at(&"status").as_string();
    if(status != "gs_ok") {
      POSEIDON_LOG_WARN(("Could not create role `$1` (`$2`): $3"), roid, nickname, status);
      response.try_emplace(&"status", &"sc_nickname_conflict");
      return;
    }

    // Log into this role.
    tx_args.clear();
    tx_args.try_emplace(&"roid", roid);
    tx_args.try_emplace(&"agent_service_uuid", service.service_uuid().to_string());

    srv_q = new_sh<Service_Future>(randomcast(&"logic"), &"*role/login", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    status = srv_q->response(0).obj.at(&"status").as_string();
    POSEIDON_CHECK(status == "gs_ok");

    impl->connections.mut(username).current_roid = roid;
    impl->connections.mut(username).current_logic_srv = srv_q->response(0).service_uuid;
    impl->connections.mut(username).available_roid_set.insert(roid);

    response.try_emplace(&"status", &"sc_ok");
  }

void
do_plus_role_login(const shptr<Implementation>& impl,
                   ::poseidon::Abstract_Fiber& fiber, const phcow_string& username,
                   ::taxon::V_object& response, const ::taxon::V_object& request)
  {
    int64_t roid = clamp_cast<int64_t>(request.at(&"roid").as_number(), -1, INT64_MAX);
    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    if(impl->connections.at(username).current_roid != 0) {
      response.try_emplace(&"status", &"sc_role_selected");
      return;
    }

    if(impl->connections.at(username).available_roid_set.count(roid) == 0) {
      response.try_emplace(&"status", &"sc_role_unavailable");
      return;
    }

    // Load this role into Redis.
    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"roid", roid);

    auto srv_q = new_sh<Service_Future>(randomcast(&"monitor"), &"*role/load", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    auto status = srv_q->response(0).obj.at(&"status").as_string();
    if(status != "gs_ok") {
      response.try_emplace(&"status", &"sc_role_unavailable");
      return;
    }

    // Log into this role.
    tx_args.clear();
    tx_args.try_emplace(&"roid", roid);
    tx_args.try_emplace(&"agent_service_uuid", service.service_uuid().to_string());

    srv_q = new_sh<Service_Future>(randomcast(&"logic"), &"*role/login", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    status = srv_q->response(0).obj.at(&"status").as_string();
    POSEIDON_CHECK(status == "gs_ok");

    impl->connections.mut(username).current_roid = roid;
    impl->connections.mut(username).current_logic_srv = srv_q->response(0).service_uuid;
    impl->connections.mut(username).available_roid_set.insert(roid);

    response.try_emplace(&"status", &"sc_ok");
  }

void
do_plus_role_logout(const shptr<Implementation>& impl,
                    ::poseidon::Abstract_Fiber& fiber, const phcow_string& username,
                    ::taxon::V_object& response, const ::taxon::V_object& /*request*/)
  {
    if(impl->connections.at(username).current_roid == 0) {
      response.try_emplace(&"status", &"sc_no_role_selected");
      return;
    }

    // Bring this role offline.
    ::taxon::V_object tx_args;
    tx_args.try_emplace(&"roid", impl->connections.at(username).current_roid);

    auto srv_q = new_sh<Service_Future>(impl->connections.at(username).current_logic_srv,
                                        &"*role/logout", tx_args);
    service.launch(srv_q);
    fiber.yield(srv_q);

    impl->connections.mut(username).current_roid = 0;
    impl->connections.mut(username).current_logic_srv = ::poseidon::UUID();

    response.try_emplace(&"status", &"sc_ok");
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

const User_Record&
User_Service::
find_user(const phcow_string& username) const noexcept
  {
    if(!this->m_impl)
      return User_Record::null;

    auto ptr = this->m_impl->users.ptr(username);
    if(!ptr)
      return User_Record::null;

    return *ptr;
  }

void
User_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    int64_t client_port = 0, client_rate_limit = 10, client_ping_interval = 0;
    int64_t max_number_of_roles_per_user = 4;
    int64_t nickname_length_limits_0 = 2, nickname_length_limits_1 = 16;

    // `agent.client_port_list`
    auto conf_value = conf_file.query(sformat("agent.client_port_list[$1]", service.service_index()));
    if(conf_value.is_integer())
      client_port = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.client_port_list[$3]`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path(), service.service_index());

    if((client_port < 1) || (client_port > 32767))
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

    // `agent.max_number_of_roles_per_user`
    conf_value = conf_file.query(&"agent.max_number_of_roles_per_user");
    if(conf_value.is_integer())
      max_number_of_roles_per_user = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.max_number_of_roles_per_user`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((max_number_of_roles_per_user < 0) || (max_number_of_roles_per_user > 9999))
      POSEIDON_THROW((
          "Invalid `agent.max_number_of_roles_per_user`: value `$1` out of range",
          "[in configuration file '$2']"),
          max_number_of_roles_per_user, conf_file.path());

    // `agent.nickname_length_limits`
    conf_value = conf_file.query(&"agent.nickname_length_limits[0]");
    if(conf_value.is_integer())
      nickname_length_limits_0 = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits[0]`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((nickname_length_limits_0 < 1) || (nickname_length_limits_0 > 255))
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits[0]`: value `$1` out of range",
          "[in configuration file '$2']"),
          nickname_length_limits_0, conf_file.path());

    conf_value = conf_file.query(&"agent.nickname_length_limits[1]");
    if(conf_value.is_integer())
      nickname_length_limits_1 = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits[1]`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((nickname_length_limits_1 < 1) || (nickname_length_limits_1 > 255))
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits[1]`: value `$1` out of range",
          "[in configuration file '$2']"),
          nickname_length_limits_1, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->client_port = static_cast<uint16_t>(client_port);
    this->m_impl->client_rate_limit = static_cast<uint16_t>(client_rate_limit);
    this->m_impl->client_ping_interval = seconds(client_ping_interval);
    this->m_impl->max_number_of_roles_per_user = static_cast<uint16_t>(max_number_of_roles_per_user);
    this->m_impl->nickname_length_limits[0] = static_cast<uint8_t>(nickname_length_limits_0);
    this->m_impl->nickname_length_limits[1] = static_cast<uint8_t>(nickname_length_limits_1);

    // Set up default client request handlers.
    this->m_impl->ws_handlers.insert_or_assign(&"+role/create", bindw(this->m_impl, do_plus_role_create));
    this->m_impl->ws_handlers.insert_or_assign(&"+role/login", bindw(this->m_impl, do_plus_role_login));
    this->m_impl->ws_handlers.insert_or_assign(&"+role/logout", bindw(this->m_impl, do_plus_role_logout));

    // Set up request handlers.
    service.set_handler(&"*user/kick", bindw(this->m_impl, do_star_user_kick));
    service.set_handler(&"*user/check_role", bindw(this->m_impl, do_star_user_check_role));
    service.set_handler(&"*user/ban/set", bindw(this->m_impl, do_star_user_ban_set));
    service.set_handler(&"*user/ban/lift", bindw(this->m_impl, do_star_user_ban_lift));
    service.set_handler(&"*nickname/acquire", bindw(this->m_impl, do_star_nickname_acquire));
    service.set_handler(&"*nickname/release", bindw(this->m_impl, do_star_nickname_release));

    // Restart the service.
    this->m_impl->ping_timer.start(150ms, 7001ms, bindw(this->m_impl, do_ping_timer_callback));
    this->m_impl->user_server.start(this->m_impl->client_port, bindw(this->m_impl, do_server_hws_callback));
  }

}  // namespace k32::agent
