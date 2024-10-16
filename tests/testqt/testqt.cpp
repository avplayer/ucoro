

#include <iostream>
#include "ucoro/awaitable.hpp"
#include <QtCore>
#include <QTimer>
#include <QCoreApplication>

ucoro::awaitable<int> coro_compute_int(int value)
{
	auto ret = co_await callback_awaitable<int>([value](auto handle) {
		QTimer::singleShot(0, [value, handle = std::move(handle)]() mutable {
			std::cout << value << " value\n";
			handle(value * 100);
		});
	});

	co_return (value + ret);
}

ucoro::awaitable<void> coro_compute_exec(int value)
{
	auto ret = co_await coro_compute_int(value);
	std::cout << "return: " << ret << std::endl;
	co_return;
}

ucoro::awaitable<void> coro_compute()
{
	auto x = co_await ucoro::local_storage;
	std::cout << "local storage: " << std::any_cast<std::string>(x) << std::endl;

	for (auto i = 0; i < 100; i++)
	{
		co_await coro_compute_exec(i);
	}
	qApp->quit();
}

int main(int argc, char **argv)
{
	std::string str = "hello";

	QCoreApplication app(argc, argv);

	coro_start(coro_compute(), str);

	app.exec();
	return 0;
}
