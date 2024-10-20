#include <iostream>
#include "ucoro/awaitable.hpp"


int main(int argc, char **argv)
{
	using CallbackAwaiterType0 = ucoro::CallbackAwaiter<void, decltype([](auto h) {}) >;
	using CallbackAwaiterType1 = ucoro::CallbackAwaiter<int, decltype([](auto h) {}) > ;

	static_assert(ucoro::concepts::local_storage_type<ucoro::local_storage_t<void>>, "not a local_storage_t");

	using local_storage_template_parameter = ucoro::traits::template_parameter_of<decltype(ucoro::local_storage), ucoro::local_storage_t>;

	static_assert(std::is_void_v<local_storage_template_parameter>, "local_storage is not local_storage_t<void>");

	static_assert(ucoro::concepts::is_awaiter_v<CallbackAwaiterType0>, "not a coroutine");
	static_assert(ucoro::concepts::is_awaiter_v<CallbackAwaiterType1>, "not a coroutine");

	static_assert(ucoro::concepts::is_awaiter_v<ucoro::awaitable<void>>, "not a coroutine");
	static_assert(ucoro::concepts::is_awaiter_v<ucoro::awaitable<int>>, "not a coroutine");

	static_assert(!ucoro::concepts::is_awaiter_v<int>, "not a coroutine");

	return 0;
}
