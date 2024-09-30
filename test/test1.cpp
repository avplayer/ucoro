
#include <boost/asio.hpp>
#include <iostream>
#include "ucoro/awaitable.hpp"

boost::asio::io_context main_ioc;

ucoro::awaitable<int> coro_compute_int(int value)
{
	auto ret = co_await manual_awaitable<int>([value](auto handle) {
		main_ioc.post([value, handle = std::move(handle)]() mutable {
			std::this_thread::sleep_for(std::chrono::seconds(0));
			std::cout << value << " value\n";
			handle(value * 100);
		});
	});

	co_return (value + ret);
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
}

int main(int argc, char **argv)
{
	coro_start(coro_compute());
	main_ioc.run();
	return 0;
}
