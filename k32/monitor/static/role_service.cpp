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
    seconds redis_role_ttl;

    ::poseidon::Easy_Timer service_timer;

    // remote data from mysql
    bool db_ready = false;
    cow_int64_dictionary<Role_Information> roles;
    ::std::list<static_vector<int64_t, 255>> save_buckets;
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
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"username";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"nickname";
    column.type = ::poseidon::mysql_column_varchar;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"update_time";
    column.type = ::poseidon::mysql_column_datetime;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"avatar";
    column.type = ::poseidon::mysql_column_blob;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"profile";
    column.type = ::poseidon::mysql_column_blob;
    table.columns.emplace_back(column);

    column.clear();
    column.name = &"whole";
    column.type = ::poseidon::mysql_column_blob;
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
do_parse_role_information_from_string(Role_Information& roinfo, const cow_string& str)
  {
    ROCKET_ASSERT(roinfo.roid != 0);

    ::taxon::Value temp_value;
    POSEIDON_CHECK(temp_value.parse(str));
    ::taxon::V_object root = temp_value.as_object();
    temp_value.clear();

    roinfo.username = root.at(&"username").as_string();
    roinfo.nickname = root.at(&"nickname").as_string();
    roinfo.update_time = root.at(&"update_time").as_time();
    roinfo.avatar = root.at(&"avatar").as_string();   // JSON as string
    roinfo.profile = root.at(&"profile").as_string();   // JSON as string
    roinfo.whole = root.at(&"whole").as_string();   // JSON as string

    roinfo.home_host = root.at(&"@home_host").as_string();
    roinfo.home_db = root.at(&"@home_db").as_string();
  }

void
do_store_role_information_into_redis(::poseidon::Abstract_Fiber& fiber,
                                     Role_Information& roinfo, seconds ttl)
  {
    ::taxon::V_object root;
    root.try_emplace(&"@home_srv", service.service_uuid().to_string());

    root.try_emplace(&"username", roinfo.username.rdstr());
    root.try_emplace(&"nickname", roinfo.nickname);
    root.try_emplace(&"update_time", roinfo.update_time);
    root.try_emplace(&"avatar", roinfo.avatar);  // JSON as string
    root.try_emplace(&"profile", roinfo.profile);  // JSON as string
    root.try_emplace(&"whole", roinfo.whole);  // JSON as string

    root.try_emplace(&"@home_host", roinfo.home_host);
    root.try_emplace(&"@home_db", roinfo.home_db);

    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"SET");
    redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roinfo.roid));
    redis_cmd.emplace_back(::taxon::Value(root).to_string());
    redis_cmd.emplace_back(&"NX");  // no replace
    redis_cmd.emplace_back(&"GET");
    redis_cmd.emplace_back(&"EX");
    redis_cmd.emplace_back(sformat("$1", ttl.count()));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(!task2->result().is_nil())
      do_parse_role_information_from_string(roinfo, task2->result().as_string());
  }

void
do_slash_role_list(const shptr<Implementation>& impl,
                   ::poseidon::Abstract_Fiber& fiber,
                   const ::poseidon::UUID& /*req_service_uuid*/,
                   ::taxon::V_object& response_data, ::taxon::V_object&& request_data)
  {
    phcow_string username;

    for(const auto& r : request_data)
      if(r.first == &"username")
        username = r.second.as_string();

    POSEIDON_CHECK(username != "");

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    static constexpr char select_avatar_from_role[] =
        R"!!!(
          SELECT `roid`
                 , `avatar`
            FROM `role`
            WHERE `username` = ?
                  AND `avatar` != ''
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(username.rdstr());       // WHERE `username` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                        &select_avatar_from_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    ::taxon::V_array role_list;
    for(const auto& row : task1->result_rows()) {
      ::taxon::V_object role;
      role.try_emplace(&"roid", row.at(0).as_integer());  // SELECT `roid`
      role.try_emplace(&"avatar", row.at(1).as_blob());   //        , `avatar`
      role_list.emplace_back(move(role));
    }

    POSEIDON_LOG_INFO(("Found $1 role(s) for user `$2`"), role_list.size(), username);

    response_data.try_emplace(&"role_list", role_list);
    response_data.try_emplace(&"status", &"gs_ok");
  }

void
do_slash_role_create(const shptr<Implementation>& impl,
                     ::poseidon::Abstract_Fiber& fiber,
                     const ::poseidon::UUID& /*req_service_uuid*/,
                     ::taxon::V_object& response_data, ::taxon::V_object&& request_data)
  {
    int64_t roid = -1;
    cow_string nickname;
    phcow_string username;

    for(const auto& r : request_data)
      if(r.first == &"roid")
        roid = r.second.as_integer();
      else if(r.first == &"nickname")
        nickname = r.second.as_string();
      else if(r.first == &"username")
        username = r.second.as_string();

    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));
    POSEIDON_CHECK(nickname != "");
    POSEIDON_CHECK(username != "");

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    Role_Information roinfo;
    roinfo.roid = roid;
    roinfo.username = username;
    roinfo.nickname = nickname;
    roinfo.update_time = system_clock::now();

    auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
    roinfo.home_host = ::poseidon::hostname;
    roinfo.home_db = mysql_conn->service_uri();

    static constexpr char insert_into_role[] =
        R"!!!(
          INSERT IGNORE INTO `role`
            SET `roid` = ?
                , `username` = ?
                , `nickname` = ?
                , `update_time` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(roinfo.roid);               // SET `roid` = ?
    sql_args.emplace_back(roinfo.username.rdstr());   //     , `username` = ?
    sql_args.emplace_back(roinfo.nickname);           //     , `nickname` = ?
    sql_args.emplace_back(roinfo.update_time);        //     , `update_time` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                               move(mysql_conn), &insert_into_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    if(task1->match_count() == 0) {
      // In this case, we still want to load a role if it belongs to the same
      // user. This makes the operation retryable.
      static constexpr char select_from_role[] =
          R"!!!(
            SELECT `nickname`
                   , `update_time`
                   , `avatar`
                   , `profile`
                   , `whole`
              FROM `role`
              WHERE `roid` = ?
                    AND `username` = ?
          )!!!";

      sql_args.clear();
      sql_args.emplace_back(roid);                // WHERE `roid` = ?
      sql_args.emplace_back(username.rdstr());    //       AND `username` = ?

      task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                     &select_from_role, sql_args);
      ::poseidon::task_scheduler.launch(task1);
      fiber.yield(task1);

      if(task1->result_rows().size() == 0) {
        response_data.try_emplace(&"status", &"gs_roid_conflict");
        return;
      }

      roinfo.nickname = task1->result_rows().front().at(0).as_blob();            // SELECT `nickname`
      roinfo.update_time = task1->result_rows().front().at(1).as_system_time();  //        , `update_time`
      roinfo.avatar = task1->result_rows().front().at(2).as_blob();              //        , `avatar`
      roinfo.profile = task1->result_rows().front().at(3).as_blob();             //        , `profile`
      roinfo.whole = task1->result_rows().front().at(4).as_blob();               //        , `whole`
    }

    do_store_role_information_into_redis(fiber, roinfo, impl->redis_role_ttl);
    impl->roles.insert_or_assign(roinfo.roid, roinfo);

    POSEIDON_LOG_INFO(("Created role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response_data.try_emplace(&"status", &"gs_ok");
  }

void
do_slash_role_load(const shptr<Implementation>& impl,
                   ::poseidon::Abstract_Fiber& fiber,
                   const ::poseidon::UUID& /*req_service_uuid*/,
                   ::taxon::V_object& response_data, ::taxon::V_object&& request_data)
  {
    int64_t roid = -1;

    for(const auto& r : request_data)
      if(r.first == &"roid")
        roid = r.second.as_integer();

    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    Role_Information roinfo;
    roinfo.roid = roid;

    auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
    roinfo.home_host = ::poseidon::hostname;
    roinfo.home_db = mysql_conn->service_uri();

    static constexpr char select_from_role[] =
        R"!!!(
          SELECT `username`
                 , `nickname`
                 , `update_time`
                 , `avatar`
                 , `profile`
                 , `whole`
            FROM `role`
            WHERE `roid` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(roinfo.roid);    // WHERE `roid` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                               move(mysql_conn), &select_from_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);

    if(task1->result_rows().size() == 0) {
      response_data.try_emplace(&"status", &"gs_roid_not_found");
      return;
    }

    roinfo.username = task1->result_rows().front().at(0).as_blob();            // SELECT `username`
    roinfo.nickname = task1->result_rows().front().at(1).as_blob();            //        , `nickname`
    roinfo.update_time = task1->result_rows().front().at(2).as_system_time();  //        , `update_time`
    roinfo.avatar = task1->result_rows().front().at(3).as_blob();              //        , `avatar`
    roinfo.profile = task1->result_rows().front().at(4).as_blob();             //        , `profile`
    roinfo.whole = task1->result_rows().front().at(5).as_blob();               //        , `whole`

    do_store_role_information_into_redis(fiber, roinfo, impl->redis_role_ttl);
    impl->roles.insert_or_assign(roinfo.roid, roinfo);

    POSEIDON_LOG_INFO(("Loaded role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response_data.try_emplace(&"status", &"gs_ok");
  }

void
do_store_role_information_into_mysql(::poseidon::Abstract_Fiber& fiber,
                                     uniptr<::poseidon::MySQL_Connection>&& mysql_conn_opt,
                                     Role_Information& roinfo)
  {
    static constexpr char update_role[] =
        R"!!!(
          UPDATE `role`
            SET `username` = ?
                , `nickname` = ?
                , `update_time` = ?
                , `avatar` = ?
                , `profile` = ?
                , `whole` = ?
            WHERE `roid` = ?
        )!!!";

    cow_vector<::poseidon::MySQL_Value> sql_args;
    sql_args.emplace_back(roinfo.username.rdstr());   // SET `username` = ?
    sql_args.emplace_back(roinfo.nickname);           //     , `nickname` = ?
    sql_args.emplace_back(roinfo.update_time);        //     , `update_time` = ?
    sql_args.emplace_back(roinfo.avatar);             //     , `avatar` = ?
    sql_args.emplace_back(roinfo.profile);            //     , `profile` = ?
    sql_args.emplace_back(roinfo.whole);              //     , `whole` = ?
    sql_args.emplace_back(roinfo.roid);               // WHERE `roid` = ?

    auto task1 = new_sh<::poseidon::MySQL_Query_Future>(::poseidon::mysql_connector,
                                                        move(mysql_conn_opt), &update_role, sql_args);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);
  }

void
do_slash_role_unload(const shptr<Implementation>& impl,
                     ::poseidon::Abstract_Fiber& fiber,
                     const ::poseidon::UUID& /*req_service_uuid*/,
                     ::taxon::V_object& response_data, ::taxon::V_object&& request_data)
  {
    int64_t roid = -1;

    for(const auto& r : request_data)
      if(r.first == &"roid")
        roid = r.second.as_integer();

    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"GET");
    redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roid));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->result().is_nil()) {
      impl->roles.erase(roid);
      response_data.try_emplace(&"status", &"gs_role_not_online");
      return;
    }

    // Write role information to MySQL. This is a slow operation, and data may
    // change when it is being executed. Therefore we will have to verify that
    // the value on Redis is unchanged before deleting it safely.
    Role_Information roinfo;
    roinfo.roid = roid;
    do {
      do_parse_role_information_from_string(roinfo, task2->result().as_string());

      auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
      if((roinfo.home_host != ::poseidon::hostname) || (roinfo.home_db != mysql_conn->service_uri())) {
        ::poseidon::mysql_connector.pool_connection(move(mysql_conn));
        response_data.try_emplace(&"status", &"gs_role_foreign");
        return;
      }

      do_store_role_information_into_mysql(fiber, move(mysql_conn), roinfo);
      POSEIDON_LOG_DEBUG(("Stored role `$1` (`$2`) into MySQL"), roinfo.roid, roinfo.nickname);

      static constexpr char redis_delete_if_unchanged[] =
          R"!!!(
            local value = redis.call('GET', KEYS[1])
            if value == ARGV[1] then
              redis.call('DEL', KEYS[1])
              return nil
            else
              return value
            end
          )!!!";

      redis_cmd.clear();
      redis_cmd.emplace_back(&"EVAL");
      redis_cmd.emplace_back(&redis_delete_if_unchanged);
      redis_cmd.emplace_back(&"1");   // one key
      redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roinfo.roid));  // KEYS[1]
      redis_cmd.emplace_back(task2->result().as_string());  // ARGV[1]

      task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
      ::poseidon::task_scheduler.launch(task2);
      fiber.yield(task2);
    }
    while(!task2->result().is_nil());
    impl->roles.erase(roid);

    POSEIDON_LOG_INFO(("Unloaded role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response_data.try_emplace(&"status", &"gs_ok");
  }

void
do_slash_role_flush(const shptr<Implementation>& impl,
                    ::poseidon::Abstract_Fiber& fiber,
                    const ::poseidon::UUID& /*req_service_uuid*/,
                    ::taxon::V_object& response_data, ::taxon::V_object&& request_data)
  {
    int64_t roid = -1;

    for(const auto& r : request_data)
      if(r.first == &"roid")
        roid = r.second.as_integer();

    POSEIDON_CHECK((roid >= 1) && (roid <= 8'99999'99999'99999));

    ////////////////////////////////////////////////////////////
    //
    POSEIDON_CHECK(impl->db_ready);

    cow_vector<cow_string> redis_cmd;
    redis_cmd.emplace_back(&"GETEX");
    redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roid));
    redis_cmd.emplace_back(&"EX");
    redis_cmd.emplace_back(sformat("$1", impl->redis_role_ttl.count()));

    auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);

    if(task2->result().is_nil()) {
      impl->roles.erase(roid);
      response_data.try_emplace(&"status", &"gs_role_not_online");
      return;
    }

    // Write a snapshot of role information to MySQL.
    Role_Information roinfo;
    roinfo.roid = roid;
    do_parse_role_information_from_string(roinfo, task2->result().as_string());

    auto mysql_conn = ::poseidon::mysql_connector.allocate_default_connection();
    if((roinfo.home_host != ::poseidon::hostname) || (roinfo.home_db != mysql_conn->service_uri())) {
      ::poseidon::mysql_connector.pool_connection(move(mysql_conn));
      response_data.try_emplace(&"status", &"gs_role_foreign");
      return;
    }

    impl->roles.insert_or_assign(roinfo.roid, roinfo);
    do_store_role_information_into_mysql(fiber, move(mysql_conn), roinfo);

    POSEIDON_LOG_INFO(("Flushed role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);

    response_data.try_emplace(&"status", &"gs_ok");
  }

void
do_service_timer_callback(const shptr<Implementation>& impl,
                          const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                          ::poseidon::Abstract_Fiber& fiber, steady_time /*now*/)
  {
    if(impl->db_ready == false) {
      // Check tables.
      do_mysql_check_table_role(fiber);
      impl->db_ready = true;
    }

    // Arrange online roles for writing. Initially, users are divided into 20
    // buckets. For each timer tick, one bucket is popped and written. If there
    // are no more buckets, users are divided again. The loop repeats as such.
    if(impl->save_buckets.empty()) {
      while(impl->save_buckets.size() < 20)
        impl->save_buckets.emplace_back();

      for(const auto& r : impl->roles) {
        auto first_bucket = impl->save_buckets.begin();
        if(first_bucket->size() >= first_bucket->capacity())
          first_bucket = impl->save_buckets.emplace(first_bucket);

        first_bucket->push_back(r.first);
        impl->save_buckets.splice(impl->save_buckets.end(), impl->save_buckets, first_bucket);
      }
    }

    auto bucket = move(impl->save_buckets.back());
    impl->save_buckets.pop_back();
    while(!bucket.empty()) {
      int64_t roid = bucket.back();
      bucket.pop_back();

      cow_vector<cow_string> redis_cmd;
      redis_cmd.emplace_back(&"GET");
      redis_cmd.emplace_back(sformat("$1/role/$2", service.application_name(), roid));

      auto task2 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, redis_cmd);
      ::poseidon::task_scheduler.launch(task2);
      fiber.yield(task2);

      if(task2->result().is_nil()) {
        impl->roles.erase(roid);
        continue;
      }

      // Write a snapshot of role information to MySQL.
      Role_Information roinfo;
      roinfo.roid = roid;
      do_parse_role_information_from_string(roinfo, task2->result().as_string());

      impl->roles.insert_or_assign(roinfo.roid, roinfo);
      do_store_role_information_into_mysql(fiber, nullptr, roinfo);

      POSEIDON_LOG_INFO(("Automatically flushed role `$1` (`$2`)"), roinfo.roid, roinfo.nickname);
    }
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
    ::asteria::V_integer redis_role_ttl = 3600;

    // `monitor.redis_role_ttl`
    auto conf_value = conf_file.query(&"monitor.redis_role_ttl");
    if(conf_value.is_integer())
      redis_role_ttl = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `monitor.redis_role_ttl`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((redis_role_ttl < 600) || (redis_role_ttl > 999999999))
      POSEIDON_THROW((
          "Invalid `monitor.redis_role_ttl`: value `$1` out of range",
          "[in configuration file '$2']"),
          redis_role_ttl, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->redis_role_ttl = seconds(redis_role_ttl);

    // Set up request handlers.
    service.set_handler(&"/role/list", bindw(this->m_impl, do_slash_role_list));
    service.set_handler(&"/role/create", bindw(this->m_impl, do_slash_role_create));
    service.set_handler(&"/role/load", bindw(this->m_impl, do_slash_role_load));
    service.set_handler(&"/role/unload", bindw(this->m_impl, do_slash_role_unload));
    service.set_handler(&"/role/flush", bindw(this->m_impl, do_slash_role_flush));

    // Restart the service.
    this->m_impl->service_timer.start(100ms, 11001ms, bindw(this->m_impl, do_service_timer_callback));
  }

}  // namespace k32::monitor
