#include <iostream>
#define DISABLE_DEBUG_CORO_STACK
#include "ucoro/awaitable.hpp"


ucoro::awaitable<void> coro_compute_exec(int value)
{
	if (value == 0)
		co_return;

	co_await coro_compute_exec(--value);

	co_return;
}

int main(int argc, char **argv)
{
	coro_start(coro_compute_exec(1000));
	return 0;
}
