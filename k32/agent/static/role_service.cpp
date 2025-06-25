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
#include <poseidon/static/mysql_connector.hpp>
namespace k32::agent {
namespace {

struct Implementation
  {
    uint16_t max_number_of_roles_per_user = 0;
    uint8_t nickname_length_limits[2] = { };

    ::poseidon::Easy_Timer role_service_timer;

    // remote data from mysql
    bool db_ready = false;
    cow_uint64_dictionary<Role_Information> roles;
  };

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
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"profile";
    column.type = ::poseidon::mysql_column_blob;
    column.nullable = false;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"whole";
    column.type = ::poseidon::mysql_column_blob;
    column.nullable = false;
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
    auto task = new_sh<::poseidon::MySQL_Check_Table_Future>(::poseidon::mysql_connector,
                                                             table);
    ::poseidon::task_scheduler.launch(task);
    fiber.yield(task);
    POSEIDON_LOG_INFO(("Finished verification of MySQL table `$1`"), table.name);
  }

void
do_role_service_timer_callback(const shptr<Implementation>& impl,
                               ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(impl->db_ready == false) {
      // Check tables.
      do_mysql_check_table_role(fiber);
      impl->db_ready = true;
    }


////////////////////////??
  }

void
do_service_role_db_list_by_user(::poseidon::Abstract_Fiber& fiber,
                                ::taxon::Value& resp_data, ::taxon::Value&& req_data)
  {
    phcow_string username;

    if(req_data.is_string())
      username = req_data.as_string();
    else
      for(const auto& r : req_data.as_object())
        if(r.first == &"username")
          username = r.second.as_string();

    ////////////////////////////////////////////////////////////
    //
    static constexpr char select_from_role[] =
        R"!!!(
          SELECT `avatar`
            FROM `role`
            WHERE `username` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(username.rdstr());       // WHERE `username` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                        &select_from_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    ::taxon::V_array avatar_list;
    for(const auto& row : task1->result_rows())
      avatar_list.emplace_back(row.at(0).as_blob());

    resp_data.open_object().try_emplace(&"avatar_list", avatar_list);
    resp_data.open_object().try_emplace(&"status", &"gs_ok");
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
    ::asteria::V_integer max_number_of_roles_per_user = 4;
    ::asteria::V_array nickname_length_limits;
    ::asteria::V_integer nickname_length_limits_0 = 2, nickname_length_limits_1 = 16;

    // `agent.max_number_of_roles_per_user`
    auto conf_value = conf_file.query(&"agent.max_number_of_roles_per_user");
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
    conf_value = conf_file.query(&"agent.nickname_length_limits");
    if(conf_value.is_array())
      nickname_length_limits = conf_value.as_array();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits`: expecting an `array`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    try {
      nickname_length_limits_0 = nickname_length_limits.at(0).as_integer();
      nickname_length_limits_1 = nickname_length_limits.at(1).as_integer();
    }
    catch(...) {
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits`: expecting an `array` of two `integer`s, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());
    }

    if((nickname_length_limits_0 < 1) || (nickname_length_limits_0 > 255))
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits[0]`: value `$1` out of range",
          "[in configuration file '$2']"),
          nickname_length_limits_0, conf_file.path());

    if((nickname_length_limits_1 < 1) || (nickname_length_limits_1 > 255))
      POSEIDON_THROW((
          "Invalid `agent.nickname_length_limits[1]`: value `$1` out of range",
          "[in configuration file '$2']"),
          nickname_length_limits_1, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->max_number_of_roles_per_user = static_cast<uint16_t>(max_number_of_roles_per_user);
    this->m_impl->nickname_length_limits[0] = static_cast<uint8_t>(nickname_length_limits_0);
    this->m_impl->nickname_length_limits[1] = static_cast<uint8_t>(nickname_length_limits_1);

    // Set up request handlers.
    service.set_handler(&"/role/db_list_by_user",
        Service::handler_type(
          [weak_impl = wkptr<Implementation>(this->m_impl)]
             (::poseidon::Abstract_Fiber& fiber,
              const ::poseidon::UUID& /*req_service_uuid*/,
              ::taxon::Value& resp_data, ::taxon::Value&& req_data)
            {
              if(const auto impl = weak_impl.lock())
                do_service_role_db_list_by_user(fiber, resp_data, move(req_data));
            }));

    // Restart the service.
    this->m_impl->role_service_timer.start(
        1100ms, 31001ms,
        ::poseidon::Easy_Timer::callback_type(
          [weak_impl = wkptr<Implementation>(this->m_impl)]
             (const shptr<::poseidon::Abstract_Timer>& /*timer*/,
              ::poseidon::Abstract_Fiber& fiber, steady_time now)
            {
              if(const auto impl = weak_impl.lock())
                do_role_service_timer_callback(impl, fiber, now);
            }));
  }

}  // namespace k32::agent
