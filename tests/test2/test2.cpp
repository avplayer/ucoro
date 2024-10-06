
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <iostream>
#include "ucoro/awaitable.hpp"

boost::asio::io_context main_ioc;

template <typename T>
struct awaitable_return_value
{
	T return_value;
	T await_resume()
	{
		return return_value;
	}
};

template <>
struct awaitable_return_value<void>
{
	void await_resume()
	{
	}
};


template <typename T> requires (!std::is_same_v<T, void>)
auto  transform_asio_awaitable(boost::asio::awaitable<T>&& asio_awaitable)
{
	struct asio_awaitable_awiter : public awaitable_return_value<T>
	{
		asio_awaitable_awiter(boost::asio::awaitable<T>&& asio_awaitable) : asio_awaitable(std::move(asio_awaitable)){};

		constexpr auto await_ready() { return false; }

		auto await_suspend(std::coroutine_handle<> continue_handle)
		{
			boost::asio::co_spawn(main_ioc, [this](auto continue_handle) -> boost::asio::awaitable<void>
			{
				if constexpr (!std::is_void_v<T>)
				{
					this->return_value = co_await std::move(asio_awaitable);
				}
				else
				{
					co_await std::move(asio_awaitable);
				}

				continue_handle.resume();
				co_return ;
			}(continue_handle), [](std::exception_ptr){});
		}

		boost::asio::awaitable<T> asio_awaitable;
	};
	return asio_awaitable_awiter { std::move(asio_awaitable) };
}


namespace ucoro
{

template <typename T>
struct await_transformer<boost::asio::awaitable<T>>
{
	static auto await_transform(boost::asio::awaitable<T>&& awaitee)
	{
		return transform_asio_awaitable(std::move(awaitee));
	}
};

}

boost::asio::awaitable<int> asio_coro_test()
{
	co_return 10;
}

ucoro::awaitable<int> coro_compute_int(int value)
{
	auto ret = co_await executor_awaitable<int>([value](auto handle) {
		main_ioc.post([value, handle = std::move(handle)]() mutable {
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
