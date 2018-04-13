//
// detail/resolver_service.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_DETAIL_RESOLVER_SERVICE_HPP
#define BOOST_ASIO_DETAIL_RESOLVER_SERVICE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>

#if !defined(BOOST_ASIO_WINDOWS_RUNTIME)

#include <boost/asio/ip/basic_resolver_query.hpp>
#include <boost/asio/ip/basic_resolver_results.hpp>
#include <boost/asio/detail/concurrency_hint.hpp>
#include <boost/asio/detail/memory.hpp>
#include <boost/asio/detail/resolve_endpoint_op.hpp>
#include <boost/asio/detail/resolve_query_op.hpp>
#include <boost/asio/detail/resolver_service_base.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace lslboost {
namespace asio {
namespace detail {

template <typename Protocol>
class resolver_service :
  public service_base<resolver_service<Protocol> >,
  public resolver_service_base
{
public:
  // The implementation type of the resolver. A cancellation token is used to
  // indicate to the background thread that the operation has been cancelled.
  typedef socket_ops::shared_cancel_token_type implementation_type;

  // The endpoint type.
  typedef typename Protocol::endpoint endpoint_type;

  // The query type.
  typedef lslboost::asio::ip::basic_resolver_query<Protocol> query_type;

  // The results type.
  typedef lslboost::asio::ip::basic_resolver_results<Protocol> results_type;

  // Constructor.
  resolver_service(lslboost::asio::io_context& io_context)
    : service_base<resolver_service<Protocol> >(io_context),
      resolver_service_base(io_context)
  {
  }

  // Destroy all user-defined handler objects owned by the service.
  void shutdown()
  {
    this->base_shutdown();
  }

  // Perform any fork-related housekeeping.
  void notify_fork(lslboost::asio::io_context::fork_event fork_ev)
  {
    this->base_notify_fork(fork_ev);
  }

  // Resolve a query to a list of entries.
  results_type resolve(implementation_type&, const query_type& query,
      lslboost::system::error_code& ec)
  {
    lslboost::asio::detail::addrinfo_type* address_info = 0;

    socket_ops::getaddrinfo(query.host_name().c_str(),
        query.service_name().c_str(), query.hints(), &address_info, ec);
    auto_addrinfo auto_address_info(address_info);

    return ec ? results_type() : results_type::create(
        address_info, query.host_name(), query.service_name());
  }

  // Asynchronously resolve a query to a list of entries.
  template <typename Handler>
  void async_resolve(implementation_type& impl,
      const query_type& query, Handler& handler)
  {
    // Allocate and construct an operation to wrap the handler.
    typedef resolve_query_op<Protocol, Handler> op;
    typename op::ptr p = { lslboost::asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl, query, io_context_impl_, handler);

    BOOST_ASIO_HANDLER_CREATION((io_context_impl_.context(),
          *p.p, "resolver", &impl, 0, "async_resolve"));

    start_resolve_op(p.p);
    p.v = p.p = 0;
  }

  // Resolve an endpoint to a list of entries.
  results_type resolve(implementation_type&,
      const endpoint_type& endpoint, lslboost::system::error_code& ec)
  {
    char host_name[NI_MAXHOST];
    char service_name[NI_MAXSERV];
    socket_ops::sync_getnameinfo(endpoint.data(), endpoint.size(),
        host_name, NI_MAXHOST, service_name, NI_MAXSERV,
        endpoint.protocol().type(), ec);

    return ec ? results_type() : results_type::create(
        endpoint, host_name, service_name);
  }

  // Asynchronously resolve an endpoint to a list of entries.
  template <typename Handler>
  void async_resolve(implementation_type& impl,
      const endpoint_type& endpoint, Handler& handler)
  {
    // Allocate and construct an operation to wrap the handler.
    typedef resolve_endpoint_op<Protocol, Handler> op;
    typename op::ptr p = { lslboost::asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl, endpoint, io_context_impl_, handler);

    BOOST_ASIO_HANDLER_CREATION((io_context_impl_.context(),
          *p.p, "resolver", &impl, 0, "async_resolve"));

    start_resolve_op(p.p);
    p.v = p.p = 0;
  }
};

} // namespace detail
} // namespace asio
} // namespace lslboost

#include <boost/asio/detail/pop_options.hpp>

#endif // !defined(BOOST_ASIO_WINDOWS_RUNTIME)

#endif // BOOST_ASIO_DETAIL_RESOLVER_SERVICE_HPP