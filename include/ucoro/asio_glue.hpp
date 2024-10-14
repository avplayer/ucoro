
#pragma once

#include "./awaitable.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>

namespace ucoro::asio_glue
{

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

	template <typename T>
	struct asio_awaitable_awaiter : public awaitable_return_value<T>
	{
		asio_awaitable_awaiter(boost::asio::awaitable<T> &&asio_awaitable)
			: asio_awaitable(std::move(asio_awaitable))
		{
		}

		constexpr auto await_ready()
		{
			return false;
		}

		auto await_suspend(std::coroutine_handle<typename ucoro::awaitable<T>::promise_type> continue_handle) noexcept
		{
			boost::asio::any_io_executor executor;
			if (continue_handle.promise().local_)
			{
				try
				{
					executor = std::any_cast<boost::asio::any_io_executor>(*continue_handle.promise().local_);
				}
				catch (const std::bad_any_cast& e)
				{
					assert(0 && "should start coro with a pointer to io_context");
					std::terminate();
				}
			}
			else
			{
				assert(0 && "should start coro with a pointer to io_context");
				std::terminate();
			}

			boost::asio::co_spawn(executor, [this](auto continue_handle) -> boost::asio::awaitable<void>
			{
				// continue_handle
				if constexpr (!std::is_void_v<T>)
				{
					this->return_value = co_await std::move(asio_awaitable);
				}
				else
				{
					co_await std::move(asio_awaitable);
				}

				continue_handle.resume();
				co_return;
			}(continue_handle), [](std::exception_ptr) {});
		}

		boost::asio::awaitable<T> asio_awaitable;
	};

	template <typename T>
	struct initiate_do_invoke_ucoro_awaitable
	{
		template <typename Handler>
		void operator()(Handler &&handler, ucoro::awaitable<T> *ucoro_awaitable) const
		{
			boost::asio::any_io_executor io_context = boost::asio::get_associated_executor(handler);
			if constexpr (std::is_void_v<T>)
			{
				[handler = std::move(handler), ucoro_awaitable = std::move(*ucoro_awaitable)]() mutable -> ucoro::awaitable<void>
				{
					co_await std::move(ucoro_awaitable);
					handler(boost::system::error_code());
				}().detach(io_context);
			}
			else
			{
				[handler = std::move(handler), ucoro_awaitable = std::move(*ucoro_awaitable)]() mutable -> ucoro::awaitable<void>
				{
					auto return_value = co_await std::move(ucoro_awaitable);
					handler(boost::system::error_code(), return_value);
				}().detach(io_context);
			}

		}
	};

	template <typename T>
	auto to_asio_awaitable(ucoro::awaitable<T> &&ucoro_awaitable)
	{
		return boost::asio::async_initiate<decltype(boost::asio::use_awaitable), void(boost::system::error_code, T)>(
			initiate_do_invoke_ucoro_awaitable<T>(), boost::asio::use_awaitable, &ucoro_awaitable);
	}

	template <>
	auto to_asio_awaitable<void>(ucoro::awaitable<void> &&ucoro_awaitable)
	{
		return boost::asio::async_initiate<decltype(boost::asio::use_awaitable), void(boost::system::error_code)>(
			initiate_do_invoke_ucoro_awaitable<void>(), boost::asio::use_awaitable, &ucoro_awaitable);
	}

} // namespace ucoro::asio_glue

template <typename T>
struct ucoro::await_transformer<boost::asio::awaitable<T>>
{
	static auto await_transform(boost::asio::awaitable<T>&& asio_awaitable)
	{
		return ucoro::asio_glue::asio_awaitable_awaiter { std::move(asio_awaitable) };
	}
};
