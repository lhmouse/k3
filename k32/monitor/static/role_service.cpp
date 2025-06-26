// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
#include "role_service.hpp"
#include "../globals.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_ws_server.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/mysql_check_table_future.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/static/task_scheduler.hpp>
#include <poseidon/fiber/mysql_query_future.hpp>
#include <poseidon/mysql/mysql_connection.hpp>
#include <poseidon/static/mysql_connector.hpp>
namespace k32::monitor {
namespace {

struct Implementation
  {
    seconds role_cache_ttl;

    ::poseidon::Easy_Timer service_timer;

    // remote data from mysql
    bool db_ready = false;
    cow_int64_dictionary<Role_Information> roles;
  };

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
do_slash_nickname_acquire(const shptr<Implementation>& /*impl*/,
                          ::poseidon::Abstract_Fiber& fiber,
                          const ::poseidon::UUID& /*req_service_uuid*/,
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

    POSEIDON_CHECK(nickname != "");

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

    if((task1->match_count() == 0) && username.empty()) {
      resp_data.open_object().try_emplace(&"status", &"gs_nickname_exists");
      return;
    }

    if(task1->match_count() == 0) {
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
      sql_args.emplace_back(nickname);          // WHERE `nickname` = ?
      sql_args.emplace_back(username.rdstr());  //       AND `username` = ?

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
do_slash_nickname_release(const shptr<Implementation>& /*impl*/,
                          ::poseidon::Abstract_Fiber& fiber,
                          const ::poseidon::UUID& /*req_service_uuid*/,
                          ::taxon::Value& resp_data, ::taxon::Value&& req_data)
  {
    cow_string nickname;

    if(req_data.is_string())
      nickname = req_data.as_string();
    else
      for(const auto& r : req_data.as_object())
        if(r.first == &"nickname")
          nickname = r.second.as_string();

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
      resp_data.open_object().try_emplace(&"status", &"gs_nickname_not_found");
      return;
    }

    POSEIDON_LOG_INFO(("Released nickname `$1`"), nickname);

    resp_data.open_object().try_emplace(&"status", &"gs_ok");
  }

void
do_mysql_check_table_role(::poseidon::Abstract_Fiber& fiber)
  {
    ::poseidon::MySQL_Table_Structure table;
    table.name = &"role";
    table.engine = ::poseidon::mysql_engine_innodb;

    ::poseidon::MySQL_Table_Column column;
    column.name = &"roid";
    column.type = ::poseidon::mysql_column_int64;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"update_time";
    column.type = ::poseidon::mysql_column_datetime;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"nickname";
    column.type = ::poseidon::mysql_column_varchar;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"avatar";
    column.type = ::poseidon::mysql_column_blob;
    column.nullable = true;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"profile";
    column.type = ::poseidon::mysql_column_blob;
    column.nullable = true;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"whole";
    column.type = ::poseidon::mysql_column_blob;
    column.nullable = true;
    table.columns.emplace_back(column);

    ::poseidon::MySQL_Table_Index index;
    index.name = &"PRIMARY";
    index.type = ::poseidon::mysql_index_unique;
    index.columns.emplace_back(&"roid");
    table.indexes.emplace_back(index);

    index.clear();
    index.name = &"username";
    index.type = ::poseidon::mysql_index_multi;
    index.columns.emplace_back(&"username");
    table.indexes.emplace_back(index);

    index.clear();
    index.name = &"nickname";
    index.type = ::poseidon::mysql_index_multi;
    index.columns.emplace_back(&"nickname");
    table.indexes.emplace_back(index);

    // This is in the default database.
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector, table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_service_timer_callback(const shptr<Implementation>& impl,
                          const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                          ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(impl->db_ready == false) {
      // Check tables.
      do_mysql_check_table_nickname(fiber);
      do_mysql_check_table_role(fiber);
      impl->db_ready = true;
    }


////////////////////////??
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Role_Service,
    Implementation);

Role_Service::
Role_Service()
  {
  }

Role_Service::
~Role_Service()
  {
  }

const Role_Information&
Role_Service::
find_role(int64_t roid) const noexcept
  {
    if(!this->m_impl)
      return null_role_information;

    auto ptr = this->m_impl->roles.ptr(roid);
    if(!ptr)
      return null_role_information;

    return *ptr;
  }

void
Role_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    ::asteria::V_integer role_cache_ttl = 300;

    // `monitor.role_cache_ttl`
    auto conf_value = conf_file.query(&"monitor.role_cache_ttl");
    if(conf_value.is_integer())
      role_cache_ttl = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `monitor.role_cache_ttl`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((role_cache_ttl < 0) || (role_cache_ttl > 99999999))
      POSEIDON_THROW((
          "Invalid `monitor.role_cache_ttl`: value `$1` out of range",
          "[in configuration file '$2']"),
          role_cache_ttl, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->role_cache_ttl = seconds(role_cache_ttl);

    // Set up request handlers.
    service.set_handler(&"/nickname/acquire", bindw(this->m_impl, do_slash_nickname_acquire));
    service.set_handler(&"/nickname/release", bindw(this->m_impl, do_slash_nickname_release));

    // Restart the service.
    this->m_impl->service_timer.start(100ms, 31001ms, bindw(this->m_impl, do_service_timer_callback));
  }

}  // namespace k32::monitor
