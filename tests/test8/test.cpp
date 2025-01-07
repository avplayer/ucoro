
#include "ucoro/awaitable.hpp"
#include "ucoro/inter_coro.hpp"
#include <iostream>

ucoro::awaitable<void> coroA(ucoro::communication::channel<int>& channel)
{
	for (int i =0; i < 200; i++)
	{
		std::cout << "pushing " << i << " from coroA\n";
		co_await channel.push(i);
		std::cout << "pushed";
	}
}

ucoro::awaitable<void> coroB(ucoro::communication::channel<int>& channel)
{
	for (int i =0; i < 200; i++)
	{
		std::cout << "poping...\n";
		auto result = co_await channel.pop();
		std::cout << "get " << result << " from coroA\n";
	}
}

ucoro::communication::channel<int> chan1(1);

int main(int argc, char** argv)
{
	coroA(chan1).detach();
	coroB(chan1).detach();

	return 0;
}
