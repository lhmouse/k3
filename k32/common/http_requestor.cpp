// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_FRIENDS_C84621A4_4E68_11F0_BA96_5254005015D2_
#include "http_requestor.hpp"
#include <poseidon/easy/easy_http_client.hpp>
namespace k32 {
namespace {

struct Implementation
  {
    cow_string host;
    uint16_t port = 0;
    ::poseidon::Easy_HTTP_Client http_client;

    // requests
    wkptr<::poseidon::HTTP_Client_Session> weak_session;
    ::std::deque<wkptr<HTTP_Future>> request_queue;
  };

void
do_client_callback(const shptr<Implementation>& impl,
                   const shptr<::poseidon::HTTP_Client_Session>& session,
                   ::poseidon::Easy_HTTP_Event event,
                   ::poseidon::HTTP_S_Headers&& ht_resp, linear_buffer&& data)
  {
    switch(event)
      {
      case ::poseidon::easy_http_open:
        POSEIDON_LOG_INFO(("Connected to `http://$1`"), session->http_default_host());
        break;

      case ::poseidon::easy_http_message:
        {
          if(ht_resp.status <= 199)
            break;

          if(impl->request_queue.empty())
            break;

          // Set the first request.
          auto req = impl->request_queue.front().lock();
          impl->request_queue.pop_front();
          if(!req)
            break;

          req->mf_resp_status_code() = ht_resp.status;
          req->mf_resp_payload().assign(data.begin(), data.end());

          for(const auto& r : ht_resp.headers)
            if(r.first == "Content-Type")
              req->mf_resp_content_type() = r.second.as_string();

          req->mf_abstract_future_complete();
          break;
        }

      case ::poseidon::easy_http_close:
        {
          // Abandon pipelined requests.
          while(!impl->request_queue.empty()) {
            auto req = impl->request_queue.front().lock();
            impl->request_queue.pop_front();
            if(!req)
              continue;

            req->mf_abstract_future_complete();
          }

          impl->weak_session.reset();
          POSEIDON_LOG_INFO(("Disconnected from `http://$1`"), session->http_default_host());
          break;
        }
      }
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(HTTP_Requestor,
    Implementation);

HTTP_Requestor::
HTTP_Requestor()
  {
  }

HTTP_Requestor::
~HTTP_Requestor()
  {
  }

void
HTTP_Requestor::
set_target_host(const cow_string& host, uint16_t port)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    this->m_impl->host = host;
    this->m_impl->port = port;
  }

void
HTTP_Requestor::
enqueue(const shptr<HTTP_Future>& req)
  {
    if(!this->m_impl)
      POSEIDON_THROW(("Service not initialized"));

    if(!req)
      POSEIDON_THROW(("Null request pointer"));

    if(!req->m_req_path.starts_with("/"))
      POSEIDON_THROW(("Request path must start with `/`"));

    // Allocate a connection.
    auto session = this->m_impl->weak_session.lock();
    if(!session) {
      session = this->m_impl->http_client.connect(
           sformat("$1:$2", this->m_impl->host, this->m_impl->port),
           ::poseidon::Easy_HTTP_Client::callback_type(
              [weak_impl = wkptr<Implementation>(this->m_impl)]
                 (const shptr<::poseidon::HTTP_Client_Session>& session2,
                  ::poseidon::Abstract_Fiber& /*fiber*/,
                  ::poseidon::Easy_HTTP_Event event,
                  ::poseidon::HTTP_S_Headers&& ht_resp, linear_buffer&& data)
                {
                  if(const auto impl = weak_impl.lock())
                    do_client_callback(impl, session2, event, move(ht_resp), move(data));
                }));

      this->m_impl->weak_session = session;
      POSEIDON_LOG_INFO(("Connecting to `http://$1`"), session->http_default_host());
    }

    // Add this future to the waiting list.
    this->m_impl->request_queue.emplace_back(req);

    // Send the request message.
    ::poseidon::HTTP_C_Headers ht_req;
    ht_req.method = ::poseidon::http_GET;
    ht_req.raw_path = req->m_req_path;
    ht_req.raw_query = req->m_req_query;
    ht_req.headers.emplace_back(&"Connection", &"keep-alive");

    if(!req->m_req_payload.empty()) {
      // Set request payload.
      ht_req.method = ::poseidon::http_POST;
      if(!req->m_req_content_type.empty())
        ht_req.headers.emplace_back(&"Content-Type", req->m_req_content_type);
    }

    session->http_request(move(ht_req), req->m_req_payload);
  }

}  // namespace k32
