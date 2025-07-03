// This file is part of
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/base/appointment.hpp>
#include <poseidon/easy/easy_ws_server.hpp>
#include <poseidon/easy/easy_ws_client.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/fiber/redis_scan_and_get_future.hpp>
#include <poseidon/base/abstract_task.hpp>
#include <poseidon/static/task_scheduler.hpp>
#include <poseidon/http/http_query_parser.hpp>
#define OPENSSL_API_COMPAT  0x10100000L
#include <openssl/md5.h>
#include <sys/types.h>
#include <net/if.h>
#include <ifaddrs.h>
namespace k32 {
namespace {

struct Remote_Service_Connection_Record
  {
    wkptr<::poseidon::WS_Client_Session> weak_session;
    cow_bivector<wkptr<Service_Future>, ::poseidon::UUID> weak_futures;
  };

struct Implementation
  {
    cow_string service_type;
    ::poseidon::Appointment appointment;
    cow_string application_name;
    cow_string application_password;
    int zone_id = 0;
    system_time zone_start_time;

    ::poseidon::UUID service_uuid;
    system_time service_start_time;
    cow_dictionary<Service::handler_type> handlers;

    ::taxon::V_object service_data;
    ::poseidon::Easy_Timer publish_timer;
    ::poseidon::Easy_Timer subscribe_timer;
    ::poseidon::Easy_WS_Server private_server;
    ::poseidon::Easy_WS_Client private_client;

    // remote data from redis
    cow_uuid_dictionary<Service_Record> remote_services_by_uuid;
    cow_dictionary<cow_vector<Service_Record>> remote_services_by_type;

    // connections
    cow_uuid_dictionary<Remote_Service_Connection_Record> remote_connections;
    ::std::vector<::poseidon::UUID> expired_service_uuids;
  };

void
do_salt_password(char* pw, const ::poseidon::UUID& s, int64_t ts, const cow_string& password)
  {
    ::MD5_CTX ctx;
    ::MD5_Init(&ctx);

    ::MD5_Update(&ctx, s.data(), s.size());
    uint64_t be = ROCKET_HTOBE64(static_cast<uint64_t>(ts));
    ::MD5_Update(&ctx, &be, 8);
    ::MD5_Update(&ctx, password.data(), password.size());

    uint8_t checksum[16];
    ::MD5_Final(checksum, &ctx);
    ::poseidon::hex_encode_16_partial(pw, checksum);
  }

void
do_remove_remote_connection(const shptr<Implementation>& impl, const ::poseidon::UUID& service_uuid)
  {
    POSEIDON_LOG_DEBUG(("Removing service connection: service_uuid `$1`"), service_uuid);
    auto conn = move(impl->remote_connections.open(service_uuid));
    impl->remote_connections.erase(service_uuid);

    for(const auto& r : conn.weak_futures)
      if(auto req = r.first.lock()) {
        bool all_received = true;
        for(auto p = req->mf_responses().mut_begin();  p != req->mf_responses().end();  ++p)
          if(p->service_uuid != service_uuid)
            all_received &= p->complete;
          else {
            p->error = &"Connection lost";
            p->complete = true;
          }

        if(all_received)
          req->mf_abstract_future_complete();
      }
  }

void
do_set_response(const wkptr<Service_Future>& weak_req, const ::poseidon::UUID& request_uuid,
                const ::taxon::V_object& response, const cow_string& error)
  {
    if(!error.empty())
      POSEIDON_LOG_ERROR(("Received service error: $1"), error);

    auto req = weak_req.lock();
    if(!req)
      return;

    bool all_received = true;
    for(auto p = req->mf_responses().mut_begin();  p != req->mf_responses().end();  ++p)
      if(p->request_uuid != request_uuid)
        all_received &= p->complete;
      else {
        p->obj = response;
        p->error = error;
        p->complete = true;
      }

    if(all_received)
      req->mf_abstract_future_complete();
  }

struct Local_Request_Fiber final : ::poseidon::Abstract_Fiber
  {
    wkptr<Implementation> m_weak_impl;
    wkptr<Service_Future> m_weak_req;
    ::poseidon::UUID m_request_uuid;
    phcow_string m_opcode;
    ::taxon::V_object m_request;

    Local_Request_Fiber(const shptr<Implementation>& impl, const shptr<Service_Future>& req,
                        const ::poseidon::UUID& request_uuid, const phcow_string& opcode,
                        const ::taxon::V_object& request)
      :
        m_weak_impl(impl), m_weak_req(req), m_request_uuid(request_uuid),
        m_opcode(opcode), m_request(request)
      {
      }

    virtual
    void
    do_on_abstract_fiber_execute() override
      {
        const auto impl = this->m_weak_impl.lock();
        if(!impl)
          return;

        // Copy the handler, in case of fiber context switches.
        static_vector<Service::handler_type, 1> handler;
        if(auto ptr = impl->handlers.ptr(this->m_opcode))
          handler.emplace_back(*ptr);

        ::taxon::V_object response;
        tinyfmt_str error_fmt;
        try {
          if(handler.empty())
            format(error_fmt, "No handler for `$1`", this->m_opcode);
          else
            handler.front() (*this, impl->service_uuid, response, this->m_request);
        }
        catch(exception& stdex) {
          format(error_fmt, "`$1`: $2\n$3", this->m_opcode, this->m_request, stdex);
          POSEIDON_LOG_ERROR(("Unhandled exception: $1"), error_fmt.get_string());
        }

        // If the caller will be waiting, set the response.
        do_set_response(this->m_weak_req, this->m_request_uuid, response, error_fmt.get_string());
      }
  };

void
do_client_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Client_Session>& session,
                      ::poseidon::Abstract_Fiber& /*fiber*/,
                      ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_ws_open:
        POSEIDON_LOG_INFO(("Connected to `$1`: $2"), session->remote_address(), data);
        break;

      case ::poseidon::easy_ws_text:
      case ::poseidon::easy_ws_binary:
        {
          if(session->session_user_data().is_null())
            return;

          const ::poseidon::UUID target_service_uuid(session->session_user_data().as_string());

          tinybuf_ln buf(move(data));
          ::taxon::Value temp_value;
          POSEIDON_CHECK(temp_value.parse(buf));
          ::taxon::V_object response = temp_value.as_object();
          temp_value.clear();

          ::poseidon::UUID request_uuid;
          if(auto ptr = response.ptr(&"@uuid"))
            request_uuid = ::poseidon::UUID(ptr->as_string());

          cow_string error;
          if(auto ptr = response.ptr(&"@error"))
            error = ptr->as_string();

          // Set the request future.
          auto& conn = impl->remote_connections.open(target_service_uuid);
          for(const auto& r : conn.weak_futures)
            if(r.second == request_uuid) {
              do_set_response(r.first, request_uuid, response, error);
              break;
            }

          POSEIDON_LOG_TRACE(("Received response: request_uuid `$1`"), request_uuid);
          break;
        }

      case ::poseidon::easy_ws_pong:
        break;

      case ::poseidon::easy_ws_close:
        {
          if(session->session_user_data().is_null())
            return;

          const ::poseidon::UUID target_service_uuid(session->session_user_data().as_string());

          if(impl->remote_connections.at(target_service_uuid).weak_session.lock() != session)
            return;

          do_remove_remote_connection(impl, target_service_uuid);

          POSEIDON_LOG_INFO(("Disconnected from `$1`: $2"), session->remote_address(), data);
          break;
        }
    }
  }

struct Remote_Response_Task final : ::poseidon::Abstract_Task
  {
    wkptr<::poseidon::WS_Server_Session> m_weak_session;
    ::poseidon::UUID m_request_uuid;
    ::taxon::V_object m_response;
    cow_string m_error;

    Remote_Response_Task(const shptr<::poseidon::WS_Server_Session>& session,
                         const ::poseidon::UUID& request_uuid,
                         const ::taxon::V_object& response, const cow_string& error)
      :
        m_weak_session(session),
        m_request_uuid(request_uuid), m_response(response), m_error(error)
      {
      }

    virtual
    void
    do_on_abstract_task_execute() override
      {
        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        this->m_response.try_emplace(&"@uuid", this->m_request_uuid.to_string());
        if(!this->m_error.empty())
          this->m_response.try_emplace(&"@error", this->m_error);
        session->ws_send(::poseidon::ws_TEXT, ::taxon::Value(this->m_response).to_string());
      }
  };

void
do_send_remote_response(const shptr<::poseidon::WS_Server_Session>& session,
                        const ::poseidon::UUID& request_uuid,
                        const ::taxon::V_object& response, const cow_string& error)
  {
    if(request_uuid.is_nil())
      return;

    auto task4 = new_sh<Remote_Response_Task>(session, request_uuid, response, error);
    ::poseidon::task_scheduler.launch(task4);
  }

struct Remote_Request_Fiber final : ::poseidon::Abstract_Fiber
  {
    wkptr<Implementation> m_weak_impl;
    wkptr<::poseidon::WS_Server_Session> m_weak_session;
    ::poseidon::UUID m_request_uuid;
    phcow_string m_opcode;
    ::taxon::V_object m_request;

    Remote_Request_Fiber(const shptr<Implementation>& impl,
                         const shptr<::poseidon::WS_Server_Session>& session,
                         const ::poseidon::UUID& request_uuid,
                         const phcow_string& opcode, const ::taxon::V_object& request)
      :
        m_weak_impl(impl), m_weak_session(session), m_request_uuid(request_uuid),
        m_opcode(opcode), m_request(request)
      {
      }

    virtual
    void
    do_on_abstract_fiber_execute() override
      {
        const auto impl = this->m_weak_impl.lock();
        if(!impl)
          return;

        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        const ::poseidon::UUID request_service_uuid(session->session_user_data().as_string());

        // Copy the handler, in case of fiber context switches.
        static_vector<Service::handler_type, 1> handler;
        if(auto ptr = impl->handlers.ptr(this->m_opcode))
          handler.emplace_back(*ptr);

        ::taxon::V_object response;
        tinyfmt_str error_fmt;
        try {
          if(handler.empty())
            format(error_fmt, "No handler for `$1`", this->m_opcode);
          else
            handler.front() (*this, request_service_uuid, response, this->m_request);
        }
        catch(exception& stdex) {
          format(error_fmt, "`$1`: $2\n$3", this->m_opcode, this->m_request, stdex);
          POSEIDON_LOG_ERROR(("Unhandled exception: $1"), error_fmt.get_string());
        }

        // If the caller will be waiting, set the response.
        do_send_remote_response(session, this->m_request_uuid, response, error_fmt.get_string());
      }
  };

void
do_server_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Server_Session>& session,
                      ::poseidon::Abstract_Fiber& /*fiber*/,
                      ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_ws_open:
        {
          ::poseidon::Network_Reference uri;
          POSEIDON_CHECK(::poseidon::parse_network_reference(uri, data) == data.size());
          cow_string path = ::poseidon::decode_and_canonicalize_uri_path(uri.path);

          if(path.size() != 37) {
            session->ws_shut_down(::poseidon::ws_status_forbidden);
            return;
          }

          path.erase(0, 1);
          ::poseidon::UUID uuid_in_path;
          if((uuid_in_path.parse(path) != 36) || (uuid_in_path != impl->service_uuid)) {
            session->ws_shut_down(::poseidon::ws_status_forbidden);
            return;
          }

          // Check authentication.
          ::poseidon::UUID request_service_uuid;
          try {
            cow_string req_pw;
            int64_t req_ts = 0;

            ::poseidon::HTTP_Query_Parser parser;
            parser.reload(cow_string(uri.query));
            while(parser.next_element())
              if(parser.current_name() == "s")
                request_service_uuid = ::poseidon::UUID(parser.current_value().as_string());
              else if(parser.current_name() == "ts")
                req_ts = parser.current_value().as_integer();
              else if(parser.current_name() == "pw")
                req_pw = parser.current_value().as_string();

            POSEIDON_CHECK(request_service_uuid != ::poseidon::UUID());
            int64_t now = ::time(nullptr);
            POSEIDON_CHECK((req_ts >= now - 60) && (req_ts <= now + 60));
            char auth_pw[33];
            do_salt_password(auth_pw, request_service_uuid, req_ts, impl->application_password);
            POSEIDON_CHECK(req_pw == auth_pw);
          }
          catch(exception& stdex) {
            POSEIDON_LOG_ERROR(("Authentication error from `$1`: $2"), session->remote_address(), stdex);
            session->ws_shut_down(::poseidon::ws_status_unauthorized);
            return;
          }

          session->mut_session_user_data() = request_service_uuid.to_string();
          POSEIDON_LOG_INFO(("Accepted service from `$1`: $2"), session->remote_address(), data);
          break;
        }

      case ::poseidon::easy_ws_text:
      case ::poseidon::easy_ws_binary:
        {
          if(session->session_user_data().is_null())
            return;

          tinybuf_ln buf(move(data));
          ::taxon::Value temp_value;
          POSEIDON_CHECK(temp_value.parse(buf));
          ::taxon::V_object request = temp_value.as_object();
          temp_value.clear();

          phcow_string opcode;
          if(auto ptr = request.ptr(&"@opcode"))
            opcode = ptr->as_string();

          ::poseidon::UUID request_uuid;
          if(auto ptr = request.ptr(&"@uuid"))
            request_uuid = ::poseidon::UUID(ptr->as_string());

          // Handle the request in another fiber, so it's stateless.
          auto fiber3 = new_sh<Remote_Request_Fiber>(impl, session, request_uuid, opcode, request);
          ::poseidon::fiber_scheduler.launch(fiber3);
          break;
        }

      case ::poseidon::easy_ws_pong:
        break;

      case ::poseidon::easy_ws_close:
        {
          if(session->session_user_data().is_null())
            return;

          POSEIDON_LOG_INFO(("Disconnected from `$1`: $2"), session->remote_address(), data);
          break;
        }
      }
  }

struct Remote_Request_Task final : ::poseidon::Abstract_Task
  {
    wkptr<::poseidon::WS_Client_Session> m_weak_session;
    wkptr<Service_Future> m_weak_req;
    ::poseidon::UUID m_request_uuid;
    phcow_string m_opcode;
    ::taxon::V_object m_request;

    Remote_Request_Task(const shptr<::poseidon::WS_Client_Session>& session,
                        const shptr<Service_Future>& req, const ::poseidon::UUID& request_uuid,
                        const phcow_string& opcode, const ::taxon::V_object& request)
      :
        m_weak_session(session), m_weak_req(req), m_request_uuid(request_uuid),
        m_opcode(opcode), m_request(request)
      {
      }

    virtual
    void
    do_on_abstract_task_execute() override
      {
        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        this->m_request.try_emplace(&"@opcode", this->m_opcode);
        if(!this->m_weak_req.expired())
          this->m_request.try_emplace(&"@uuid", this->m_request_uuid.to_string());
        session->ws_send(::poseidon::ws_TEXT, ::taxon::Value(this->m_request).to_string());
      }
  };

void
do_subscribe_services(const shptr<Implementation>& impl,
                      const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                      ::poseidon::Abstract_Fiber& fiber, steady_time /*now*/)
  {
    cow_uuid_dictionary<Service_Record> remote_services_by_uuid;
    cow_dictionary<cow_vector<Service_Record>> remote_services_by_type;

    auto pattern = sformat("$1/service/*", impl->application_name);
    auto task2 = new_sh<::poseidon::Redis_Scan_and_Get_Future>(::poseidon::redis_connector, pattern);
    ::poseidon::task_scheduler.launch(task2);
    fiber.yield(task2);
    POSEIDON_LOG_TRACE(("Fetched $1 services"), task2->result().size());

    for(const auto& r : task2->result())
      try {
        Service_Record remote;
        POSEIDON_CHECK(r.first.size() == pattern.size() + 35);  // note `*` in pattern
        POSEIDON_CHECK(remote.service_uuid.parse_partial(r.first.data() + pattern.size() - 1) == 36);

        ::taxon::Value temp_value;
        POSEIDON_CHECK(temp_value.parse(r.second));
        ::taxon::V_object root = temp_value.as_object();
        temp_value.clear();

        if(root.at(&"application_name").as_string() != impl->application_name)
          continue;

        remote.service_type = root.at(&"service_type").as_string();
        remote.service_index = static_cast<int>(root.at(&"service_index").as_integer());

        if(auto hostname = root.ptr(&"hostname"))
          remote.hostname = hostname->as_string();

        if(auto addresses = root.ptr(&"addresses"))
          for(const auto& t : addresses->as_array())
            remote.addresses.emplace_back(t.as_string());

        if(!impl->remote_services_by_uuid.count(remote.service_uuid))
          POSEIDON_LOG_INFO(("Discovered NEW service `$1`: $2"), r.first, root);

        remote_services_by_uuid.try_emplace(remote.service_uuid, remote);
        remote_services_by_type.open(remote.service_type).emplace_back(remote);
      }
      catch(exception& stdex) {
        POSEIDON_LOG_WARN(("Invalid service `$1`: $2"), r.first, stdex);
        continue;
      }

    for(const auto& r : impl->remote_services_by_uuid)
      if(!remote_services_by_uuid.count(r.first))
        POSEIDON_LOG_INFO(("Forgot DOWN service `$1`: $2"), r.first, r.second.service_type);

    // Update services as an atomic operation.
    impl->remote_services_by_uuid = remote_services_by_uuid;
    impl->remote_services_by_type = remote_services_by_type;

    // Purge connections that have been lost, as well as services that have
    // been removed from Redis.
    for(const auto& r : impl->remote_connections) {
      auto session = r.second.weak_session.lock();
      if(session && impl->remote_services_by_uuid.count(r.first))
        continue;

      if(session)
        session->ws_shut_down(::poseidon::ws_status_normal);

      POSEIDON_LOG_INFO(("Purging expired service `$1`"), r.first);
      impl->expired_service_uuids.emplace_back(r.first);
    }

    while(impl->expired_service_uuids.size() != 0) {
      ::poseidon::UUID service_uuid = impl->expired_service_uuids.back();
      impl->expired_service_uuids.pop_back();

      do_remove_remote_connection(impl, service_uuid);
    }
  }

void
do_publish_service(const shptr<Implementation>& impl,
                   const shptr<::poseidon::Abstract_Timer>& timer,
                   ::poseidon::Abstract_Fiber& fiber, steady_time now)
  {
    if(impl->appointment.index() < 0)
      return;

    if(impl->service_start_time == system_time())
      impl->service_start_time = system_clock::now();

    impl->service_data.insert_or_assign(&"service_type", impl->service_type);
    impl->service_data.insert_or_assign(&"service_index", impl->appointment.index());
    impl->service_data.insert_or_assign(&"application_name", impl->application_name);
    impl->service_data.insert_or_assign(&"zone_id", impl->zone_id);
    impl->service_data.insert_or_assign(&"zone_start_time", impl->zone_start_time);

    auto private_addr = impl->private_server.local_address();
    if(private_addr.port() != 0) {
      // Get all running network interfaces.
      ::ifaddrs* ifa;
      if(::getifaddrs(&ifa) != 0) {
        POSEIDON_LOG_ERROR(("Network configuration error: ${errno:full}]"));
        return;
      }

      const auto ifa_guard = ::rocket::make_unique_handle(ifa, ::freeifaddrs);

      impl->service_data.insert_or_assign(&"hostname", ::poseidon::hostname);
      ::taxon::V_array& addresses = impl->service_data.open(&"addresses").open_array();
      addresses.clear();

      for(ifa = ifa_guard;  ifa;  ifa = ifa->ifa_next)
        if(!(ifa->ifa_flags & IFF_RUNNING) || !ifa->ifa_addr)
          continue;
        else if(ifa->ifa_addr->sa_family == AF_INET) {
          // IPv4
          auto sa = reinterpret_cast<::sockaddr_in*>(ifa->ifa_addr);
          ::poseidon::IPv6_Address addr;
          ::memcpy(addr.mut_data(), ::poseidon::ipv4_unspecified.data(), 16);
          ::memcpy(addr.mut_data() + 12, &(sa->sin_addr), 4);
          addr.set_port(private_addr.port());
          addresses.emplace_back(addr.to_string());
        }
        else if(ifa->ifa_addr->sa_family == AF_INET6) {
          // IPv6
          auto sa = reinterpret_cast<::sockaddr_in6*>(ifa->ifa_addr);
          ::poseidon::IPv6_Address addr;
          addr.set_addr(sa->sin6_addr);
          addr.set_port(private_addr.port());
          addresses.emplace_back(addr.to_string());
        }
    }

    cow_vector<cow_string> cmd;
    cmd.emplace_back(&"SET");
    cmd.emplace_back(sformat("$1/service/$2", impl->application_name, impl->service_uuid));
    cmd.emplace_back(::taxon::Value(impl->service_data).to_string());
    cmd.emplace_back(&"EX");
    cmd.emplace_back("10");

    auto task1 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, cmd);
    ::poseidon::task_scheduler.launch(task1);
    fiber.yield(task1);
    POSEIDON_CHECK(task1->status() == "OK");
    POSEIDON_LOG_TRACE(("Published service `$1`: $2"), cmd.at(1), cmd.at(2));

    if(impl->service_start_time - system_clock::now() <= 60s)
      do_subscribe_services(impl, timer, fiber, now);
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Service,
    Implementation);

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
add_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.try_emplace(opcode, handler);
    if(!r.second)
      POSEIDON_THROW(("A handler for `$1` already exists"), opcode);
  }

bool
Service::
set_handler(const phcow_string& opcode, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.insert_or_assign(opcode, handler);
    return r.second;
  }

bool
Service::
remove_handler(const phcow_string& opcode) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->handlers.erase(opcode);
  }

const ::poseidon::UUID&
Service::
service_uuid() const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::UUID::min();

    return this->m_impl->service_uuid;
  }

const cow_string&
Service::
service_type() const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::empty_cow_string;

    return this->m_impl->service_type;
  }

int
Service::
service_index() const noexcept
  {
    if(!this->m_impl)
      return 0;

    return this->m_impl->appointment.index();
  }

const cow_string&
Service::
application_name() const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::empty_cow_string;

    return this->m_impl->application_name;
  }

int
Service::
zone_id() const noexcept
  {
    if(!this->m_impl)
      return 0;

    return this->m_impl->zone_id;
  }

system_time
Service::
zone_start_time() const noexcept
  {
    if(!this->m_impl)
      return system_time();

    return this->m_impl->zone_start_time;
  }

const Service_Record&
Service::
find_remote_service(const ::poseidon::UUID& remote_service_uuid) const noexcept
  {
    if(!this->m_impl)
      return Service_Record::null;

    auto ptr = this->m_impl->remote_services_by_uuid.ptr(remote_service_uuid);
    if(!ptr)
      return Service_Record::null;

    return *ptr;
  }

void
Service::
reload(const ::poseidon::Config_File& conf_file, const cow_string& service_type)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    ::asteria::V_string application_name, application_password, lock_directory;
    int64_t zone_id = 0;
    ::poseidon::DateTime zone_start_time;

    // `application_name`
    auto conf_value = conf_file.query(&"application_name");
    if(conf_value.is_string())
      application_name = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `application_name`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if(application_name.empty())
      POSEIDON_THROW((
          "Invalid `application_name`: empty name not valid",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    for(char ch : application_name) {
      static constexpr char valid_chars[] =
         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _+-,.()~!@#$%";

      if(::rocket::xmemchr(valid_chars, ch, sizeof(valid_chars) - 1) == nullptr)
        POSEIDON_THROW((
            "Invalid `application_name`: character `$1` not allowed",
            "[in configuration file '$2']"),
            ch, conf_file.path());
    }

    // `application_password`
    conf_value = conf_file.query(&"application_password");
    if(conf_value.is_string())
      application_password = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `application_password`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    // `zone_id`
    conf_value = conf_file.query(&"zone_id");
    if(conf_value.is_integer())
      zone_id = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `zone_id`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((zone_id < 1) || (zone_id > 99999999))
      POSEIDON_THROW((
          "Invalid `zone_id`: value `$1` out of range",
          "[in configuration file '$2']"),
          zone_id, conf_file.path());

    // `zone_start_time`
    conf_value = conf_file.query(&"zone_start_time");
    if(conf_value.is_string())
      zone_start_time = ::poseidon::DateTime(conf_value.as_string());
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `zone_start_time`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if(zone_start_time.as_system_time() > system_clock::now())
      POSEIDON_THROW((
          "Invalid `zone_start_time`: value `$1` out of range",
          "[in configuration file '$2']"),
          zone_start_time, conf_file.path());

    // `lock_directory`
    conf_value = conf_file.query(&"lock_directory");
    if(conf_value.is_string())
      lock_directory = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `lock_directory`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->service_type = service_type;
    this->m_impl->application_name = application_name;
    this->m_impl->application_password = application_password;
    this->m_impl->zone_id = static_cast<int>(zone_id);
    this->m_impl->zone_start_time = zone_start_time.as_system_time();

    // Set up constants.
    if(this->m_impl->service_uuid.is_nil())
      this->m_impl->service_uuid = ::poseidon::UUID::random();

    if(this->m_impl->appointment.index() == -1)
      this->m_impl->appointment.enroll(sformat("$1/$2.lock", lock_directory, service_type));

    // Restart the service.
    this->m_impl->publish_timer.start(1500ms, 6101ms, bindw(this->m_impl, do_publish_service));
    this->m_impl->subscribe_timer.start(31001ms, bindw(this->m_impl, do_subscribe_services));
    this->m_impl->private_server.start(0, bindw(this->m_impl, do_server_ws_callback));
  }

void
Service::
launch(const shptr<Service_Future>& req)
  {
    if(!req)
      POSEIDON_THROW(("Null request pointer"));

    if(!this->m_impl)
      POSEIDON_THROW(("Service not initialized"));

    // Collect target services.
    req->mf_responses().clear();
    if(req->target_service_uuid() == loopback_uuid) {
      // Add myself.
      req->mf_responses().emplace_back().service_uuid = this->m_impl->service_uuid;
    }
    else if(req->target_service_uuid() == multicast_uuid) {
      // Add all matching services.
      auto psv = this->m_impl->remote_services_by_type.ptr(req->target_service_type());
      if(psv && !psv->empty()) {
        req->mf_responses().reserve(psv->size());
        for(const auto& s : *psv)
          req->mf_responses().emplace_back().service_uuid = s.service_uuid;
      }
    }
    else if(req->target_service_uuid() == randomcast_uuid) {
      // Add a random service from multicast list.
      auto psv = this->m_impl->remote_services_by_type.ptr(req->target_service_type());
      if(psv && !psv->empty()) {
        size_t k = ::rocket::probe_origin(psv->size(), static_cast<size_t>(::random()));
        req->mf_responses().emplace_back().service_uuid = psv->at(k).service_uuid;
      }
    }
    else if(req->target_service_uuid() == broadcast_uuid) {
      // Add all services.
      req->mf_responses().reserve(this->m_impl->remote_services_by_uuid.size());
      for(const auto& r : this->m_impl->remote_services_by_uuid)
        req->mf_responses().emplace_back().service_uuid = r.second.service_uuid;
    }
    else {
      // Do unicast.
      auto ps = this->m_impl->remote_services_by_uuid.ptr(req->target_service_uuid());
      if(ps)
        req->mf_responses().emplace_back().service_uuid = ps->service_uuid;
    }

    if(req->mf_responses().size() == 0)
      req->mf_abstract_future_complete();
    else
      for(size_t k = 0;  k != req->mf_responses().size();  ++k) {
        auto& resp = req->mf_responses().mut(k);
        resp.request_uuid = ::poseidon::UUID::random();

        if(resp.service_uuid == this->m_impl->service_uuid) {
          // This is myself, so there's no need to send it over network.
          auto fiber3 = new_sh<Local_Request_Fiber>(this->m_impl, req, resp.request_uuid,
                                                    req->opcode(), req->request());
          ::poseidon::fiber_scheduler.launch(fiber3);
        }
        else {
          // Send the request asynchronously.
          const auto& srv = this->m_impl->remote_services_by_uuid.at(resp.service_uuid);
          auto& conn = this->m_impl->remote_connections.open(srv.service_uuid);
          auto session = conn.weak_session.lock();
          if(!session) {
            // Find an address to connect to. If the address is loopback, it shall
            // only be accepted if the target service is on the same machine, and in
            // this case it takes precedence over a private address.
            const ::poseidon::IPv6_Address* use_addr = nullptr;
            for(const auto& addr : srv.addresses)
              if(addr.classify() != ::poseidon::ip_address_loopback)
                use_addr = &addr;
              else if(srv.hostname == ::poseidon::hostname) {
                use_addr = &addr;
                break;
              }

            if(!use_addr) {
              POSEIDON_LOG_ERROR(("No viable address to service `$1`"), srv.service_uuid);
              resp.error = &"No viable address";
              continue;
            }

            tinyfmt_str saddr_fmt;
            format(saddr_fmt, "$1/$2?s=$3", *use_addr, srv.service_uuid, this->m_impl->service_uuid);
            int64_t now = ::time(nullptr);
            format(saddr_fmt, "&ts=$1", now);
            char auth_pw[33];
            do_salt_password(auth_pw, this->m_impl->service_uuid, now, this->m_impl->application_password);
            format(saddr_fmt, "&pw=$1", auth_pw);

            cow_string saddr = saddr_fmt.get_string();
            session = this->m_impl->private_client.connect(saddr, bindw(this->m_impl, do_client_ws_callback));

            session->mut_session_user_data() = srv.service_uuid.to_string();
            conn.weak_session = session;
            POSEIDON_LOG_INFO(("Connecting to `$1`: use_addr = $2"), srv.service_uuid, *use_addr);
          }

          // Add this future to the waiting list.
          size_t index = SIZE_MAX;
          for(size_t b = 0;  b != conn.weak_futures.size();  ++b)
            if(conn.weak_futures[b].first.expired()) {
              index = b;
              break;
            }

          if(index == SIZE_MAX)
            conn.weak_futures.emplace_back(req, resp.request_uuid);
          else
            conn.weak_futures.mut(index) = ::std::make_pair(req, resp.request_uuid);

          // Send and wait.
          auto task2 = new_sh<Remote_Request_Task>(session, req, resp.request_uuid,
                                                   req->opcode(), req->request());
          ::poseidon::task_scheduler.launch(task2);
        }
      }
  }

}  // namespace k32
