//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <concepts>
#include <coroutine>
#include <functional>
#include <type_traits>
#include <memory>
#include <any>
#include <cassert>

#if defined(DEBUG) || defined(_DEBUG)
#include <unordered_set>
inline std::unordered_set<void *> debug_coro_count;
#endif

namespace ucoro
{
	template <typename T>
	struct awaitable;

	template <typename T>
	struct awaitable_promise;

	template <typename T, typename CallbackFunction>
	struct ExecutorAwaiter;

	template <typename T, typename CallbackFunction>
	struct CallbackAwaiter;

	template <typename T>
	struct local_storage_t {};
	inline constexpr local_storage_t<void> local_storage;


	//////////////////////////////////////////////////////////////////////////
	/// is_awaiter from https://github.com/lewissbaker/cppcoro/blob/master/include/cppcoro/detail/is_awaiter.hpp
	namespace detail
	{
		// NOTE: We're accepting a return value of coroutine_handle<P> here
		// which is an extension supported by Clang which is not yet part of
		// the C++ coroutines TS.
		template <typename T>
		concept is_valid_await_suspend_return_value = std::convertible_to<T, std::coroutine_handle<>> || std::is_void_v<T> || std::is_same_v<T, bool>;

		// NOTE: We're testing whether await_suspend() will be callable using an
		// arbitrary coroutine_handle here by checking if it supports being passed
		// a coroutine_handle<void>. This may result in a false-result for some
		// types which are only awaitable within a certain context.
		template <typename T>
		concept is_awaiter_v = requires ( T a)
		{
			{ a.await_ready() } -> std::convertible_to<bool>;
			{ a.await_suspend(std::coroutine_handle<>{}) } ->  is_valid_await_suspend_return_value;
			{ a.await_resume() };
		};
	} // namespace detail


	//////////////////////////////////////////////////////////////////////////
	struct awaitable_detached
	{
		struct promise_type
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

#if defined(DEBUG) || defined(_DEBUG)
			void *operator new(std::size_t size)
			{
				void *ptr = malloc(size);
				if (!ptr)
				{
					throw std::bad_alloc{};
				}
				debug_coro_count.insert(ptr);
				return ptr;
			}

			void operator delete(void *ptr, std::size_t size)
			{
				debug_coro_count.erase(ptr);
				(void)size;
				free(ptr);
			}
#endif
		};
	};

	//////////////////////////////////////////////////////////////////////////

	template <typename T>
	struct final_awaitable
	{
		bool await_ready() noexcept
		{
			return false;
		}
		void await_resume() noexcept
		{
		}
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
	//

	struct awaitable_promise_base
	{
		auto initial_suspend() {
			return std::suspend_always{};
		}

		void unhandled_exception() {}

		template <typename A> requires( detail::is_awaiter_v<A> )
		auto await_transform(A awaiter) const
		{
			return awaiter;
		}

		auto await_transform(local_storage_t<void>) noexcept
		{
			struct result
			{
				awaitable_promise_base* this_;

				bool await_ready() const noexcept
				{
					return true;
				}

				void await_suspend(std::coroutine_handle<void>) noexcept
				{
				}

				auto await_resume() const noexcept
				{
					return *this_->local_;
				}
			};

			return result{ this };
		}

		template <typename T>
		auto await_transform(local_storage_t<T>)
		{
			struct result
			{
				awaitable_promise_base* this_;

				bool await_ready() const noexcept
				{
					return true;
				}

				void await_suspend(std::coroutine_handle<void>) noexcept
				{
				}

				auto await_resume()
				{
					return std::any_cast<T>(*this_->local_);
				}
			};

			return result{ this };
		}

		std::coroutine_handle<> continuation_;
		std::shared_ptr<std::any> local_;
	};

	//////////////////////////////////////////////////////////////////////////
	// 返回 T 的协程 awaitable_promise 实现.

	// Promise 类型实现...
	template <typename T>
	struct awaitable_promise : public awaitable_promise_base
	{
		awaitable<T> get_return_object();

		auto final_suspend() noexcept
		{
			return final_awaitable<T>{};
		}

		template <typename V>
		void return_value(V &&val) noexcept
		{
			value_ = std::forward<V>(val);
		}

#if defined(DEBUG) || defined(_DEBUG)
		void *operator new(std::size_t size)
		{
			void *ptr = malloc(size);
			if (!ptr)
			{
				throw std::bad_alloc{};
			}
			debug_coro_count.insert(ptr);
			return ptr;
		}

		void operator delete(void *ptr, std::size_t size)
		{
			debug_coro_count.erase(ptr);
			(void)size;
			free(ptr);
		}
#endif
		T value_; // 用于存储协程返回的值
	};

	//////////////////////////////////////////////////////////////////////////
	// 返回 void 的协程偏特化 awaitable_promise 实现

	template <>
	struct awaitable_promise<void> : public awaitable_promise_base
	{
		awaitable<void> get_return_object();

		auto final_suspend() noexcept
		{
			return final_awaitable<void>{};
		}

		void return_void()
		{
		}

#if defined(DEBUG) || defined(_DEBUG)
		void *operator new(std::size_t size)
		{
			void *ptr = malloc(size);
			if (!ptr)
			{
				throw std::bad_alloc{};
			}
			debug_coro_count.insert(ptr);
			return ptr;
		}

		void operator delete(void *ptr, std::size_t size)
		{
			debug_coro_count.erase(ptr);
			(void)size;
			free(ptr);
		}
#endif
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
		awaitable &operator=(const awaitable &) = delete;

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

		bool await_ready() const noexcept
		{
			return false;
		}

		T await_resume()
		{
			if constexpr (std::is_void_v<T>)
			{
				current_coro_handle_.destroy();
				current_coro_handle_ = nullptr;
			}
			else
			{
				auto ret = std::move(current_coro_handle_.promise().value_);
				current_coro_handle_.destroy();
				current_coro_handle_ = nullptr;

				return ret;
			}
		}

		template <typename PromiseType>
		auto await_suspend(std::coroutine_handle<PromiseType> continuation)
		{
			current_coro_handle_.promise().continuation_ = continuation;

			if constexpr (std::is_base_of_v<awaitable_promise_base, PromiseType>)
			{
				current_coro_handle_.promise().local_ = continuation.promise().local_;
			}

			return current_coro_handle_;
		}

		void set_local(std::any local)
		{
			assert("local has value" && !current_coro_handle_.promise().local_);
			current_coro_handle_.promise().local_ = std::make_shared<std::any>(local);
		}

		void detach()
		{
			auto launch_coro = [](awaitable<T> lazy) -> awaitable_detached { co_await lazy; };
			[[maybe_unused]] auto detached = launch_coro(std::move(*this));
		}

		void detach(std::any local)
		{
			if (local.has_value())
				set_local(local);

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

	awaitable<void> awaitable_promise<void>::get_return_object()
	{
		auto result = awaitable<void>{std::coroutine_handle<awaitable_promise<void>>::from_promise(*this)};
		return result;
	}
} // namespace ucoro


//////////////////////////////////////////////////////////////////////////

template <typename T, typename CallbackFunction>
struct CallbackAwaiter
{
public:
	explicit CallbackAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
	{
	}

	bool await_ready() noexcept
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
	explicit CallbackAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
	{
	}

	bool await_ready() noexcept
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

template <typename T, typename callback>
CallbackAwaiter<T, callback> callback_awaitable(callback &&cb)
{
	return CallbackAwaiter<T, callback>{std::forward<callback>(cb)};
}

//////////////////////////////////////////////////////////////////////////

template <typename T, typename CallbackFunction>
struct ExecutorAwaiter
{
public:
	explicit ExecutorAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
	{
	}

	bool await_ready() noexcept
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
struct ExecutorAwaiter<void, CallbackFunction>
{
public:
	explicit ExecutorAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
	{
	}

	bool await_ready() noexcept
	{
		return false;
	}

	void await_suspend(std::coroutine_handle<> handle)
	{
		callback_function_([handle = std::move(handle)]() mutable { handle.resume(); });
	}

	void await_resume() noexcept
	{
	}

private:
	CallbackFunction callback_function_;
};

template <typename T, typename callback>
ExecutorAwaiter<T, callback> executor_awaitable(callback &&cb)
{
	return ExecutorAwaiter<T, callback>{std::forward<callback>(cb)};
}

template <typename Awaitable, typename Local>
void coro_start(Awaitable &&a, Local&& local)
{
	a.detach(local);
}

template <typename Awaitable>
void coro_start(Awaitable &&a)
{
	a.detach();
}
