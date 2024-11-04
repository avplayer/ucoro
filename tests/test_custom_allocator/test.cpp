
#include "ucoro/awaitable.hpp"
#include <iostream>

struct my_allocator
{
	using value_type = void;

	value_type* allocate(std::size_t size)
	{
		return std::malloc(size);
	}

	void deallocate(value_type* ptr, std::size_t size)
	{
		free(ptr);
	}

};

// 只有 test2 用自定义分配器分配
// 验证混合使用 分配器的awaitable也是没问题的
ucoro::awaitable<int, my_allocator> test()
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
