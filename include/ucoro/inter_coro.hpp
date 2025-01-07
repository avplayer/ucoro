
// inter coro communication tools

// aka. channels

#pragma once

#include <deque>
#include "awaitable.hpp"

namespace ucoro::communication
{


template<typename T>
class channel
{
	void wake_up_one_pusher()
	{
		if (m_push_awaiting.empty())
			return;
		auto top_waiter = m_push_awaiting.front();
		// wake up .
		m_push_awaiting.pop_front();
        top_waiter.resume();
	}

	void wake_up_one_poper()
	{
		if (m_pop_awaiting.empty())
			return;
		auto top_waiter = m_pop_awaiting.front();
		// wake up .
		m_pop_awaiting.pop_front();
        top_waiter.resume();
	}

    struct wait_on_queue
    {
        std::deque<std::coroutine_handle<>> & wait_on;

        wait_on_queue(std::deque<std::coroutine_handle<>> & wait_on)
            : wait_on(wait_on)
        {}

        constexpr bool await_ready() noexcept { return false; }

        constexpr void await_resume() noexcept {}

        void await_suspend(std::coroutine_handle<> handle)
        {
            wait_on.push_back(handle);
        }
    };

public:
	awaitable<T> pop()
	{
		if (m_queue.empty())
		{
			// yield
            co_await wait_on_queue{m_pop_awaiting};
		}
		T r = m_queue.front();
		m_queue.pop_front();
		wake_up_one_pusher();
		co_return r;
	}

	awaitable<void> push(T t)
	{
		m_queue.push_back(t);
		wake_up_one_poper();
		if (m_queue.size() >= (m_max_pending))
		{
            co_await wait_on_queue{m_push_awaiting};
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
};


}


