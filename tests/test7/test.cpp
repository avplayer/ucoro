
#include "ucoro/awaitable.hpp"
#include <iostream>

ucoro::awaitable<int> test()
{
	throw std::runtime_error("test throw");
    co_return 1;
}


ucoro::awaitable<void> test2()
{
	throw std::runtime_error("test throw");
    co_return;
}

ucoro::awaitable<int> coro_compute()
{
	try
	{
		sync_await(test2());
	}
	catch(const std::exception& e)
	{
		std::cerr << "exception in test2: " << e.what() << '\n';
	}

	co_return co_await test();
}

int main(int argc, char** argv)
{
	try
	{
		std::string str = "hello";
		sync_await(coro_compute(), str);
	}
	catch (std::exception& e)
	{
		std::cerr << "exception: " << e.what() << std::endl;
	}

	return 0;
}
