#include "../src/cancellable_streambuf.h"
#include "catch.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace asio = lslboost::asio;
using namespace asio;
using err_t = const lslboost::system::error_code &;
typedef lsl::cancellable_streambuf cancellable_streambuf;

static uint16_t port = 28812;
static const char hello[] = "Hello World";
static const std::string hellostr(hello);

static std::mutex output_mutex;

asio::const_buffer hellobuf() { return asio::const_buffer(hello, sizeof(hello)); }

#define MINFO(str)                                                                                 \
	{                                                                                              \
		std::unique_lock<std::mutex> out_lock(output_mutex);                                       \
		INFO(str)                                                                                  \
	}

template <typename T> void test_cancel_thread(T &&task, cancellable_streambuf &sb) {
	std::condition_variable cv;
	std::mutex mut;
	bool status = false;
	auto future = std::async(std::launch::async, [&]() {
		std::unique_lock<std::mutex> lock(mut);
		MINFO("Thread 1: started")
		status = true;
		lock.unlock();
		cv.notify_all();
		MINFO("Thread 1: starting socket operation")
		task();
		MINFO("Thread 1: socket operation finished")
	});
	// We need to wait until sb_blockconnect.connect() was called, but the
	// thread is blocked connecting so we can't let it signal it's ready
	// So we wait 200ms immediately after connect() is supposed to be called
	{
		std::unique_lock<std::mutex> lock(mut);
		cv.wait(lock, [&] { return status; });
	}

	if (future.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready)
		MINFO("Thread 1 finished too soon, couldn't test cancellation")
	MINFO("Thread 0: Closing socket…")
	sb.cancel();
	// Double cancel, shouldn't do anything dramatic
	sb.cancel();

	// Allow the thread 2 seconds to finish
	if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
		throw std::runtime_error("Thread 0: Thread didn't join!");
	else {
		INFO("Thread 0: Thread was successfully canceled")
		future.get();
	}
}

TEST_CASE("streambufs can connect", "[streambuf][basic][network]") {
	asio::io_context io_ctx;
	cancellable_streambuf sb_connect;
	INFO("Thread 0: Binding remote socket and keeping it busy…")
	ip::tcp::endpoint ep(ip::address_v4::loopback(), port++);
	ip::tcp::acceptor remote(io_ctx);
	remote.open(ip::tcp::v4());
	remote.bind(ep);
	// Create a socket that keeps connect()ing sockets hanging
	// On Windows, this requires an additional socket options, on Unix
	// a backlog size of 0 and a socket waiting for the connection to be accept()ed
	// On macOS, backlog 0 uses SOMAXCONN instead and 1 is correct
#ifdef _WIN32
	remote.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_CONDITIONAL_ACCEPT>(1));
	remote.listen(0);
#else
#ifdef __APPLE__
	int backlog = 1;
#else
	int backlog = 0;
#endif
	remote.listen(backlog);
	cancellable_streambuf busykeeper;
	busykeeper.connect(ep);
#endif
	INFO("Thread 0: Remote socket should be busy")

	test_cancel_thread([&sb_connect, ep]() { sb_connect.connect(ep); }, sb_connect);
	remote.close();
}

TEST_CASE("streambufs can transfer data", "[streambuf][network]") {
	asio::io_context io_ctx;
	cancellable_streambuf sb_read;
	ip::tcp::endpoint ep(ip::address_v4::loopback(), port++);
	ip::tcp::acceptor remote(io_ctx, ep, true);
	remote.listen(1);
	INFO("Thread 0: Connecting…")
	sb_read.connect(ep);
	INFO("Thread 0: Connected (" << sb_read.puberror().message() << ')')
	ip::tcp::socket sock(io_ctx);
	remote.accept(sock);

	test_cancel_thread(
		[&sb_read]() {
			int c = sb_read.sgetc();
			MINFO("Thread 1: Read char " << c)
		},
		sb_read);
}

TEST_CASE("receive v4 packets on v6 socket", "[ipv6][network]") {
	const uint16_t test_port = port++;
	asio::io_context io_ctx;
	ip::udp::socket sock(io_ctx, ip::udp::v6());
	sock.set_option(ip::v6_only(false));
	sock.bind(ip::udp::endpoint(ip::address_v6::any(), test_port));

	ip::udp::socket sender_v4(io_ctx, ip::udp::v4()), sender_v6(io_ctx, ip::udp::v6());
	asio::const_buffer sbuf(hellobuf());
	char recvbuf[64] = {0};
	sender_v4.send_to(sbuf, ip::udp::endpoint(ip::address_v4::loopback(), test_port));
	auto recv_len = sock.receive(asio::buffer(recvbuf, sizeof(recvbuf) - 1));
	CHECK(recv_len == sizeof(hello));
	CHECK(hellostr == recvbuf);
	std::fill_n(recvbuf, recv_len, 0);

	sender_v6.send_to(sbuf, ip::udp::endpoint(ip::address_v6::loopback(), test_port));
	recv_len = sock.receive(asio::buffer(recvbuf, sizeof(recvbuf) - 1));
	CHECK(hellostr == recvbuf);
	std::fill_n(recvbuf, recv_len, 0);
}

TEST_CASE("ipaddresses", "[ipv6][network][basic]") {
	ip::address_v4 v4addr(ip::make_address_v4("192.168.172.1")),
		mcastv4(ip::make_address_v4("239.0.0.183"));
	ip::address_v6 v6addr = ip::make_address_v6(ip::v4_mapped_t(), v4addr);
	ip::address addr(v4addr), addr_mapped(v6addr);
	CHECK(!v4addr.is_multicast());
	CHECK(mcastv4.is_multicast());
	// mapped IPv4 multicast addresses aren't considered IPv6 multicast addresses
	CHECK(!ip::make_address_v6(ip::v4_mapped, mcastv4).is_multicast());
	CHECK(v6addr.is_v4_mapped());
	CHECK(addr != addr_mapped);
	CHECK(addr == ip::address(ip::make_address_v4(ip::v4_mapped, v6addr)));

	auto scoped = ip::make_address_v6("::1%3");
	CHECK(scoped.scope_id() == 3);
}

/// Can multiple sockets bind to the same port and receive all broad-/multicast packets?
TEST_CASE("reuseport", "[network][basic][!mayfail]") {
	const uint16_t test_port = port++;
	asio::io_context io_ctx(1);
	lslboost::system::error_code ec;
	// Linux: sudo ip link set lo multicast on; sudo ip mroute show table all
	for (auto addrstr : {"224.0.0.1", "255.255.255.255", "ff02::1"}) SECTION(addrstr) {
			std::vector<ip::udp::socket> socks;
			auto addr = ip::make_address(addrstr);
			if (!addr.is_multicast())
				REQUIRE((addr.is_v4() && addr.to_v4() == ip::address_v4::broadcast()));
			auto proto = addr.is_v4() ? ip::udp::v4() : ip::udp::v6();
			for (int i = 0; i < 2; ++i) {
				socks.emplace_back(io_ctx, proto);
				auto &sock = socks.back();
				sock.set_option(ip::udp::socket::reuse_address(true));
				if (addr.is_multicast()) sock.set_option(ip::multicast::join_group(addr), ec);
				if (ec == error::no_such_device || ec == std::errc::address_not_available)
					FAIL("No IPv6 route configured, skipping test!");
				sock.bind(ip::udp::endpoint(proto, test_port));
			}
			{
				ip::udp::socket outsock(io_ctx, proto);
				if (addr.is_multicast())
					outsock.set_option(ip::multicast::join_group(addr));
				else
					outsock.set_option(ip::udp::socket::broadcast(true));
				// outsock.set_option(ip::multicast::enable_loopback(true));
				auto sent = outsock.send_to(hellobuf(), ip::udp::endpoint(addr, test_port));
				REQUIRE(sent == sizeof(hello));
				outsock.close();
			}
			char inbuf[sizeof(hello)] = {0};
			std::size_t received = 0;

			asio::steady_timer timeout(io_ctx, std::chrono::seconds(2));
			timeout.async_wait([](err_t err) {
				if (!err) throw std::runtime_error("Test didn't finish in time");
			});
			for (auto &insock : socks)
				insock.async_receive(
					asio::buffer(inbuf, sizeof(inbuf)), [&](err_t, std::size_t len) {
						CHECK(len == sizeof(hello));
						CHECK(hellostr == inbuf);
						received++;
					});
			while (received < socks.size()) io_ctx.run_one();
			timeout.cancel();
		}
}
