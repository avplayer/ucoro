#include <iostream>
#include "ucoro/awaitable.hpp"


ucoro::awaitable<int> coro_compute_int(int value)
{
	co_return (value * 100);
}

ucoro::awaitable<void> coro_compute_exec(int value)
{
	auto x = co_await ucoro::local_storage;
	std::cout << "local storage: " << std::any_cast<std::string>(x) << std::endl;

	try
	{
		auto y = co_await ucoro::local_storage_t<std::string>();
		std::cout << "local storage: " << y << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cout << e.what();
	}

	auto comput_promise = coro_compute_int(value);

	auto ret = co_await std::move(comput_promise);
	std::cout << "return: " << ret << std::endl;
}

ucoro::awaitable<void> coro_compute()
{
	for (auto i = 0; i < 100; i+=2)
	{
		co_await coro_compute_exec(i);
		co_await coro_compute_exec(i+1).detach(std::string{"hello from detached coro"});
	}
}

int main(int argc, char **argv)
{
	std::string str = "hello from main coro";

	coro_start(coro_compute(), str);

	return 0;
}
