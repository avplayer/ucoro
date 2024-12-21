
#include <boost/asio.hpp>
#include <iostream>
#include "ucoro/awaitable.hpp"

boost::asio::io_context main_ioc;

ucoro::awaitable<int> coro_compute_int(int value)
{
	auto ret = co_await callback_awaitable<int>([value](auto handle) {
		boost::asio::post(main_ioc, [value, handle = std::move(handle)]() mutable {
			std::this_thread::sleep_for(std::chrono::seconds(0));
			std::cout << value << " value\n";

			// 这里调用后，重新进入协程框架.
			handle(value * 100);
		});

		// 这里 return 后，就出协程框架了.
	});


	// 放这里能抓到
	throw std::bad_alloc{};

	co_return (value + ret);
}

ucoro::awaitable<void> coro_compute_exec(int value)
{
	try {
		auto ret = co_await coro_compute_int(value);
		std::cout << "return: " << ret << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cout << "exception: " << e.what() << std::endl;
	}

	co_return;
}

ucoro::awaitable<void> coro_compute()
{
	auto x = co_await ucoro::local_storage;
	std::cout << "local storage: " << std::any_cast<std::string>(x) << std::endl;

	for (auto i = 0; i < 100; i++)
	{
		co_await coro_compute_exec(i);
	}
}

int main(int argc, char **argv)
{
	std::string str = "hello";

	coro_start(coro_compute(), str);

	main_ioc.run();

	return 0;
}
