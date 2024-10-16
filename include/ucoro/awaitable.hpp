//
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
#include <cassert>
#include <cstdlib>
#include <functional>
#include <memory>
#include <type_traits>
#include <atomic>

#if defined(DEBUG) || defined(_DEBUG)
#if defined(ENABLE_DEBUG_CORO_LEAK)

#define DEBUG_CORO_PROMISE_LEAK

#include <unordered_set>
inline std::unordered_set<void*> debug_coro_leak;

#endif
#endif

namespace ucoro
{
	template<typename T>
	struct await_transformer;

	template<typename T>
	struct awaitable;

	template<typename T>
	struct awaitable_promise;

	template<typename T, typename CallbackFunction>
	struct ExecutorAwaiter;

	template<typename T, typename CallbackFunction>
	struct CallbackAwaiter;

	template<typename T>
	struct local_storage_t
	{
	};
	inline constexpr local_storage_t<void> local_storage;

	//////////////////////////////////////////////////////////////////////////
	namespace detail
	{
		template<typename T>
		concept is_valid_await_suspend_return_value =
			std::convertible_to<T, std::coroutine_handle<>> || std::is_void_v<T> || std::is_same_v<T, bool>;

		// 用于判定 T 是否是一个 awaiter 的类型, 即: 拥有 await_ready，await_suspend，await_resume 成员函数的结构或类.
		template<typename T>
		concept is_awaiter_v = requires (T a) {
			{ a.await_ready() } -> std::convertible_to<bool>;
			{ a.await_suspend(std::coroutine_handle<>{}) } -> is_valid_await_suspend_return_value;
			{ a.await_resume() };
		};

		// 用于判定 T 是否是一个 awaitable<>::promise_type 的类型, 即: 拥有 local_ 成员。
		template<typename T>
		concept is_awaitable_promise_type_v = requires (T a) {
			{ a.local_ } -> std::convertible_to<std::shared_ptr<std::any>>;
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
	// 存储协程 promise 的返回值
	template<typename T>
	struct awaitable_promise_value
	{
		template<typename V>
		void return_value(V&& val) noexcept
		{
			value_.template emplace<T>(std::forward<V>(val));
		}

		void unhandled_exception() noexcept
		{
			value_.template emplace<std::exception_ptr>(std::current_exception());
		}

		std::variant<std::exception_ptr, T> value_{nullptr};
	};

	//////////////////////////////////////////////////////////////////////////
	// 存储协程 promise 的返回值 void 的特化实现
	template<>
	struct awaitable_promise_value<void>
	{
		std::exception_ptr exception_{nullptr};

		constexpr void return_void() noexcept
		{
		}

		void unhandled_exception() noexcept
		{
			exception_ = std::current_exception();
		}
	};

	//////////////////////////////////////////////////////////////////////////
	// 使用 .detach() 后创建的独立的协程的入口点
	// 由它开始链式使用 awaitable<>
	template<typename T = void>
	struct awaitable_detached
	{
		awaitable_detached(const awaitable_detached&) = delete;

		struct promise_type : public awaitable_promise_value<T>, public debug_coro_promise
		{
			awaitable_detached get_return_object()
			{
				return awaitable_detached{std::coroutine_handle<promise_type>::from_promise(*this)};
			}

			struct final_awaiter : std::suspend_always
			{
				std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> final_coro_handle) const noexcept
				{
					if (final_coro_handle.promise().continuation_)
					{
						// continuation_ 不为空，则 说明 .detach() 被 co_await
						// 因此，awaitable_detached 析构的时候会顺便撤销自己，所以这里不用 destory
						// 返回 continuation_，以便让协程框架调用 continuation_.resume()
						// 这样就把等它的协程唤醒了.
						return final_coro_handle.promise().continuation_;
					}
					// 如果 continuation_ 为空，则说明 .detach() 没有被 co_await
					// 因此，awaitable_detached 对象其实已经析构
					// 所以必须主动调用 destroy() 以免内存泄漏.
					final_coro_handle.destroy();
					return std::noop_coroutine();
				}
			};

			auto initial_suspend() noexcept
			{
				return std::suspend_always{};
			}

			auto final_suspend() noexcept
			{
				return final_awaiter{};
			}

			// 对 detached 的 coro 调用 co_await 相当于 thread.join()
			// 因此记录这个 continuation 以便在 final awaiter 里唤醒
			std::coroutine_handle<> continuation_;
		};

		explicit awaitable_detached(std::coroutine_handle<promise_type> promise_handle)
			: current_coro_handle_(promise_handle)
		{
		}

		awaitable_detached(awaitable_detached&& other)
			: current_coro_handle_(other.current_coro_handle_)
		{
			other.current_coro_handle_ = nullptr;
		}

		~awaitable_detached()
		{
			if (current_coro_handle_)
			{
				if (current_coro_handle_.done())
				{
					current_coro_handle_.destroy();
				}
				else
				{
					// 由于 initial_supend 为 suspend_always
					// 因此 如果不对 .detach() 的返回值调用 co_await
					// 此协程将不会运行。
					// 因此，在本对象析构时，协程其实完全没运行过。
					// 正因为本对象析构的时候，协程都没有运行，就意味着
					// 其实用户只是调用了 .detach() 并没有对返回值进行
					// co_await 操作。
					// 因此为了能把协程运行起来，这里强制调用 resume
					current_coro_handle_.resume();
				}
			}
		}

		bool await_ready() noexcept
		{
			return false;
		}

		auto await_suspend(std::coroutine_handle<> continuation) noexcept
		{
			current_coro_handle_.promise().continuation_ = continuation;
			return current_coro_handle_;
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
			}
			else
			{
				auto ret = std::move(current_coro_handle_.promise().value_);
				if (std::holds_alternative<std::exception_ptr>(ret))
				{
					std::rethrow_exception(std::get<std::exception_ptr>(ret));
				}

				return std::get<T>(ret);
			}
		}

		std::coroutine_handle<promise_type> current_coro_handle_;
	};

	//////////////////////////////////////////////////////////////////////////

	template<typename T>
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
	// 返回 T 的协程 awaitable_promise 实现.

	// Promise 类型实现...
	template<typename T>
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

		template<typename A> requires (detail::is_awaiter_v<std::decay_t<A>>)
		auto await_transform(A&& awaiter) const
		{
			static_assert(std::is_rvalue_reference_v<decltype(awaiter)>, "co_await must be used on rvalue");
			return std::forward<A>(awaiter);
		}

		template<typename A> requires (!detail::is_awaiter_v<std::decay_t<A>>)
		auto await_transform(A&& awaiter) const
		{
			return await_transformer<A>::await_transform(std::move(awaiter));
		}

		void set_local(std::any local)
		{
			local_ = std::make_shared<std::any>(local);
		}

		template<typename localtype>
		struct local_storage_awaiter
		{
			awaitable_promise* this_;

			constexpr bool await_ready() const noexcept
			{
				return true;
			}
			void await_suspend(std::coroutine_handle<void>) noexcept
			{
			}

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

		template<typename localtype>
		auto await_transform(local_storage_t<localtype>)
		{
			return local_storage_awaiter<localtype>{this};
		}

		std::coroutine_handle<> continuation_;
		std::shared_ptr<std::any> local_;
	};

	//////////////////////////////////////////////////////////////////////////

	// awaitable 协程包装...
	template<typename T>
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

		awaitable(awaitable&& t) noexcept : current_coro_handle_(t.current_coro_handle_)
		{
			t.current_coro_handle_ = nullptr;
		}

		awaitable& operator=(awaitable&& t) noexcept
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

		awaitable(const awaitable&) = delete;
		awaitable(awaitable&) = delete;
		awaitable& operator=(const awaitable&) = delete;
		awaitable& operator=(awaitable&) = delete;

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

		template<typename PromiseType>
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

		auto detach()
		{
			auto launch_coro = [](awaitable<T> lazy) -> awaitable_detached<T> { co_return co_await lazy; };
			return launch_coro(std::move(*this));
		}

		auto detach(std::any local)
		{
			if (local.has_value())
			{
				set_local(local);
			}

			auto launch_coro = [](awaitable<T> lazy) -> awaitable_detached<T> { co_return co_await lazy; };
			return launch_coro(std::move(*this));
		}

		std::coroutine_handle<promise_type> current_coro_handle_;
	};

	//////////////////////////////////////////////////////////////////////////

	template<typename T>
	awaitable<T> awaitable_promise<T>::get_return_object()
	{
		auto result = awaitable<T>{std::coroutine_handle<awaitable_promise<T>>::from_promise(*this)};
		return result;
	}
} // namespace ucoro

//////////////////////////////////////////////////////////////////////////

namespace ucoro
{
	template<typename T>
	struct CallbackAwaiterBase
	{
		T await_resume() noexcept
		{
			return std::move(result_);
		}

		T result_;
	};

	template<>
	struct CallbackAwaiterBase<void>
	{
		void await_resume() noexcept
		{
		}
	};

	template<typename T, typename CallbackFunction>
	struct CallbackAwaiter : public CallbackAwaiterBase<T>
	{
		CallbackAwaiter(const CallbackAwaiter&) = delete;
		CallbackAwaiter& operator = (const CallbackAwaiter&) = delete;
	public:
		explicit CallbackAwaiter(CallbackFunction&& callback_function)
			: callback_function_(std::forward<CallbackFunction>(callback_function))
		{
		}

		CallbackAwaiter(CallbackAwaiter&&) = default;

		constexpr bool await_ready() noexcept
		{
			return false;
		}

		// 用户调用 handle( ret_value ) 就是在这里执行的.
		void resume_coro(std::coroutine_handle<> handle, std::shared_ptr<std::atomic_flag> executor_detect_flag)
		{
			if (executor_detect_flag->test_and_set())
			{
				// 如果执行到这里，说明 executor_detect_flag 运行在 callback_function_ 返回之后，所以也就
				// 是说运行在 executor 中。
				handle.resume();
			}
		}

		std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle)
		{
			auto executor_detect_flag = std::make_shared<std::atomic_flag>();

			if constexpr (std::is_void_v<T>)
			{
				callback_function_([this, handle, executor_detect_flag]() mutable
				{
					return resume_coro(handle, executor_detect_flag);
				});
			}
			else
			{
				callback_function_([this, executor_detect_flag, handle](T t) mutable
				{
					this->result_ = std::move(t);
					return resume_coro(handle, executor_detect_flag);
				});
			}

			if (executor_detect_flag->test_and_set())
			{
				// 如果执行到这里，说明 executor_detect_flag 已经被执行，这里分 2 种情况:
				//
				// 第一种情况就是
				// 在 executor 线程中执行了 executor_detect_flag executor 线程快于当前线程。
				//
				// executor 线程快于当前线程的情况下，executor_detect_flag 什么都不会做，仅仅只设置 flag。
				// 如果 executor 线程慢于当前线程，则上面的 flag.test_and_set() 会返回 false 并
				// 设置 flag，然后执行 return std::noop_coroutine(); 在此后的 executor_detect_flag 中
				// 因为 flag.test_and_set() 为 true 将会 resume 协程。
				//
				// 第二种情况就是 executor_detect_flag 直接被 callback_function_ executor_detect_flag
				// 也仅仅只设置 flag。
				//
				// 无论哪一种情况，我们都可以在这里直接返回 handle 让协程框架维护协程 resume。
				return handle;
			}
			else
			{
				// 如果执行到这里，说明 executor_detect_flag 肯定没被执行，说明是由 executor 驱动.
				// executor 驱动即返回 noop_coroutine 即可.
				return std::noop_coroutine();
			}
		}

	private:
		CallbackFunction callback_function_;
	};

	//////////////////////////////////////////////////////////////////////////

	template<typename T, typename CallbackFunction>
	struct ExecutorAwaiter : public CallbackAwaiterBase<T>
	{
	public:
		explicit ExecutorAwaiter(CallbackFunction&& callback_function)
			: callback_function_(std::move(callback_function))
		{
		}

		constexpr bool await_ready() noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle)
		{
			if constexpr (std::is_void_v<T>)
			{
				callback_function_(handle);
			}
			else
			{
				callback_function_([handle = std::move(handle), this](T t) mutable
				{
					this->result_ = std::move(t);
					handle.resume();
				});
			}
		}

	private:
		CallbackFunction callback_function_;
	};

} // namespace ucoro

//////////////////////////////////////////////////////////////////////////

template<typename T, typename callback>
ucoro::CallbackAwaiter<T, callback> callback_awaitable(callback&& cb)
{
	return ucoro::CallbackAwaiter<T, callback>{std::forward<callback>(cb)};
}

template<typename T, typename callback>
ucoro::ExecutorAwaiter<T, callback> executor_awaitable(callback&& cb)
{
	return ucoro::ExecutorAwaiter<T, callback>{std::forward<callback>(cb)};
}

template<typename Awaitable, typename Local>
auto coro_start(Awaitable&& coro, Local&& local)
{
	return coro.detach(local);
}

template<typename Awaitable>
auto coro_start(Awaitable&& coro)
{
	return coro.detach();
}
