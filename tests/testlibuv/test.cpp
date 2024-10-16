#include <iostream>
#include "ucoro/awaitable.hpp"

#include "uv.h"

ucoro::awaitable<void> async_sleep_with_uv_timer(int ms)
{
	co_await callback_awaitable<void>([ms](auto continuation)
	{
		struct uv_timer_with_data : uv_timer_s
		{
			decltype(continuation) continuation_;

			uv_timer_with_data(decltype(continuation)&& c)
				: continuation_(c){}
		};

		uv_timer_with_data* timer_handle = new uv_timer_with_data { std::forward<decltype(continuation)>(continuation) };

		uv_timer_init(uv_default_loop(), timer_handle);
		uv_timer_start(timer_handle, [](uv_timer_t* handle)
		{
			uv_timer_stop(handle);
			reinterpret_cast<uv_timer_with_data*>(handle)->continuation_();
			delete handle;
		}, ms, false);

	});
}

ucoro::awaitable<int> coro_compute_int(int value)
{
	co_await async_sleep_with_uv_timer(20);

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
	for (auto i = 0; i < 100; i++)
	{
		co_await coro_compute_exec(i);
	}

	uv_stop(uv_default_loop());
}

int main(int argc, char **argv)
{
	coro_start(coro_compute(), uv_default_loop());

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
	uv_library_shutdown();
	return 0;
}
