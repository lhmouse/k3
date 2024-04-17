// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "service.hpp"
#include <poseidon/fiber/abstract_future.hpp>
#include <poseidon/static/task_executor.hpp>
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
set_application_name(cow_stringR name)
  {
    if(name.empty() || !all_of(name,
        [](char ch) {
          return ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z'))
                 || ((ch >= '0') && (ch <= '9'))
                 || (ch == '.') || (ch == '-') || (ch == '_') || (ch == '~');
        }))
      POSEIDON_THROW(("Invalid application name `$1`"), name);

    this->m_uuid = ::poseidon::UUID::random();
    this->m_app_name = name;
  }

void
Service::
set_private_type(cow_stringR type)
  {
    this->m_prv_type = type;
  }

void
Service::
set_private_port(uint16_t port)
  {
    this->m_prv_port = port;
  }

void
Service::
set_property(phsh_stringR name, ::taxon::Value value)
  {
    this->m_props.insert_or_assign(name, move(value));
  }

bool
Service::
unset_property(phsh_stringR name)
  {
    return this->m_props.erase(name);
  }

void
Service::
synchronize_services_with_redis(::poseidon::Abstract_Fiber& fiber, seconds ttl)
  {
    if(this->m_app_name.empty())
      POSEIDON_THROW(("Missing application name"));

    struct Redis_Task : ::poseidon::Abstract_Future
      {
        ::poseidon::UUID in_uuid;
        cow_string in_redis_key_prefix;
        cow_string in_prv_type;
        uint16_t in_prv_port;
        ::taxon::V_object in_props;
        seconds in_ttl;
        snapshot_map out_remotes;

        virtual
        void
        do_on_abstract_future_execute() override
          {
            ::taxon::Value taxon;
            ::rocket::tinyfmt_str fmt;
            vector<cow_string> keys;
            cow_string cmd[6];
            ::poseidon::Redis_Value reply;

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
              POSEIDON_LOG_TRACE(("Using local address from Redis connection: `$1`"),
                                 redis->local_address());
            }

            // Upload my service information.
            taxon.mut_object().try_emplace(&"properties", this->in_props);
            taxon.mut_object().try_emplace(&"private_type", this->in_prv_type);
            taxon.mut_object().try_emplace(&"process_id", static_cast<double>(::getpid()));

            auto sys_time = system_clock::now();
            using hires_milliseconds = ::std::chrono::duration<double, ::std::milli>;
            auto hires_dur = time_point_cast<hires_milliseconds>(sys_time).time_since_epoch();
            taxon.mut_object().try_emplace(&"timestamp", hires_dur.count());

            auto prv_addr = redis->local_address();
            prv_addr.set_port(this->in_prv_port);
            fmt << prv_addr;
            taxon.mut_object().try_emplace(&"private_address", fmt.extract_string());

            cmd[0] = &"set";
            fmt << this->in_redis_key_prefix << this->in_uuid;
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
            cmd[3] = this->in_redis_key_prefix + "*";
            do {
              redis->execute(cmd, 4);
              redis->fetch_reply(reply);

              for(const auto& r : reply.as_array().at(1).as_array())
                keys.emplace_back(r.as_string());

              cmd[1] = reply.as_array().at(0).as_string();
            } while(cmd[1] != "0");

            for(const auto& key : keys) {
              if(key.size() != this->in_redis_key_prefix.size() + 36)
                continue;

              ::poseidon::UUID uuid;
              if(uuid.parse_partial(key.data() + this->in_redis_key_prefix.size()) != 36) {
                POSEIDON_LOG_WARN(("Invalid service name `$1`"), key);
                continue;
              }

              cmd[0] = "get";
              cmd[1] = key;
              redis->execute(cmd, 2);
              redis->fetch_reply(reply);

              ::taxon::Parser_Context pctx;
              taxon.parse_with(pctx, reply.as_string());
              if(pctx.error || !taxon.is_object()) {
                POSEIDON_LOG_WARN(("Invalid service: `$1` = $2"), key, reply);
                continue;
              }

              POSEIDON_LOG_TRACE(("Received service: `$1`$3 = $2"),
                                 uuid, taxon, (uuid == this->in_uuid) ? " (self)" : "");

              this->out_remotes[uuid] = move(taxon.mut_object());
            }
          }
      };

    // This needs to be done in an asynchronous way.
    auto task = new_sh<Redis_Task>();
    task->in_uuid = this->m_uuid;
    task->in_redis_key_prefix = this->m_app_name + "/services/";
    task->in_prv_type = this->m_prv_type;
    task->in_prv_port = this->m_prv_port;
    task->in_props = this->m_props;
    task->in_ttl = ttl;

    ::poseidon::task_executor.enqueue(task);
    ::poseidon::fiber_scheduler.yield(fiber, task);

    this->m_remotes = move(task->out_remotes);
    POSEIDON_LOG_TRACE(("Finished synchronizing services: size = $1"),
                       this->m_remotes.size());
  }

}  // namespace k3
