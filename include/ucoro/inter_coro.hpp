
// inter coro communication tools

// aka. channels

#pragma once

#include <deque>
#include <mutex>
#include "awaitable.hpp"

#if __has_include(<asio.hpp>)
#include <asio.hpp>
#define __HAS_ASIO_HPP
#endif

#if __has_include(<boost/asio.hpp>)
#include <boost/asio.hpp>
#define __HAS_BOOST_ASIO_HPP
#endif

namespace ucoro::communication
{

struct dummy_mutex
{
	constexpr void lock() noexcept {}
	constexpr void unlock() noexcept {}
};

/*
 * channel<>, 协程版的异步列队。默认线程不安全。
 * 使用此channel的多个协程需要被同一个线程调度。
 * 否则会导致数据损坏。需要线程安全则要在第二个参数上使用 std::mutex
 */
template<typename T, typename MUTEX = dummy_mutex>
class channel
{
	void wake_up_one_pusher()
	{
		if (m_push_awaiting.empty())
		{
			mutex.unlock();
			return;
		}
		auto top_waiter = m_push_awaiting.front();
		// wake up .
		m_push_awaiting.pop_front();
		mutex.unlock();
        top_waiter.resume();
	}

	void wake_up_one_poper()
	{
		mutex.lock();
		if (m_pop_awaiting.empty())
			return;
		auto top_waiter = m_pop_awaiting.front();
		// wake up .
		m_pop_awaiting.pop_front();
		mutex.unlock();
        top_waiter.resume();
		mutex.lock();
	}

    struct wait_on_queue
    {
		MUTEX & mutex;
        std::deque<std::coroutine_handle<>> & wait_on;

        wait_on_queue(MUTEX& mutex, std::deque<std::coroutine_handle<>> & wait_on)
			: mutex(mutex)
            , wait_on(wait_on)
        {}

        constexpr bool await_ready() noexcept { return false; }

        constexpr void await_resume() noexcept {}

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            wait_on.push_back(handle);
			mutex.unlock();
        }
    };

public:
	awaitable<T> pop()
	{
		mutex.lock();
		if (m_queue.empty())
		{
			// yield
            co_await wait_on_queue{mutex, m_pop_awaiting};
			mutex.lock();
		}
		T r = m_queue.front();
		m_queue.pop_front();
		wake_up_one_pusher();
		co_return r;
	}

	awaitable<void> push(T t)
	{
		wake_up_one_poper();
		m_queue.push_back(t);
		if (m_queue.size() > (m_max_pending))
		{
			// yield
            co_await wait_on_queue{mutex, m_push_awaiting};
		}
		else
		{
			mutex.unlock();
		}
	}

	channel(long max_pending = 1)
		: m_max_pending(max_pending)
	{
		assert(max_pending > 0);
	}

	long m_max_pending;
	std::deque<T> m_queue;
	std::deque<std::coroutine_handle<>> m_pop_awaiting;
	std::deque<std::coroutine_handle<>> m_push_awaiting;
	MUTEX mutex;
};

template<typename T>
concept has_post_member = requires (T t)
{
	t.post([](){});
};

template<typename T>
concept is_io_context = requires (T t)
{
	t.get_executor().execute([](){});
};


// 协程版的 mutex. 使用场景为多线程调度多协程的情况下进行数据保护。
// 虽然并不十分的建议将协程调度在多线程环境，但是总有特殊情况需要使用。
// 注：单线程调度协程并不等同于只能使用一个线程开发应用。而是使用一个线程
// 一个 io_context 的形式。这样在同一个 io_context 下运行的代码，就无需
// 锁保护。也就是说，本 mutex 的使用场景是多线程跑一个 io_context，然后
// 使用协程，也就是 M:N 调度下进行数据保护。
class mutex
{
	mutex(const mutex&) = delete;
	mutex(mutex&&) = delete;

	struct base_executor
	{
		virtual void post(std::coroutine_handle<> handle) = 0;
		virtual ~base_executor(){};
	};

	struct dummy_executor : public base_executor
	{
		virtual void post(std::coroutine_handle<> handle) override
		{
			return handle.resume();
		}
	};

	template<typename Executor>
	struct post_executor : public base_executor
	{
		Executor& executor;
		virtual void post(std::coroutine_handle<> handle) override
		{
			executor.post(handle);
		}

		post_executor(Executor& ex)
			: executor(ex)
		{}
	};

	template<typename Executor>
	struct std_executor : public base_executor
	{
		Executor executor;
		virtual void post(std::coroutine_handle<> handle) override
		{
			executor.execute(handle);
		}

		std_executor(Executor&& ex)
			: executor(ex)
		{}
	};

#ifdef __HAS_ASIO_HPP
	struct asio_io_context : public base_executor
	{
		asio::io_context& executor;
		virtual void post(std::coroutine_handle<> handle) override
		{
			asio::post(executor, handle);
		}

		asio_io_context(asio::io_context& io) : executor(io) {}
	};
#endif

#ifdef __HAS_BOOST_ASIO_HPP
	struct boost_asio_io_context : public base_executor
	{
		boost::asio::io_context& executor;
		virtual void post(std::coroutine_handle<> handle) override
		{
			boost::asio::post(executor, handle);
		}
		boost_asio_io_context(boost::asio::io_context& io) : executor(io) {}
	};
#endif

	struct any_executor
	{
		std::unique_ptr<base_executor> impl_;
		void post(std::coroutine_handle<> handle)
		{
			impl_->post(handle);
		}

		any_executor()
		{
			impl_.reset(new dummy_executor);
		}

#ifdef __HAS_ASIO_HPP
		any_executor(asio::io_context& ex)
		{
			impl_.reset(new asio_io_context{ex});
		}
#endif
#ifdef __HAS_BOOST_ASIO_HPP
		any_executor(boost::asio::io_context& ex)
		{
			impl_.reset(new boost_asio_io_context{ex});
		}
#endif
		template<typename Ex> requires (has_post_member<Ex>)
		any_executor(Ex& ex)
		{
			impl_.reset(new post_executor{ex});
		}

		template<typename Ex> requires (is_io_context<Ex>)
		any_executor(Ex& ex)
		{
			impl_.reset(new std_executor{ex.get_executor()});
		}
	};

	struct lock_waiter
	{
		any_executor& executor;
		std::coroutine_handle<> coro_handle;

		lock_waiter(any_executor& e, std::coroutine_handle<> coro_handle)
			: executor(e), coro_handle(coro_handle)
		{}

		void operator()()
		{
			executor.post(coro_handle);
		}
	};

	struct await_locker
	{
		mutex* parent;
		any_executor& executor;
        constexpr bool await_ready() noexcept { return false; }

        constexpr void await_resume() noexcept {}

        void await_suspend(std::coroutine_handle<> corohandle) noexcept
        {
			parent->m_awaiter.emplace_back(executor, corohandle);
			parent->m_thread_lock.unlock();
        }
	};

public:
	mutex()
	{}

	awaitable<void> lock(any_executor executor)
	{
		if (is_locked.test_and_set())
		{
			m_thread_lock.lock();
			if (is_locked.test_and_set())
			{
				// lock 失败。等待释放.
				co_await await_locker{this, executor};
				// 释放成功。同时已被本协程上锁.
			}
			else
			{
				// 第二次尝试后锁成功.
				m_thread_lock.unlock();
			}
		}

		// lock 成功。返回.
		co_return;
	}

	void unlock()
	{
		m_thread_lock.lock();
		// 要释放锁，先检查是否有等待者.
		if (m_awaiter.empty())
		{
			is_locked.clear();
			m_thread_lock.unlock();
			return;
		}
		// 有等待者，唤醒其中之一.
		auto top_waiter = m_awaiter.front();
		m_awaiter.pop_front();
		m_thread_lock.unlock();

		return top_waiter();
	}
private:
	std::mutex m_thread_lock;
	std::atomic_flag is_locked;
	std::deque<lock_waiter> m_awaiter;
};

namespace detail {
	struct auto_unlocker
	{
		mutex& mtx;

		~auto_unlocker()
		{
			mtx.unlock();
		}

	};
}

template<typename Executor>
auto scoped_lock(mutex& lock, Executor&& ex) -> awaitable<detail::auto_unlocker>
{
	co_await lock.lock(ex);

	co_return detail::auto_unlocker{lock};
}

}


