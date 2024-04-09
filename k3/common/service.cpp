// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "service.hpp"
#include <poseidon/base/abstract_async_task.hpp>
#include <poseidon/static/async_task_executor.hpp>
#include <poseidon/fiber/abstract_future.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <poseidon/redis/redis_connection.hpp>
#include <poseidon/redis/redis_value.hpp>
#include <poseidon/static/redis_connector.hpp>
namespace k3 {

Service::
Service()
  {
  }

Service::
~Service()
  {
  }

void
Service::
set_application_name(cow_stringR app_name)
  {
    static constexpr char name_chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_~";

    if(app_name.empty() || (app_name.find_not_of(name_chars) != cow_string::npos))
      POSEIDON_THROW(("Invalid application name `$1`"), app_name);

    this->m_uuid = ::poseidon::UUID::random();
    this->m_app_name = app_name;
  }

void
Service::
set_property(phsh_stringR name, ::taxon::Value value)
  {
    this->m_properties.insert_or_assign(name, move(value));
  }

bool
Service::
unset_property(phsh_stringR name)
  {
    return this->m_properties.erase(name);
  }

void
Service::
synchronize_services_with_redis(::poseidon::Abstract_Fiber& fiber, seconds ttl)
  {
    if(this->m_app_name.empty())
      POSEIDON_THROW(("Missing application name"));

    struct Redis_Task
      : ::poseidon::Abstract_Async_Task, ::poseidon::Abstract_Future
      {
        ::poseidon::UUID in_uuid;
        cow_string in_app_name_slash;
        ::taxon::V_object in_properties;
        seconds in_ttl;
        snapshot_map out_remotes;

        virtual
        void
        do_on_abstract_async_task_execute() override
          {
            this->do_abstract_future_request();
          }

        virtual
        void
        do_on_abstract_future_execute() override
          {
            ::taxon::Value taxon;
            ::rocket::tinyfmt_str fmt;
            cow_string cmd[6];
            ::poseidon::Redis_Value reply;
            vector<cow_string> keys;
            ::poseidon::UUID uuid;
            ::taxon::Parser_Context pctx;

            // Connect to the default Redis server. The connection shall be put
            // back before this function returns.
            auto redis = ::poseidon::redis_connector.allocate_default_connection();
            const auto redis_guard = make_unique_handle(&redis,
                [&](void*) {
                  if(redis && redis->reset())
                    ::poseidon::redis_connector.pool_connection(move(redis));
                });

            if(redis->local_address() == ::poseidon::ipv6_unspecified) {
              // Ensure a socket connection has been established and the local
              // address has been set.
              cmd[0] = &"ping";
              redis->execute(cmd, 1);
              POSEIDON_LOG_DEBUG(("Local address: `$1`"), redis->local_address());
            }

            // Upload my service information.
            taxon.mut_object().try_emplace(&"timestamp", system_clock::now());
            taxon.mut_object().try_emplace(&"properties", this->in_properties);
            redis->local_address().print_to(fmt);
            taxon.mut_object().try_emplace(&"host", fmt.extract_string());
            fmt << (uint32_t) ::getpid();
            taxon.mut_object().try_emplace(&"pid", fmt.extract_string());

            cmd[0] = &"set";
            fmt << this->in_app_name_slash << this->in_uuid;
            cmd[1] = fmt.extract_string();
            taxon.print_to(cmd[2]);
            cmd[3] = &"ex";
            fmt << this->in_ttl.count();
            cmd[4] = fmt.extract_string();
            redis->execute(cmd, 5);

            // Get keys of all service.
            cmd[0] = &"scan";
            cmd[1] = &"0";
            cmd[2] = &"match";
            fmt << this->in_app_name_slash << '*';
            cmd[3] = fmt.extract_string();
            do {
              redis->execute(cmd, 4);
              redis->fetch_reply(reply);

              for(const auto& r : reply.as_array().at(1).as_array())
                keys.emplace_back(r.as_string());

              cmd[1] = reply.as_array().at(0).as_string();
            } while(cmd[1] != "0");

            for(const auto& key : keys) {
              if(!key.starts_with(this->in_app_name_slash))
                continue;

              chars_view key_suffix = key;
              key_suffix >>= this->in_app_name_slash.length();
              if(uuid.parse(key_suffix) == 0) {
                POSEIDON_LOG_WARN(("Invalid service name `$1`"), key);
                continue;
              }

              cmd[0] = "get";
              cmd[1] = key;
              redis->execute(cmd, 2);
              redis->fetch_reply(reply);

              taxon.parse_with(pctx, reply.as_string());
              if(pctx.error || !taxon.is_object()) {
                POSEIDON_LOG_WARN(("Invalid service: `$1` = $2"), key, reply);
                continue;
              }

              POSEIDON_LOG_DEBUG(("Received service: `$1`$3 = $2"),
                                 uuid, taxon,
                                 (uuid == this->in_uuid) ? " (self)" : "");

              this->out_remotes[uuid] = move(taxon.mut_object());
            }
          }
      };

    // This needs to be done in an asynchronous way.
    auto task = new_sh<Redis_Task>();
    task->in_uuid = this->m_uuid;
    task->in_app_name_slash = this->m_app_name + '/';
    task->in_properties = this->m_properties;
    task->in_ttl = ttl;

    ::poseidon::async_task_executor.enqueue(task);
    ::poseidon::fiber_scheduler.yield(fiber, task);

    this->m_remotes = move(task->out_remotes);
    POSEIDON_LOG_DEBUG(("Finished synchronizing services: size = $1"),
                       this->m_remotes.size());
  }

}  // namespace k3
