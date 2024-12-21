//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <concepts>
#include <any>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <atomic>
#ifdef DISABLE_EXCEPTION
#include <optional>
#else
#include <variant>
#endif

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
	struct CallbackAwaiter;

	template<typename T>
	struct local_storage_t
	{
	};

	inline constexpr local_storage_t<void> local_storage;

	//////////////////////////////////////////////////////////////////////////
	namespace concepts
	{
		// 类型是否是 local_storage_t<> 的一种
		template<typename T>
		concept local_storage_type = requires(T x)
		{
			{ local_storage_t{x} } -> std::same_as<T>;
		};

		// 类型是否是 awaitable<> 的一种
		template<typename T>
		concept awaitable_type = requires(T x)
		{
			{ awaitable{x} } -> std::same_as<T>;
		};

		// 类型是否是 awaitable_promise<> 的一种
		template<typename T>
		concept awaitable_promise_type = requires(T x)
		{
			{ awaitable_promise{x} } -> std::same_as<T>;
		};

		// await_suspend 有三种返回值
		template<typename T>
		concept is_valid_await_suspend_return_value = std::convertible_to<T, std::coroutine_handle<>> ||
													std::is_void_v<T> ||
													std::is_same_v<T, bool>;

		// 用于判定 T 是否是一个 awaiter 的类型, 即: 拥有 await_ready，await_suspend，await_resume 成员函数的结构或类.
		template<typename T>
		concept is_awaiter_v = requires (T a)
		{
			{ a.await_ready() } -> std::same_as<bool>;
			{ a.await_suspend(std::coroutine_handle<>{}) } -> is_valid_await_suspend_return_value;
			{ a.await_resume() };
		};

		template<typename T>
		concept has_operator_co_await = requires (T a)
		{
			{ a.operator co_await() } -> is_awaiter_v;
 		};

		// 用于判定 T 是可以用在 co_await 后面
		template<typename T>
		concept is_awaitable_v = is_awaiter_v<typename std::decay_t<T>> ||
									awaitable_type<T> ||
		 							has_operator_co_await<typename std::decay_t<T>>;


		template<typename T>
		concept has_user_defined_await_transformer = requires (T&& a)
		{
			await_transformer<T>::await_transform(std::move(a));
		};

		template<typename T>
		struct is_not_awaitable : std::false_type{};

	} // namespace concepts

	namespace traits
	{
		//////////////////////////////////////////////////////////////////////////
		// 用于从 A = U<T> 类型里提取 T 参数
		// 比如
		// template_parameter_of<local_storage_t<int>, local_storage_t>;  // int
		// template_parameter_of<decltype(local_storage), local_storage_t>;  // void
		//
		// 首先定义一个接受 template_parameter_of<Testee, FromTemplate> 这样的一个默认模板萃取
		template<typename Testee, template<typename> typename FromTemplate>
		struct template_parameter_traits;

		// 接着定义一个偏特化，匹配 template_parameter_traits<模板名<参数>, 模板名>
		// 这样，这个偏特化的 template_parameter_traits 就有了一个
		// 名为 template_parameter 的成员类型，其定义的类型就是 _template_parameter
		// 于是就把 TemplateParameter 这个类型给萃取出来了
		template<template<typename> typename ClassTemplate, typename TemplateParameter>
		struct template_parameter_traits<ClassTemplate<TemplateParameter>, ClassTemplate>
		{
			using template_parameter = TemplateParameter ;
		};

		// 最后，定义一个简化用法的 using 让用户的地方代码变短点
		template<typename TesteeType, template<typename> typename FromTemplate>
		using template_parameter_of = typename template_parameter_traits<
									std::decay_t<TesteeType>, FromTemplate>::template_parameter;

		// 利用 通用工具 template_parameter_of 萃取 local_storage_t<T> 里的 T
		template<concepts::local_storage_type LocalStorage>
		using local_storage_value_type = template_parameter_of<LocalStorage, local_storage_t>;


		// 利用 通用工具 template_parameter_of 萃取 awaitable<T> 里的 T
		template<concepts::awaitable_type AwaitableType>
		using awaitable_return_type = template_parameter_of<AwaitableType, awaitable>;

		template<typename T>
		struct exception_with_result
		{
			using type = std::variant<std::exception_ptr, T>;
		};

		template<>
		struct exception_with_result<void>
		{
			using type = std::exception_ptr;
		};

		template<typename T>
		using exception_with_result_t = typename exception_with_result<T>::type;


	} // namespace traits

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
#ifndef DISABLE_EXCEPTION
			value_.template emplace<std::exception_ptr>(std::current_exception());
#endif
		}

		T get_value() const
		{
#ifndef DISABLE_EXCEPTION
			if (std::holds_alternative<std::exception_ptr>(value_))
			{
				std::rethrow_exception(std::get<std::exception_ptr>(value_));
			}

			return std::get<T>(value_);
#else
			return value_.value();
#endif
		}
#ifndef DISABLE_EXCEPTION
		std::variant<std::exception_ptr, T> value_{nullptr};
#else
		std::optional<T> value_;
#endif
	};

	//////////////////////////////////////////////////////////////////////////
	// 存储协程 promise 的返回值 void 的特化实现
	template<>
	struct awaitable_promise_value<void>
	{
#ifndef DISABLE_EXCEPTION
		std::exception_ptr exception_{nullptr};
#endif
		constexpr void return_void() noexcept
		{
		}

		void unhandled_exception() noexcept
		{
#ifndef DISABLE_EXCEPTION
			exception_ = std::current_exception();
#endif
		}

		void get_value() const
		{
#ifndef DISABLE_EXCEPTION
			if (exception_)
			{
				std::rethrow_exception(exception_);
			}
#endif
		}
	};

	//////////////////////////////////////////////////////////////////////////

	template<typename T>
	struct final_awaitable
	{
		awaitable_promise<T> * parent;

		constexpr void await_resume() noexcept
		{
			// 并且，如果协程处于 .detach() 而没有被 co_await
			// 则异常一直存储在 promise 里，并没有代码会去调用他的 await_resume() 重抛异常
			// 所以这里重新抛出来，避免有被静默吞并的异常
			parent->get_value();
		}

		bool await_ready() noexcept
		{
			// continuation_ 不为空，则 说明 .detach() 被 co_await, 则
			// 返回 continuation_，以便让协程框架调用 continuation_.resume()
			// 这样就把等它的协程唤醒了.
			return !parent->continuation_;
			// 如果 continuation_ 为空，则说明此乃调用链上的最后一个 promise
			// 返回 true 让协程框架 自动调用 coroutine_handle::destory()
		}

		std::coroutine_handle<> await_suspend(std::coroutine_handle<awaitable_promise<T>> h) noexcept
		{
			return h.promise().continuation_;
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
			return final_awaitable<T>{this};
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
			if constexpr (concepts::local_storage_type<std::decay_t<A>>)
			{
				// 调用 co_await local_storage_t<T>
				return local_storage_awaiter<traits::local_storage_value_type<std::decay_t<A>>>{this};
			}
			else if constexpr (concepts::is_awaitable_v<A>)
			{
				// 调用 co_await awaitable<T>; 或者其他有三件套的类型
				static_assert(std::is_rvalue_reference_v<A&&>, "co_await must be used on rvalue");
				return std::forward<A>(awaiter);
			}
			else if constexpr (concepts::has_user_defined_await_transformer<A>)
			{
				// 调用 co_await 其他写了 await_transformer 的自定义类型.
				// 例如包含了 asio_glue.hpp 后，就可以 co_await asio::awaitable<T>;
				return await_transformer<A>::await_transform(std::move(awaiter));
			}
			else
			{
				static_assert(concepts::is_not_awaitable<A>::value, "co_await must be called on an awaitable type");
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
			if constexpr (concepts::awaitable_promise_type<PromiseType>)
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

		auto detach(std::any local = {})
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

		template<typename Function> requires std::is_invocable_v<Function, ucoro::traits::exception_with_result_t<T>>
		auto detach_with_callback(Function completion_handler)
		{
			return detach_with_callback<Function>(std::any{}, completion_handler);
		}

		template<typename Function> requires std::is_invocable_v<Function, ucoro::traits::exception_with_result_t<T>>
		auto detach_with_callback(std::any local, Function completion_handler)
		{
			auto launched_coro = [](awaitable<T> lazy, auto completion_handler) mutable -> awaitable<void>
			{
				using result_wrapper = ucoro::traits::exception_with_result_t<T>;
				try
				{
					if constexpr (std::is_void_v<T>)
					{
						co_await std::move(lazy);
						completion_handler(result_wrapper{nullptr});
					}
					else
					{
						completion_handler(result_wrapper{co_await std::move(lazy)});
					}
				}
				catch(...)
				{
					completion_handler(result_wrapper{std::current_exception()});
				}
			}(std::move(*this), std::move(completion_handler));

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
auto callback_awaitable(callback&& cb) -> ucoro::awaitable<T>
{
	co_return co_await ucoro::CallbackAwaiter<T, callback>{std::forward<callback>(cb)};
}

template<typename Awaitable, typename Local, typename CompleteFunction>
auto coro_start(Awaitable&& coro, Local&& local, CompleteFunction completer)
{
	return coro.detach_with_callback(local, completer);
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

template<typename T>
auto sync_await(ucoro::awaitable<T> lazy, std::any local_ = {}) -> T
{
	ucoro::traits::exception_with_result_t<T> result;

	lazy.detach_with_callback(local_, [&](ucoro::traits::exception_with_result_t<T> result_)
	{
		result = result_;
	});

	if constexpr (std::is_void_v<T>)
	{
		if (result)
		{
			std::rethrow_exception(result);
		}
	}
	else
	{
		if (std::holds_alternative<std::exception_ptr>(result))
		{
			std::rethrow_exception(std::get<std::exception_ptr>(result));
		}
		return std::get<T>(result);
	}
}
