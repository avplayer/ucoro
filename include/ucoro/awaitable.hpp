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
#include <cassert>

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
		// 用于判定 T 是否是一个 U<anytype> 的类型
		// 比如
		// is_instance_of_v<std::vector<int>,std::vector>;  // true
		// is_instance_of_v<std::vector<int>,std::list>;  // false
		template<class T, template<class...> class U>
		inline constexpr bool is_instance_of_v = std::false_type{};
		template<template<class...> class U, class... Vs>
		inline constexpr bool is_instance_of_v<U<Vs...>,U> = std::true_type{};

		template<class LocalStorage>
		struct local_storage_value_type;

		template<class ValueType>
		struct local_storage_value_type<local_storage_t<ValueType>>
		{
			typedef ValueType value_type;
		};

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

		T get_value() const
		{
			if (std::holds_alternative<std::exception_ptr>(value_))
			{
				std::rethrow_exception(std::get<std::exception_ptr>(value_));
			}

			return std::get<T>(value_);
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

		void get_value()
		{
			if (exception_)
			{
				std::rethrow_exception(exception_);
			}
		}
	};

	//////////////////////////////////////////////////////////////////////////

	template<typename T>
	struct final_awaitable : std::suspend_always
	{
		std::coroutine_handle<> await_suspend(std::coroutine_handle<awaitable_promise<T>> h) noexcept
		{
			if (h.promise().continuation_)
			{
				// continuation_ 不为空，则 说明 .detach() 被 co_await
				// 因此，awaitable_detached 析构的时候会顺便撤销自己，所以这里不用 destory
				// 返回 continuation_，以便让协程框架调用 continuation_.resume()
				// 这样就把等它的协程唤醒了.
				return h.promise().continuation_;
			}
			// 并且，如果协程处于 .detach() 而没有被 co_await
			// 则异常一直存储在 promise 里，并没有代码会去调用他的 await_resume() 重抛异常
			// 所以这里重新抛出来，避免有被静默吞并的异常
			h.promise().get_value();
			// 如果 continuation_ 为空，则说明 .detach() 没有被 co_await
			// 因此，awaitable_detached 对象其实已经析构
			// 所以必须主动调用 destroy() 以免内存泄漏.
			h.destroy();
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

		void set_local(std::any local)
		{
			local_ = std::make_shared<std::any>(std::move(local));
		}

		template<typename localtype>
		struct local_storage_awaiter
		{
			const awaitable_promise* this_;

			constexpr bool await_ready() const noexcept { return true; }
			constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}

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

		template<typename A>
		auto await_transform(A&& awaiter) const
		{
			if constexpr ( detail::is_instance_of_v<std::decay_t<A>, local_storage_t> )
			{
				return local_storage_awaiter<typename detail::local_storage_value_type<std::decay_t<A>>::value_type>{this};
			}
			else if constexpr ( detail::is_awaiter_v<std::decay_t<A>> )
			{
				static_assert(std::is_rvalue_reference_v<decltype(awaiter)>, "co_await must be used on rvalue");
				return std::forward<A>(awaiter);
			}
			else
			{
				return await_transformer<A>::await_transform(std::move(awaiter));
			}
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

		explicit awaitable(std::coroutine_handle<promise_type> h)
			: current_coro_handle_(h)
		{
		}

		~awaitable()
		{
			if (current_coro_handle_)
			{
				if (current_coro_handle_.done())
				{
					current_coro_handle_.destroy();
				}
				else
				{
					current_coro_handle_.resume();
				}
			}
		}

		awaitable(awaitable&& t) noexcept
			: current_coro_handle_(t.current_coro_handle_)
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

		constexpr bool await_ready() const noexcept
		{
			return false;
		}

		T await_resume()
		{
			return current_coro_handle_.promise().get_value();
		}

		template<typename PromiseType>
		auto await_suspend(std::coroutine_handle<PromiseType> continuation)
		{
			if constexpr (detail::is_instance_of_v<PromiseType, awaitable_promise>)
			{
				current_coro_handle_.promise().local_ = continuation.promise().local_;
			}

			current_coro_handle_.promise().continuation_ = continuation;
			return current_coro_handle_;
		}

		void set_local(std::any local)
		{
			assert("local has value" && !current_coro_handle_.promise().local_);
			current_coro_handle_.promise().set_local(std::move(local));
		}

		auto detach()
		{
			auto launch_coro = [](awaitable<T> lazy) -> awaitable<T> { co_return co_await std::move(lazy); };
			return launch_coro(std::move(*this));
		}

		auto detach(std::any local)
		{
			auto launched_coro = [](awaitable<T> lazy) mutable -> awaitable<T>
			{
				co_return co_await std::move(lazy);
			}(std::move(*this));

			if (local.has_value())
			{
				launched_coro.set_local(local);
			}

			return launched_coro;
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
		void resume_coro(std::coroutine_handle<> handle)
		{
			if (executor_detect_flag_->test_and_set())
			{
				// 如果执行到这里，说明 executor_detect_flag_ 运行在 callback_function_ 返回之后，所以也就
				// 是说运行在 executor 中。
				handle.resume();
			}
		}

		bool await_suspend(std::coroutine_handle<> handle)
		{
			executor_detect_flag_ = std::make_unique<std::atomic_flag>();

			auto exception_detect_flag = std::make_shared<std::atomic_flag>();

			try
			{
				if constexpr (std::is_void_v<T>)
				{
					callback_function_([this, handle, exception_detect_flag]() mutable
					{
						if (exception_detect_flag->test_and_set())
							return;
						return resume_coro(handle);
					});
				}
				else
				{
					callback_function_([this, handle, exception_detect_flag](T t) mutable
					{
						if (exception_detect_flag->test_and_set())
							return;
						this->result_ = std::move(t);
						return resume_coro(handle);
					});
				}
			}
			catch (...)
			{
				exception_detect_flag->test_and_set();

				auto e = std::current_exception();

				// 这里的 rethrow_exception 将导致当前协程直接被 resume 并将异常传递给调用者协程的 promise
				std::rethrow_exception(e);

				// 不可到达.
				for (;;);
			}

			if (executor_detect_flag_->test_and_set())
			{
				// 如果执行到这里，说明 resume_coro 已经被执行，这里分 2 种情况:
				//
				// 第一种情况就是在 executor 线程中执行了 resume_coro，executor 线程快于当前线程。
				//
				// executor 线程快于当前线程的情况下，resume_coro 什么都不会做，仅仅只设置 executor_detect_flag_
				//
				// 如果 executor 线程慢于当前线程，则上面的 executor_detect_flag_.test_and_set() 会
				// 返回 false 并设置为 true，然后便会执行 else 部分的 return std::noop_coroutine();
				// 在此后的 executor_detect_flag_ 中，因为 executor_detect_flag_.test_and_set() 为
				// true 将会 resume 协程。
				//
				// 第二种情况就是 resume_coro 直接被 callback_function_ 调用，resume_coro 函数也仅仅
				// 只设置 executor_detect_flag_ 为 true 不作任何事情，在 callback_function_ 返回后
				// 上面的 if (executor_detect_flag_->test_and_set()) 语句将为 true 而执行下面的
				// return false; 语句。
				// 返回 false 等同于 handle.resume() 但是不会爆栈.
				return false;
			}
			else
			{
				// 如果执行到这里，说明 resume_coro 肯定没被执行，说明协程唤醒是由 executor 驱动，此时
				// 即返回 true 即可.
				// 返回 true 等同于不调用 handle.resume(), 于是执行流程会最终返回 executor 的循环事件
				// 里。至于协程何时恢复，就要等 resume_coro 被调用啦.
				return true;
			}
		}

	private:
		CallbackFunction callback_function_;
		std::unique_ptr<std::atomic_flag> executor_detect_flag_;
	};

} // namespace ucoro

//////////////////////////////////////////////////////////////////////////

template<typename T, typename callback>
ucoro::CallbackAwaiter<T, callback> callback_awaitable(callback&& cb)
{
	return ucoro::CallbackAwaiter<T, callback>{std::forward<callback>(cb)};
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
