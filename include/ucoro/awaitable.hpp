//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <coroutine>
#include <functional>
#include <type_traits>

#if defined(DEBUG) || defined(_DEBUG)
#include <unordered_set>
std::unordered_set<void *> debug_coro_count;
#endif

namespace ucoro
{
	template <typename T>
	struct awaitable;

	template <typename T>
	struct awaitable_promise;

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
	// 返回 T 的协程 awaitable_promise 实现.

	// Promise 类型实现...
	template <typename T>
	struct awaitable_promise
	{
		awaitable<T> get_return_object();

		auto initial_suspend()
		{
			return std::suspend_always{};
		}

		auto final_suspend() noexcept
		{
			return final_awaitable<T>{};
		}

		void unhandled_exception()
		{
		}

		template <typename V>
		void return_value(V &&val) noexcept
		{
			value_ = std::forward<V>(val);
		}

		void reset_handle(std::coroutine_handle<> h)
		{
			continuation_ = h;
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

		std::coroutine_handle<> continuation_;
		T value_; // 用于存储协程返回的值
	};

	//////////////////////////////////////////////////////////////////////////
	// 返回 void 的协程偏特化 awaitable_promise 实现

	template <>
	struct awaitable_promise<void>
	{
		awaitable<void> get_return_object();

		auto initial_suspend()
		{
			return std::suspend_always{};
		}

		auto final_suspend() noexcept
		{
			return final_awaitable<void>{};
		}

		void unhandled_exception()
		{
		}
		void return_void()
		{
		}

		void reset_handle(std::coroutine_handle<> h)
		{
			continuation_ = h;
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

		std::coroutine_handle<> continuation_;
	};

	//////////////////////////////////////////////////////////////////////////

	// awaitable 协程包装...
	template <typename T>
	struct awaitable
	{
		using promise_type = awaitable_promise<T>;

		awaitable(std::coroutine_handle<promise_type> h) : current_coro_handle_(h)
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

		auto await_suspend(std::coroutine_handle<> continuation)
		{
			current_coro_handle_.promise().reset_handle(continuation);
			return current_coro_handle_;
		}

		void detach()
		{
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
	CallbackAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
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
	CallbackAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
	{
	}

	bool await_ready() noexcept
	{
		return false;
	}

	auto await_suspend(std::coroutine_handle<> handle)
	{
		callback_function_([this]() {});
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
struct ManualAwaiter
{
public:
	ManualAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
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
struct ManualAwaiter<void, CallbackFunction>
{
public:
	ManualAwaiter(CallbackFunction &&callback_function) : callback_function_(std::move(callback_function))
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
ManualAwaiter<T, callback> manual_awaitable(callback &&cb)
{
	return ManualAwaiter<T, callback>{std::forward<callback>(cb)};
}

template <typename Awaitable>
void coro_start(Awaitable &&a)
{
	a.detach();
}
