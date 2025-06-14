// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_DARK_MAGIC_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_ws_server.hpp>
#include <poseidon/easy/easy_ws_client.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/abstract_fiber.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/fiber/redis_scan_and_get_future.hpp>
#include <poseidon/base/abstract_task.hpp>
#include <poseidon/static/task_executor.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <poseidon/http/http_query_parser.hpp>
#define OPENSSL_API_COMPAT  0x10100000L
#include <openssl/md5.h>
#include <sys/types.h>
#include <net/if.h>
#include <ifaddrs.h>
namespace k32 {
namespace {

struct Remote_Service_Information
  {
    ::poseidon::UUID service_uuid;
    cow_string service_type;
    cow_string hostname;
    cow_vector<::poseidon::IPv6_Address> addresses;
  };

struct Remote_Connection_Information
  {
    wkptr<::poseidon::WS_Client_Session> weak_session;
    cow_bivector<wkptr<Service_Future>, ::poseidon::UUID> weak_futures;
  };

struct Implementation
  {
    ::poseidon::UUID service_uuid = ::poseidon::UUID::random();
    cow_dictionary<Service::handler_type> handlers;

    ::poseidon::Easy_WS_Server private_server;
    ::poseidon::Easy_WS_Client private_client;
    ::poseidon::Easy_Timer publish_timer;
    ::poseidon::Easy_Timer subscribe_timer;

    // local data
    cow_string service_type;
    cow_string application_name;
    cow_string application_password;
    ::taxon::V_object cached_service_data;

    // remote data from redis
    cow_uuid_dictionary<Remote_Service_Information> remote_services_by_uuid;
    cow_dictionary<cow_vector<Remote_Service_Information>> remote_services_by_type;

    // connections
    cow_uuid_dictionary<Remote_Connection_Information> remote_connections_by_uuid;
    cow_vector<::poseidon::UUID> expired_service_uuids;
  };

void
do_salt_password(char* pw, int64_t timestamp, const cow_string& password)
  {
    ::MD5_CTX ctx;
    ::MD5_Init(&ctx);

    uint64_t ts_bytes = ROCKET_HTOBE64(static_cast<uint64_t>(timestamp));
    ::MD5_Update(&ctx, &ts_bytes, 8);
    ::MD5_Update(&ctx, password.data(), password.size());

    unsigned char checksum[16];
    ::MD5_Final(checksum, &ctx);
    ::poseidon::hex_encode_16_partial(pw, checksum);
  }

struct Send_Request_Task final : ::poseidon::Abstract_Task
  {
    shptr<Send_Request_Task> m_self_lock;
    wkptr<::poseidon::WS_Client_Session> m_weak_session;
    cow_string m_opcode;
    ::taxon::Value m_request_data;
    wkptr<Service_Future> m_weak_req;
    ::poseidon::UUID m_request_uuid;

    Send_Request_Task(const shptr<::poseidon::WS_Client_Session>& session,
                      const cow_string& opcode, const ::taxon::Value& request_data,
                      const shptr<Service_Future>& req, const ::poseidon::UUID& request_uuid)
      :
        m_weak_session(session),
        m_opcode(opcode), m_request_data(request_data), m_weak_req(req),
        m_request_uuid(request_uuid)
      {
      }

    virtual
    void
    do_on_abstract_task_execute() override
      {
        this->m_self_lock.reset();
        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        ::taxon::Value root;
        root.mut_object()[&"code"] = this->m_opcode;

        if(!this->m_request_data.is_null())
          root.mut_object()[&"data"] = this->m_request_data;

        if(!this->m_weak_req.expired())
          root.mut_object()[&"uuid"] = this->m_request_uuid.print_to_string();

        session->ws_send(::poseidon::websocket_TEXT, root.print_to_string());
      }
  };

struct Send_Response_Task final : ::poseidon::Abstract_Task
  {
    shptr<Send_Response_Task> m_self_lock;
    wkptr<::poseidon::WS_Server_Session> m_weak_session;
    ::poseidon::UUID m_request_uuid;
    cow_string m_error;
    ::taxon::Value m_response_data;

    Send_Response_Task(const shptr<::poseidon::WS_Server_Session>& session,
                       const ::poseidon::UUID& request_uuid, const cow_string& error,
                       const ::taxon::Value& response_data)
      :
        m_weak_session(session),
        m_request_uuid(request_uuid), m_error(error), m_response_data(response_data)
      {
      }

    virtual
    void
    do_on_abstract_task_execute() override
      {
        this->m_self_lock.reset();
        const auto session = this->m_weak_session.lock();
        if(!session)
          return;

        ::taxon::Value root;
        root.mut_object()[&"uuid"] = this->m_request_uuid.print_to_string();

        if(this->m_error != "")
          root.mut_object()[&"error"] = this->m_error;

        if(!this->m_response_data.is_null())
          root.mut_object()[&"data"] = this->m_response_data;

        session->ws_send(::poseidon::websocket_TEXT, root.print_to_string());
      }
  };

struct Serve_Request_Fiber final : ::poseidon::Abstract_Fiber
  {
    wkptr<Implementation> m_weak_impl;
    wkptr<::poseidon::WS_Server_Session> m_weak_session;
    ::poseidon::UUID m_request_uuid;
    cow_string m_opcode;
    ::taxon::Value m_request_data;

    Serve_Request_Fiber(const shptr<Implementation>& impl,
                        const shptr<::poseidon::WS_Server_Session>& session,
                        const ::poseidon::UUID& request_uuid,
                        const cow_string& opcode, const ::taxon::Value& request_data)
      :
        m_weak_impl(impl), m_weak_session(session),
        m_request_uuid(request_uuid), m_opcode(opcode),
        m_request_data(request_data)
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

        ::taxon::Value response_data;
        ::rocket::tinyfmt_str error_fmt;

        try {
          auto handler = impl->handlers.ptr(this->m_opcode);
          if(handler)
            (* handler) (*this, response_data, move(this->m_request_data));
          else
            format(error_fmt, "No handler defined for `$1`", this->m_opcode);
        }
        catch(exception& stdex) {
          POSEIDON_LOG_ERROR(("Unhandled exception: $2"), this->m_opcode, stdex);
          format(error_fmt, "$1", stdex);
        }

        if(this->m_request_uuid.is_nil())
          return;

        // Send a response asynchronously.
        auto task4 = new_sh<Send_Response_Task>(session,
                       this->m_request_uuid, error_fmt.get_string(), response_data);
        ::poseidon::task_executor.enqueue(task4);
        task4->m_self_lock = task4;
      }
  };

void
do_server_ws_callback(const shptr<Implementation>& impl,
                      const shptr<::poseidon::WS_Server_Session>& session,
                      ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    switch(static_cast<uint32_t>(event))
      {
      case ::poseidon::easy_ws_open:
        {
          // Authenticate.
          int64_t req_timestamp = 0;
          cow_string req_auth_password;

          cow_string query;
          query.assign(data.begin(), data.end());

          size_t pos = query.find('?');
          if(pos != cow_string::npos)
            query.erase(0, pos + 1);

          pos = query.find('#');
          if(pos != cow_string::npos)
            query.erase(pos);

          ::poseidon::HTTP_Query_Parser parser;
          parser.reload(query);

          while(parser.next_element())
            if(parser.current_name() == "t")
              req_timestamp = parser.current_value().as_integer();
            else if(parser.current_name() == "pw")
              req_auth_password = parser.current_value().as_string();

          int64_t now = ::time(nullptr);
          POSEIDON_CHECK((req_timestamp >= now - 60) && (req_timestamp <= now + 60));

          char auth_password[33];
          do_salt_password(auth_password, req_timestamp, impl->application_password);
          if(req_auth_password != auth_password) {
            POSEIDON_LOG_WARN(("Unauthenticated connection from `$1`"), session->remote_address());
            session->close();
            return;
          }
        }

        POSEIDON_LOG_INFO(("Accepted service from `$1`: $2"), session->remote_address(), data);
        break;

      case ::poseidon::easy_ws_text:
      case ::poseidon::easy_ws_binary:
        {
          // Parse the request object.
          ::taxon::Value root;
          ::taxon::Parser_Context pctx;
          ::rocket::tinybuf_ln buf(move(data));
          root.parse_with(pctx, buf);
          POSEIDON_CHECK(!pctx.error);
          POSEIDON_CHECK(root.is_object());

          cow_string opcode;
          ::taxon::Value request_data;
          ::poseidon::UUID request_uuid;

          auto sub = root.as_object().ptr(&"code");
          if(sub && sub->is_string())
            opcode = sub->as_string();

          sub = root.as_object().ptr(&"data");
          if(sub)
            request_data = *sub;

          sub = root.as_object().ptr(&"uuid");
          if(sub && sub->is_string())
            request_uuid.parse(sub->as_string());

          // Handle the request in another fiber, so it's stateless.
          POSEIDON_CHECK(opcode != "");
          auto fiber3 = new_sh<Serve_Request_Fiber>(impl, session,
                                request_uuid, opcode, request_data);
          ::poseidon::fiber_scheduler.launch(fiber3);
        }
        break;

      case ::poseidon::easy_ws_close:
        POSEIDON_LOG_INFO(("Disconnected from `$1`: $2"), session->remote_address(), data);
        break;
      }
  }

void
do_remove_remote_connection(const shptr<Implementation>& impl, const ::poseidon::UUID& service_uuid)
  {
    POSEIDON_LOG_DEBUG(("Removing service connection: service_uuid `$1`"), service_uuid);
    auto conn = move(impl->remote_connections_by_uuid[service_uuid]);
    impl->remote_connections_by_uuid.erase(service_uuid);

    for(const auto& r : conn.weak_futures)
      if(auto req = r.first.lock()) {
          bool all_received = true;
          auto& rsv = req->mf_responses();
          for(auto p = rsv.mut_begin();  p != rsv.end();  ++p)
            if(p->service_uuid != service_uuid)
              all_received &= p->response_received;
            else {
              p->error = &"Connection lost";
              p->response_received = true;
            }

          if(all_received)
            req->mf_abstract_future_complete();
      }
  }

void
do_client_ws_callback(const shptr<Implementation>& impl, const ::poseidon::UUID& service_uuid,
                      const shptr<::poseidon::WS_Client_Session>& session,
                      ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    switch(static_cast<uint32_t>(event))
      {
      case ::poseidon::easy_ws_open:
        POSEIDON_LOG_INFO(("Connected to `$1`: $2"), session->remote_address(), data);
        break;

      case ::poseidon::easy_ws_text:
      case ::poseidon::easy_ws_binary:
        {
          // Parse the response object.
          ::taxon::Value root;
          ::taxon::Parser_Context pctx;
          ::rocket::tinybuf_ln buf(move(data));
          root.parse_with(pctx, buf);
          POSEIDON_CHECK(!pctx.error);
          POSEIDON_CHECK(root.is_object());

          ::poseidon::UUID request_uuid;
          ::taxon::Value response_data;
          cow_string error;

          auto sub = root.as_object().ptr(&"uuid");
          if(sub && sub->is_string())
            request_uuid.parse(sub->as_string());

          sub = root.as_object().ptr(&"data");
          if(sub)
            response_data = *sub;

          sub = root.as_object().ptr(&"error");
          if(sub && sub->is_string())
            error = sub->as_string();

          // Find the request future.
          auto& conn = impl->remote_connections_by_uuid[service_uuid];
          shptr<Service_Future> req;
          for(const auto& r : conn.weak_futures)
            if(r.second == request_uuid) {
              req = r.first.lock();
              break;
            }

          if(!req)
            return;

          // Complete the response. If all responses have been completed,
          // also complete the request future.
          bool all_received = true;
          auto& rsv = req->mf_responses();
          for(auto p = rsv.mut_begin();  p != rsv.end();  ++p)
            if(p->request_uuid != request_uuid)
              all_received &= p->response_received;
            else {
              p->response_data = response_data;
              p->error = error;
              p->response_received = true;
            }

          if(all_received)
            req->mf_abstract_future_complete();
        }
        break;

      case ::poseidon::easy_ws_close:
        // Remove connection info.
        do_remove_remote_connection(impl, service_uuid);

        POSEIDON_LOG_INFO(("Disconnected from `$1`: $2"), session->remote_address(), data);
        break;
    }
  }

void
do_subscribe_service(const shptr<Implementation>& impl, ::poseidon::Abstract_Fiber& fiber)
  {
    auto pattern = sformat("$1/services/*", impl->application_name);
    auto task2 = new_sh<::poseidon::Redis_Scan_and_Get_Future>(::poseidon::redis_connector, pattern);
    ::poseidon::task_executor.enqueue(task2);
    ::poseidon::fiber_scheduler.yield(fiber, task2);
    POSEIDON_LOG_TRACE(("Fetched $1 services"), task2->result().size());

    cow_uuid_dictionary<Remote_Service_Information> remote_services_by_uuid;
    cow_dictionary<cow_vector<Remote_Service_Information>> remote_services_by_type;

    for(const auto& r : task2->result())
      try {
        Remote_Service_Information remote;
        POSEIDON_CHECK(r.first.size() == pattern.size() + 35);  // note `*` in pattern
        POSEIDON_CHECK(remote.service_uuid.parse_partial(r.first.data() + pattern.size() - 1) == 36);

        ::taxon::Value root;
        ::taxon::Parser_Context pctx;
        root.parse_with(pctx, r.second);
        POSEIDON_CHECK(!pctx.error);
        POSEIDON_CHECK(root.is_object());

        if(root.as_object().at(&"application_name").as_string() != impl->application_name)
          continue;

        ::taxon::V_object service_data = root.as_object();
        remote.service_type = service_data.at(&"service_type").as_string();

        if(auto hostname = service_data.ptr(&"hostname"))
          remote.hostname = hostname->as_string();

        if(auto addresses = service_data.ptr(&"addresses"))
          for(const auto& t : addresses->as_array()) {
            ::poseidon::IPv6_Address addr;
            if(addr.parse(t.as_string()))
              remote.addresses.emplace_back(addr);
          }

        remote_services_by_uuid[remote.service_uuid] = remote;
        remote_services_by_type[remote.service_type].emplace_back(remote);
      }
      catch(exception& stdex) {
        POSEIDON_LOG_WARN((
            "Invalid service `$1`: $2",
            "Service information from Redis could not be parsed: $3"),
            r.first, r.second, stdex);
      }

    // Update services as an atomic operation.
    impl->remote_services_by_uuid = remote_services_by_uuid;
    impl->remote_services_by_type = remote_services_by_type;

    // Purge connections that have been lost, as well as services that have
    // been removed from Redis.
    for(const auto& r : impl->remote_connections_by_uuid) {
      auto session = r.second.weak_session.lock();
      if(session && impl->remote_services_by_uuid.count(r.first))
        continue;

      if(session)
        session->close();

      POSEIDON_LOG_INFO(("Purging expired service `$1`"), r.first);
      impl->expired_service_uuids.emplace_back(r.first);
    }

    while(impl->expired_service_uuids.size() != 0) {
      do_remove_remote_connection(impl, impl->expired_service_uuids.back());
      impl->expired_service_uuids.pop_back();
    }
  }

void
do_publish_service_with_ttl(const shptr<Implementation>& impl,
                            ::poseidon::Abstract_Fiber& fiber, seconds ttl)
  {
    ::taxon::V_object& service_data = impl->cached_service_data;
    service_data[&"application_name"] = impl->application_name;
    service_data[&"service_type"] = impl->service_type;

    auto private_addr = impl->private_server.local_address();
    if(private_addr.port() != 0) {
      // Get all running network interfaces.
      ::ifaddrs* ifa;
      if(::getifaddrs(&ifa) != 0) {
        POSEIDON_LOG_ERROR(("Network configuration error: ${errno:full}]"));
        return;
      }

      const auto ifa_guard = ::rocket::make_unique_handle(ifa, ::freeifaddrs);

      service_data[&"hostname"] = ::poseidon::hostname;
      ::taxon::V_array& addresses = service_data[&"addresses"].mut_array();
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
          addresses.emplace_back(addr.print_to_string());
        }
        else if(ifa->ifa_addr->sa_family == AF_INET6) {
          // IPv6
          auto sa = reinterpret_cast<::sockaddr_in6*>(ifa->ifa_addr);
          ::poseidon::IPv6_Address addr;
          addr.set_addr(sa->sin6_addr);
          addr.set_port(private_addr.port());
          addresses.emplace_back(addr.print_to_string());
        }
    }

    cow_vector<cow_string> cmd;
    cmd.reserve(6);
    cmd.emplace_back(&"SET");
    cmd.emplace_back(sformat("$1/services/$2", impl->application_name, impl->service_uuid));
    cmd.emplace_back(::taxon::Value(service_data).print_to_string());
    cmd.emplace_back(&"NX");
    cmd.emplace_back(&"EX");
    cmd.emplace_back(sformat("$1", ttl.count()));

    auto task1 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, cmd);
    ::poseidon::task_executor.enqueue(task1);
    ::poseidon::fiber_scheduler.yield(fiber, task1);
    POSEIDON_LOG_TRACE(("Published service `$1`: $2"), cmd.at(1), cmd.at(2));

    if(impl->remote_services_by_uuid.count(impl->service_uuid) == false)
      do_subscribe_service(impl, fiber);
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

const ::poseidon::UUID&
Service::
service_uuid() const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::UUID::min();

    return this->m_impl->service_uuid;
  }

void
Service::
add_handler(const phcow_string& code, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.try_emplace(code, handler);
    if(!r.second)
      POSEIDON_THROW(("A handler for `$1` already exists"), code);
  }

bool
Service::
set_handler(const phcow_string& code, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    auto r = this->m_impl->handlers.insert_or_assign(code, handler);
    return r.second;
  }

bool
Service::
remove_handler(const phcow_string& code) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->handlers.erase(code);
  }

void
Service::
reload(const ::poseidon::Config_File& conf_file, const cow_string& service_type)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    cow_string application_name, application_password;

    // `application_name`
    auto conf_value = conf_file.query(&"k32.application_name");
    if(conf_value.is_string())
      application_name = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.application_name`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if(application_name.empty())
      POSEIDON_THROW((
          "Invalid `k32.application_name`: empty name not valid",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    for(char ch : application_name) {
      static constexpr char valid_chars[] =
         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _+-,.()~!@#$%";

      if(::rocket::xmemchr(valid_chars, ch, sizeof(valid_chars) - 1) == nullptr)
        POSEIDON_THROW((
            "Invalid `k32.application_name`: character `$1` not allowed",
            "[in configuration file '$2']"),
            ch, conf_file.path());
    }

    // `application_password`
    conf_value = conf_file.query(&"k32.application_password");
    if(conf_value.is_string())
      application_password = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.application_password`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->service_type = service_type;
    this->m_impl->application_name = application_name;
    this->m_impl->application_password = application_password;

    // Restart the service.
    this->m_impl->private_server.start_any(
         0,  // any port
         ::poseidon::Easy_WS_Server::callback_type(
            [weak_impl = wkptr<Implementation>(this->m_impl)]
               (const shptr<::poseidon::WS_Server_Session>& session,
                ::poseidon::Abstract_Fiber& /*fiber*/,
                ::poseidon::Easy_WS_Event event, linear_buffer&& data)
              {
                if(const auto impl = weak_impl.lock())
                  do_server_ws_callback(impl, session, event, move(data));
              }));

    this->m_impl->publish_timer.start(
         1s, 5s,
         ::poseidon::Easy_Timer::callback_type(
            [weak_impl = wkptr<Implementation>(this->m_impl)]
               (const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                ::poseidon::Abstract_Fiber& fiber,
                ::std::chrono::steady_clock::time_point /*now*/)
              {
                if(const auto impl = weak_impl.lock())
                  do_publish_service_with_ttl(impl, fiber, 10s);
              }));

    this->m_impl->subscribe_timer.start(
         6s, 30s,
         ::poseidon::Easy_Timer::callback_type(
            [weak_impl = wkptr<Implementation>(this->m_impl)]
               (const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                ::poseidon::Abstract_Fiber& fiber,
                ::std::chrono::steady_clock::time_point /*now*/)
              {
                if(const auto impl = weak_impl.lock())
                  do_subscribe_service(impl, fiber);
              }));
  }

void
Service::
enqueue(const shptr<Service_Future>& req)
  {
    if(!this->m_impl)
      POSEIDON_THROW(("Service not initialized"));

    req->m_responses.clear();
    if(req->m_target_service_uuid == multicast_uuid) {
      // Add all matching services.
      auto psv = this->m_impl->remote_services_by_type.ptr(req->m_target_service_type);
      if(psv && !psv->empty()) {
        req->m_responses.reserve(psv->size());
        for(const auto& s : *psv)
          req->m_responses.emplace_back().service_uuid = s.service_uuid;
      }
    }
    else if(req->m_target_service_uuid == randomcast_uuid) {
      // Add a random service from multicast list.
      auto psv = this->m_impl->remote_services_by_type.ptr(req->m_target_service_type);
      if(psv && !psv->empty()) {
        size_t k = ::rocket::probe_origin(psv->size(), static_cast<size_t>(::random()));
        req->m_responses.emplace_back().service_uuid = psv->at(k).service_uuid;
      }
    }
    else if(req->m_target_service_uuid == broadcast_uuid) {
      // Add all services.
      req->m_responses.reserve(this->m_impl->remote_services_by_uuid.size());
      for(const auto& r : this->m_impl->remote_services_by_uuid)
        req->m_responses.emplace_back().service_uuid = r.second.service_uuid;
    }
    else {
      // Do unicast.
      auto ps = this->m_impl->remote_services_by_uuid.ptr(req->m_target_service_uuid);
      if(ps)
        req->m_responses.emplace_back().service_uuid = ps->service_uuid;
    }

    if(req->m_responses.size() == 0)
      POSEIDON_LOG_ERROR((
          "No service configured: service_uuid = `$1`, service_type = `$2`"),
          req->m_target_service_uuid, req->m_target_service_type);

    bool something_sent = false;
    for(size_t k = 0;  k != req->m_responses.size();  ++k) {
      auto& resp = req->m_responses.mut(k);
      resp.request_uuid = ::poseidon::UUID::random();

      const auto& srv = this->m_impl->remote_services_by_uuid.at(resp.service_uuid);
      auto& conn = this->m_impl->remote_connections_by_uuid[srv.service_uuid];
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

        int64_t timestamp = ::time(nullptr);
        char auth_password[33];
        do_salt_password(auth_password, timestamp, this->m_impl->application_password);

        session = this->m_impl->private_client.connect(
             sformat("$1/?t=$2&pw=$3", *use_addr, timestamp, auth_password),  // URI
             ::poseidon::Easy_WS_Client::callback_type(
                [weak_impl = wkptr<Implementation>(this->m_impl),
                 service_uuid_2 = resp.service_uuid]
                   (const shptr<::poseidon::WS_Client_Session>& session_2,
                    ::poseidon::Abstract_Fiber& /*fiber*/,
                    ::poseidon::Easy_WS_Event event, linear_buffer&& data)
                  {
                    if(const auto impl = weak_impl.lock())
                      do_client_ws_callback(impl, service_uuid_2, session_2, event, move(data));
                  }));

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

      // Send the request asynchronously.
      auto task2 = new_sh<Send_Request_Task>(session, req->m_opcode,
                            req->m_request_data, req, resp.request_uuid);
      ::poseidon::task_executor.enqueue(task2);
      task2->m_self_lock = task2;
      something_sent = true;
    }

    if(!something_sent)
      req->mf_abstract_future_complete();
  }

}  // namespace k32
