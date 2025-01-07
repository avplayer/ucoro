
#include "ucoro/awaitable.hpp"
#include "ucoro/inter_coro.hpp"
#include <iostream>
#include <boost/asio.hpp>

ucoro::communication::mutex mtx;
boost::asio::io_context io;

ucoro::awaitable<void> coroA()
{
	boost::asio::executor_work_guard<decltype(io.get_executor())> work{io.get_executor()};
	for (int i =0; i < 200; i++)
	{
		std::cout << "A locking...\n";
		co_await mtx.lock(io);
		std::cout << "coroA locked\n";
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		mtx.unlock();
		std::cout << "coroA unlocked\n";
	}
}

ucoro::awaitable<void> coroB()
{
	boost::asio::executor_work_guard<decltype(io.get_executor())> work{io.get_executor()};
	for (int i =0; i < 200; i++)
	{
		std::cout << "B locking...\n";
		co_await scoped_lock(mtx, io);
		std::cout << "coroB locked\n";
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		mtx.unlock();
		std::cout << "coroB unlocked\n";
	}
}

int main(int argc, char** argv)
{
	boost::asio::post(io, [](){coroA().detach(&io);});
	boost::asio::post(io, [](){coroB().detach(&io);});

	std::thread(&boost::asio::io_context::run, &io).detach();
	std::thread(&boost::asio::io_context::run, &io).detach();

	io.run();
	return 0;
}
