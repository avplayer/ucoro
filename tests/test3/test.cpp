#include <iostream>
#include "ucoro/awaitable.hpp"


int main(int argc, char **argv)
{
	using CallbackAwaiterType0 = CallbackAwaiter<void, decltype([](auto h) {}) >;
	using ExecutorAwaiterType0 = ExecutorAwaiter<void, decltype([](auto h) {}) > ;

	using CallbackAwaiterType1 = CallbackAwaiter<int, decltype([](auto h) {}) > ;
	using ExecutorAwaiterType1 = ExecutorAwaiter<int, decltype([](auto h) {}) > ;

	static_assert(ucoro::detail::is_awaiter_v < CallbackAwaiterType0 >, "not a coroutine");
	static_assert(ucoro::detail::is_awaiter_v < ExecutorAwaiterType0 >, "not a coroutine");

	static_assert(ucoro::detail::is_awaiter_v < CallbackAwaiterType1 >, "not a coroutine");
	static_assert(ucoro::detail::is_awaiter_v < ExecutorAwaiterType1 >, "not a coroutine");

	static_assert(ucoro::detail::is_awaiter_v < ucoro::awaitable<void> >, "not a coroutine");
	static_assert(ucoro::detail::is_awaiter_v < ucoro::awaitable<int> >, "not a coroutine");

	static_assert(!ucoro::detail::is_awaiter_v < int >, "not a coroutine");

	return 0;
}
