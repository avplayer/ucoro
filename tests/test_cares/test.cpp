#include <iostream>
#include "ucoro/awaitable.hpp"
#include <vector>
#include <ares.h>


////////////////////////////////////////////////////////////////////
template <auto fn>
struct deleter
{
    template <typename T>
    constexpr void operator()(T* arg) const
    {
        fn(arg);
    }
};

template <typename T, auto fn>
using auto_delete = std::unique_ptr<T, deleter<fn>>;
////////////////////////////////////////////////////////////////////
ucoro::awaitable<std::vector<sockaddr>> aync_cares_query(std::string host)
{
	ares_channel_t* channel = co_await ucoro::local_storage_t<ares_channel_t*>();

	/* Perform an IPv4 and IPv6 request for the provided domain name */
	co_return co_await callback_awaitable<std::vector<sockaddr>>([channel, host](auto continuation_handle) mutable
	{
		ares_addrinfo_hints hints = { 0 };
		hints.ai_family = AF_UNSPEC;
		hints.ai_flags  = ARES_AI_CANONNAME;

		/* Callback that is called when DNS query is finished */
		auto addrinfo_cb = [](void *arg, int status, int timeouts, struct ares_addrinfo *result)
		{
			// 这里的 magic 是 decltype(continuation_handle) 类型，获取到了 resume 对象的指针.
			auto continuation_handle_p = reinterpret_cast<decltype(continuation_handle)*>(arg);

			auto_delete<decltype(continuation_handle), [](auto p){ delete p;}>
				auto_delete_continuation_handle{continuation_handle_p};

			std::vector<sockaddr> ret;

			auto_delete<ares_addrinfo, ares_freeaddrinfo> auto_delete_result(result);

			if (result)
			{
				struct ares_addrinfo_node *node;
				for (node = result->nodes; node != NULL; node = node->ai_next)
				{
					ret.push_back(*(node->ai_addr));
				}
			}

			(*continuation_handle_p)(ret);
		};

		// continuation_handle 这里被移动构造了一个 堆 上的版本。
		ares_getaddrinfo(channel, host.c_str(), NULL, &hints, addrinfo_cb, new decltype(continuation_handle){std::move(continuation_handle)});
	});
}


ucoro::awaitable<void> coro_cares_query()
{
	for (auto host : {"microcai.org", "github.com", "google.com", "baidu.com", "boost.org"})
	{
		auto dns_result = co_await aync_cares_query(host);

		printf("===================\n");
		std::cout << "query for \"" << host << "\" result:" << std::endl;

		for (auto sock_address : dns_result)
		{
			char        addr_buf[64] = "";
			const void *ptr          = NULL;
			if (sock_address.sa_family == AF_INET)
			{
				const struct sockaddr_in *in_addr =
					(const struct sockaddr_in *)((void *) &sock_address);
				ptr = &in_addr->sin_addr;
			}
			else if (sock_address.sa_family == AF_INET6)
			{
				const struct sockaddr_in6 *in_addr =
					(const struct sockaddr_in6 *)((void *) &sock_address);
				ptr = &in_addr->sin6_addr;
			}
			else
			{
				continue;
			}
			ares_inet_ntop(sock_address.sa_family, ptr, addr_buf, sizeof(addr_buf));
			printf("Addr: %s\n", addr_buf);
		}
	}
}

int main(int argc, char **argv)
{
	ares_library_init(ARES_LIB_INIT_ALL);

	ares_channel_t *channel = NULL;
  	ares_options options = { 0 };

	/* Enable event thread so we don't have to monitor file descriptors */
	options.evsys = ARES_EVSYS_DEFAULT;

	/* Initialize channel to run queries, a single channel can accept unlimited
	* queries */
	if (ares_init_options(&channel, &options, ARES_OPT_EVENT_THREAD) != ARES_SUCCESS)
	{
		printf("c-ares initialization issue\n");
		return 1;
	}

	coro_start(coro_cares_query(), channel);

	/* Wait until no more requests are left to be processed */
	ares_queue_wait_empty(channel, -1);

	/* Cleanup */
	ares_destroy(channel);
	ares_library_cleanup();
	return 0;
}
