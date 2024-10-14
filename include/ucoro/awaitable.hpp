﻿//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <concepts>
#include <variant>

#if defined(__has_include)

#if __has_include(<coroutine>)
#include <coroutine>
#else
#include <experimental/coroutine>
namespace std
{
	using std::experimental::coroutine_handle;
	using std::experimental::coroutine_traits;
	using std::experimental::noop_coroutine;
	using std::experimental::suspend_always;
	using std::experimental::suspend_never;
} // namespace std
#endif

#else

#error "Compiler version too low to support coroutine !!!"

#endif

#include <any>
#include <cstdlib>
#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>

#if defined(DEBUG) || defined(_DEBUG)
#if defined(ENABLE_DEBUG_CORO_LEAK)

#define DEBUG_CORO_PROMISE_LEAK

#include <unordered_set>
inline std::unordered_set<void*> debug_coro_leak;

#endif
#endif


namespace ucoro
{
	template <typename T>
	struct await_transformer;

	template <typename T>
	struct awaitable;

	template <typename T>
	struct awaitable_promise;

	template <typename T, typename CallbackFunction>
	struct ExecutorAwaiter;

	template <typename T, typename CallbackFunction>
	struct CallbackAwaiter;

	template <typename T>
	struct local_storage_t
	{
	};
	inline constexpr local_storage_t<void> local_storage;

	//////////////////////////////////////////////////////////////////////////
	namespace detail
	{
		template <typename T>
		concept is_valid_await_suspend_return_value =
			std::convertible_to<T, std::coroutine_handle<>> || std::is_void_v<T> || std::is_same_v<T, bool>;

		// 用于判定 T 是否是一个 awaiter 的类型, 即: 拥有 await_ready，await_suspend，await_resume 成员函数的结构或类.
		template <typename T>
		concept is_awaiter_v = requires(T a) {
			{ a.await_ready() } -> std::convertible_to<bool>;
			{ a.await_suspend(std::coroutine_handle<>{}) } -> is_valid_await_suspend_return_value;
			{ a.await_resume() };
		};

		// 用于判定 T 是否是一个 awaitable<>::promise_type 的类型, 即: 拥有 local_ 成员。
		template <typename T>
		concept is_awaitable_promise_type_v = requires (T a){
			{ a.local_ } -> std::convertible_to<std::any> ;
		};
	} // namespace detail

	struct debug_coro_promise
	{
#if defined(DEBUG_CORO_PROMISE_LEAK)

		void* operator new(std::size_t size)
		{
			void* ptr = std::malloc(size);
			if (!ptr)
			{
				throw std::bad_alloc{};
			}
			debug_coro_leak.insert(ptr);
			return ptr;
		}

		void operator delete(void* ptr, [[maybe_unused]] std::size_t size)
		{
			debug_coro_leak.erase(ptr);
			std::free(ptr);
		}

#endif // DEBUG_CORO_PROMISE_LEAK
	};


	//////////////////////////////////////////////////////////////////////////
	struct awaitable_detached
	{
		struct promise_type
			: public debug_coro_promise
		{
			std::suspend_never initial_suspend() noexcept
			{
				return {};
			}

			std::suspend_never final_suspend() noexcept
			{
				return {};
			}

			void return_void() noexcept
			{
			}

			void unhandled_exception()
			{
			}

			awaitable_detached get_return_object() noexcept
			{
				return awaitable_detached();
			}
		};
	};

	//////////////////////////////////////////////////////////////////////////

	template <typename T>
	struct final_awaitable : std::suspend_always
	{
		std::coroutine_handle<> await_suspend(std::coroutine_handle<awaitable_promise<T>> h) noexcept
		{
			if (h.promise().continuation_)
			{
				return h.promise().continuation_;
			}
			return std::noop_coroutine();
		}
	};

	//////////////////////////////////////////////////////////////////////////
	// 存储协程 promise 的返回值
	template<typename T>
	struct awaitable_promise_value
	{
		template <typename V>
		void return_value(V &&val) noexcept
		{
			value_.template emplace<T>(std::forward<V>(val));
		}

		void unhandled_exception() noexcept
		{
			value_.template emplace<std::exception_ptr>(std::current_exception());
		}

		std::variant<std::exception_ptr, T> value_{ nullptr };
	};

	//////////////////////////////////////////////////////////////////////////
	// 存储协程 promise 的返回值 void 的特化实现
	template<>
	struct awaitable_promise_value<void>
	{
		std::exception_ptr exception_{ nullptr };

		constexpr void return_void() noexcept { }

		void unhandled_exception() noexcept
		{
			exception_ = std::current_exception();
		}

	};

	//////////////////////////////////////////////////////////////////////////
	// 返回 T 的协程 awaitable_promise 实现.

	// Promise 类型实现...
	template <typename T>
	struct awaitable_promise : public awaitable_promise_value<T>, public debug_coro_promise
	{
		awaitable<T> get_return_object();

		auto final_suspend() noexcept
		{
			return final_awaitable<T>{};
		}

		auto initial_suspend()
		{
			return std::suspend_always{};
		}

		template <typename A>
			requires (detail::is_awaiter_v<std::decay_t<A>>)
		auto await_transform(A&& awaiter) const
		{
			static_assert(std::is_rvalue_reference_v<decltype(awaiter)>, "co_await must be used on rvalue");
			return std::forward<A>(awaiter);
		}

		template <typename A>
			requires (!detail::is_awaiter_v<std::decay_t<A>>)
		auto await_transform(A&& awaiter) const
		{
			return await_transformer<A>::await_transform(std::move(awaiter));
		}

		void set_local(std::any local)
		{
			local_ = std::make_shared<std::any>(local);
		}

		template <typename localtype>
		struct local_storage_awaiter
		{
			awaitable_promise *this_;

			constexpr bool await_ready() const noexcept { return true; }
			void await_suspend(std::coroutine_handle<void>) noexcept {}

			auto await_resume() const noexcept
			{
				if constexpr (std::is_void_v<localtype>)
				{
					return *this_->local_;
				}
				else
				{
					return std::any_cast<localtype>(*this_->local_);
				}
			}
		};

		template <typename localtype>
		auto await_transform(local_storage_t<localtype>)
		{
			return local_storage_awaiter<localtype>{this};
		}

		std::coroutine_handle<> continuation_;
		std::shared_ptr<std::any> local_;
	};

	//////////////////////////////////////////////////////////////////////////

	// awaitable 协程包装...
	template <typename T>
	struct awaitable
	{
		using promise_type = awaitable_promise<T>;

		explicit awaitable(std::coroutine_handle<promise_type> h) : current_coro_handle_(h)
		{
		}

		~awaitable()
		{
			if (current_coro_handle_ && current_coro_handle_.done())
			{
				current_coro_handle_.destroy();
			}
		}

		awaitable(awaitable &&t) noexcept : current_coro_handle_(t.current_coro_handle_)
		{
			t.current_coro_handle_ = nullptr;
		}

		awaitable &operator=(awaitable &&t) noexcept
		{
			if (&t != this)
			{
				if (current_coro_handle_)
				{
					current_coro_handle_.destroy();
				}
				current_coro_handle_ = t.current_coro_handle_;
				t.current_coro_handle_ = nullptr;
			}
			return *this;
		}

		awaitable(const awaitable &) = delete;
		awaitable(awaitable &) = delete;
		awaitable &operator=(const awaitable &) = delete;
		awaitable &operator=(awaitable &) = delete;

		T operator()()
		{
			return get();
		}

		T get()
		{
			if constexpr (!std::is_void_v<T>)
			{
				return std::move(current_coro_handle_.promise().value_);
			}
		}

		constexpr bool await_ready() const noexcept
		{
			return false;
		}

		T await_resume()
		{
			if constexpr (std::is_void_v<T>)
			{
				auto exception = current_coro_handle_.promise().exception_;
				if (exception)
				{
					std::rethrow_exception(exception);
				}

				current_coro_handle_.destroy();
				current_coro_handle_ = nullptr;
			}
			else
			{
				auto ret = std::move(current_coro_handle_.promise().value_);
				if (std::holds_alternative<std::exception_ptr>(ret))
				{
					std::rethrow_exception(std::get<std::exception_ptr>(ret));
				}

				current_coro_handle_.destroy();
				current_coro_handle_ = nullptr;

				return std::get<T>(ret);
			}
		}

		template <typename PromiseType>
		auto await_suspend(std::coroutine_handle<PromiseType> continuation)
		{
			if constexpr (detail::is_awaitable_promise_type_v<PromiseType>)
			{
				current_coro_handle_.promise().local_ = continuation.promise().local_;
			}

			current_coro_handle_.promise().continuation_ = continuation;
			return current_coro_handle_;
		}

		void set_local(std::any local)
		{
			assert("local has value" && !current_coro_handle_.promise().local_);
			current_coro_handle_.promise().set_local(local);
		}

		void detach()
		{
			auto launch_coro = [](awaitable<T> lazy) -> awaitable_detached { co_await lazy; };
			[[maybe_unused]] auto detached = launch_coro(std::move(*this));
		}

		void detach(std::any local)
		{
			if (local.has_value())
			{
				set_local(local);
			}

			auto launch_coro = [](awaitable<T> lazy) -> awaitable_detached { co_await lazy; };
			[[maybe_unused]] auto detached = launch_coro(std::move(*this));
		}

		std::coroutine_handle<promise_type> current_coro_handle_;
	};

	//////////////////////////////////////////////////////////////////////////

	template <typename T>
	awaitable<T> awaitable_promise<T>::get_return_object()
	{
		auto result = awaitable<T>{std::coroutine_handle<awaitable_promise<T>>::from_promise(*this)};
		return result;
	}
} // namespace ucoro

//////////////////////////////////////////////////////////////////////////

namespace ucoro
{

	template <typename T, typename CallbackFunction>
	struct CallbackAwaiter
	{
	public:
		explicit CallbackAwaiter(CallbackFunction &&callback_function)
			: callback_function_(std::move(callback_function))
		{
		}

		constexpr bool await_ready() noexcept
		{
			return false;
		}

		auto await_suspend(std::coroutine_handle<> handle)
		{
			callback_function_([this](T t) mutable { result_ = std::move(t); });
			return handle;
		}

		T await_resume() noexcept
		{
			return std::move(result_);
		}

	private:
		CallbackFunction callback_function_;
		T result_;
	};

	template <typename CallbackFunction>
	struct CallbackAwaiter<void, CallbackFunction>
	{
	public:
		explicit CallbackAwaiter(CallbackFunction &&callback_function)
			: callback_function_(std::move(callback_function))
		{
		}

		constexpr bool await_ready() noexcept
		{
			return false;
		}

		auto await_suspend(std::coroutine_handle<> handle)
		{
			callback_function_([]() {});
			return handle;
		}

		void await_resume() noexcept
		{
		}

	private:
		CallbackFunction callback_function_;
	};

	//////////////////////////////////////////////////////////////////////////

	template <typename T, typename CallbackFunction>
	struct ExecutorAwaiter
	{
	public:
		explicit ExecutorAwaiter(CallbackFunction &&callback_function)
			: callback_function_(std::move(callback_function))
		{
		}

		constexpr bool await_ready() noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle)
		{
			callback_function_([handle = std::move(handle), this](T t) mutable
			{
				result_ = std::move(t);
				handle.resume();
			});
		}

		T await_resume() noexcept
		{
			return std::move(result_);
		}

	private:
		CallbackFunction callback_function_;
		T result_;
	};

	template <typename CallbackFunction>
	struct ExecutorAwaiter<void, CallbackFunction> : std::suspend_always
	{
	public:
		explicit ExecutorAwaiter(CallbackFunction &&callback_function)
			: callback_function_(std::move(callback_function))
		{
		}

		void await_suspend(std::coroutine_handle<> handle)
		{
			callback_function_(handle);
		}

	private:
		CallbackFunction callback_function_;
	};
} // namespace ucoro

//////////////////////////////////////////////////////////////////////////

template <typename T, typename callback>
ucoro::CallbackAwaiter<T, callback> callback_awaitable(callback &&cb)
{
	return ucoro::CallbackAwaiter<T, callback>{std::forward<callback>(cb)};
}

template <typename T, typename callback>
ucoro::ExecutorAwaiter<T, callback> executor_awaitable(callback &&cb)
{
	return ucoro::ExecutorAwaiter<T, callback>{std::forward<callback>(cb)};
}

template <typename Awaitable, typename Local>
void coro_start(Awaitable &&coro, Local &&local)
{
	coro.detach(local);
}

template <typename Awaitable>
void coro_start(Awaitable &&coro)
{
	coro.detach();
}
