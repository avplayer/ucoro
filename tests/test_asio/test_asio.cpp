
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <iostream>
#include "ucoro/awaitable.hpp"
#include "ucoro/asio_glue.hpp"

boost::asio::io_context main_ioc;

ucoro::awaitable<int> called_from_asio_coro()
{
	co_return 10;
}

boost::asio::awaitable<int> asio_coro_test()
{
	co_return co_await ucoro::asio_glue::to_asio_awaitable(called_from_asio_coro());
}

ucoro::awaitable<int> coro_compute_int(int value)
{
	auto ret = co_await callback_awaitable<int>([value](auto handle) {
		boost::asio::post(main_ioc, [value, handle = std::move(handle)]() mutable {
			std::this_thread::sleep_for(std::chrono::seconds(0));
			std::cout << value << " value\n";
			handle(value * 100);
		});
	});

	co_return (value + ret + co_await asio_coro_test());
}

ucoro::awaitable<void> coro_compute_exec(int value)
{
	auto ret = co_await coro_compute_int(value);
	std::cout << "return: " << ret << std::endl;
	co_return;
}

ucoro::awaitable<void> coro_compute()
{
	// auto x = co_await ucoro::local_storage;
	// std::cout << "local storage: " << std::any_cast<boost::asio::io_context*>(x) << std::endl;

	for (auto i = 0; i < 100; i++)
	{
		co_await coro_compute_exec(i);
	}
}

int main(int argc, char **argv)
{
	coro_start(coro_compute(), boost::asio::any_io_executor(main_ioc.get_executor()));

	main_ioc.run();

	return 0;
}
