#include <iostream>
#include "ucoro/awaitable.hpp"

#include "event2/event.h"

ucoro::awaitable<void> async_sleep_with_event_timer(int ms)
{
	event_base* io = co_await ucoro::local_storage_t<event_base*>();

	co_await executor_awaitable<void>([ms, io](auto continuation)
	{
		timeval tout = {
			.tv_sec = ms / 1000,
			.tv_usec = 1000 * (ms % 1000)
		};

		auto ev = event_new(io, -1, 0, [](evutil_socket_t, short, void * continuation_address)
		{
			auto continuation_ = std::coroutine_handle<>::from_address(continuation_address);
			continuation_.resume();
		}, continuation.address());

		event_add(ev, &tout);
	});
}

ucoro::awaitable<int> coro_compute_int(int value)
{
	co_await async_sleep_with_event_timer(20);

	co_return (value + 1);
}

ucoro::awaitable<void> coro_compute_exec(int value)
{
	auto ret = co_await coro_compute_int(value);
	std::cout << "return: " << ret << std::endl;
	co_return;
}

ucoro::awaitable<void> coro_compute()
{
	event_base* io = co_await ucoro::local_storage_t<event_base*>();

	for (auto i = 0; i < 100; i++)
	{
		co_await coro_compute_exec(i);
	}

	event_base_loopbreak(io);
}

int main(int argc, char **argv)
{

	event_config* io_cfg = event_config_new();
	event_config_set_flag(io_cfg, EVENT_BASE_FLAG_NOLOCK);
	event_config_avoid_method(io_cfg, "select");

	auto io = event_base_new_with_config(io_cfg);
	event_config_free(io_cfg);


	coro_start(coro_compute(), io);


	event_base_dispatch(io);
	event_base_free(io);
	return 0;
}
