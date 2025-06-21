// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_HTTP_REQUESTOR_
#define K32_COMMON_HTTP_REQUESTOR_

#include "../fwd.hpp"
#include "http_future.hpp"
namespace k32 {

class HTTP_Requestor
  {
  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    HTTP_Requestor();

  public:
    HTTP_Requestor(const HTTP_Requestor&) = delete;
    HTTP_Requestor& operator=(const HTTP_Requestor&) & = delete;
    ~HTTP_Requestor();

    // Sets the target host. This must be called before any request messages
    // can be sent.
    void
    set_target_host(const cow_string& host, uint16_t port = 80);

    // Enqueues an HTTP request. After this function returns, the caller shall
    // wait on the future. If this function fails, an exception is thrown, and
    // there is no effect.
    void
    enqueue(const shptr<HTTP_Future>& req);
  };

}  // namespace k32
#endif
