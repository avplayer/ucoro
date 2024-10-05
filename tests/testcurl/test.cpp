#include <future>
#include <iostream>
#include <thread>
#define DISABLE_DEBUG_CORO_STACK
#include "ucoro/awaitable.hpp"

#include "curl/curl.h"

ucoro::awaitable<void> async_curl_http_post()
{
	CURLM *curl = co_await ucoro::local_storage_t<CURLM *>{};

	co_await executor_awaitable<void>([curl](std::coroutine_handle<void> continuation)
	{
		auto http_handle = curl_easy_init();

		/* set the options (I left out a few, you get the point anyway) */
		curl_easy_setopt(http_handle, CURLOPT_URL, "https://www.google.com/");
		curl_easy_setopt(http_handle, CURLOPT_PRIVATE, continuation.address());
		curl_multi_add_handle(curl, http_handle);
	});
}

ucoro::awaitable<int> coro_compute_int(int value)
{
	co_await async_curl_http_post();

	co_return (value + 1);
}

ucoro::awaitable<void> coro_compute_exec(int value)
{
	auto ret = co_await coro_compute_int(value);
	std::cout << "return: " << ret << std::endl;
	co_return;
}

ucoro::awaitable<void> coro_compute()
{
	CURL *curl = co_await ucoro::local_storage_t<CURLM *>{};
	for (auto i = 0; i < 100; i++)
	{
		co_await coro_compute_exec(i);
	}

	curl_easy_cleanup(curl);
}

int main(int argc, char **argv)
{
	curl_global_init(CURL_GLOBAL_ALL);
	auto curl = curl_multi_init();

	if (curl)
	{
		coro_start(coro_compute(), curl);
	}

	int msgs_left = -1;
	CURLMsg *msg;

	int still_alive = 1;
	do
	{
		curl_multi_perform(curl, &still_alive);

		/* !checksrc! disable EQUALSNULL 1 */
		while ((msg = curl_multi_info_read(curl, &msgs_left)) != NULL)
		{
			if (msg->msg == CURLMSG_DONE)
			{
				char *url;
				CURL *e = msg->easy_handle;
				curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &url);
				fprintf(stderr, "R: %d - %s <%s>\n", msg->data.result, curl_easy_strerror(msg->data.result), url);
				curl_multi_remove_handle(curl, e);
				void * coroutine_handle_address = nullptr;
				curl_easy_getinfo(e, CURLINFO_PRIVATE, &coroutine_handle_address);
				if (coroutine_handle_address)
				{
					auto completion_handle = std::coroutine_handle<void>::from_address(coroutine_handle_address);
					completion_handle();
				}

				curl_easy_cleanup(e);
			}
			else
			{
				fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
			}
		}
		if (still_alive)
		{
			curl_multi_wait(curl, NULL, 0, 1000, NULL);
		}
	}
	while (still_alive);

	curl_global_cleanup();
	return 0;
}
