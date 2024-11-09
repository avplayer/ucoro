#include <future>
#include <iostream>
#include <thread>
#include "ucoro/awaitable.hpp"

#include "curl/curl.h"

template<typename ResultType>
struct handler_wrapper
{
	static void invoke_once(void* completion_data, ResultType arg)
	{
		(*reinterpret_cast<handler_wrapper*>(completion_data))(arg);
		delete reinterpret_cast<handler_wrapper*>(completion_data);
	}

	virtual ~handler_wrapper()
	{
	}

	virtual void operator() (ResultType)
	{
	}
};


template<typename T, typename continuation_t>
struct handler_wrapper_impl : public handler_wrapper<T>
{
	continuation_t continuation;

	handler_wrapper_impl(continuation_t&& continuation)
		: continuation(continuation)
	{
	}

	virtual void operator() (T t) override
	{
		continuation(std::move(t));
	}
};

ucoro::awaitable<CURLcode> async_curl_http_post(std::string url)
{
	CURLM* curl = co_await ucoro::local_storage_t<CURLM*>{};

	auto curl_code = co_await callback_awaitable<CURLcode>([curl, url](auto continuation)
	{
		auto http_handle = curl_easy_init();

		auto complete_op = new handler_wrapper_impl<CURLcode, decltype(continuation)>(std::move(continuation));

		/* set the options (I left out a few, you get the point anyway) */
		curl_easy_setopt(http_handle, CURLOPT_URL, url.c_str());
		curl_easy_setopt(http_handle, CURLOPT_PRIVATE, complete_op);
		curl_multi_add_handle(curl, http_handle);
		curl_multi_wakeup(curl);

		// NOTE: 记住这个位置，叫 2 号位
		return;
	});

	// NOTE: 记住这个位置，叫 1 号位
	co_return curl_code;
}

ucoro::awaitable<int> coro_compute_int(std::string url)
{
	co_return co_await async_curl_http_post(url);
}

ucoro::awaitable<void> coro_compute_exec(std::string url)
{
	auto ret = co_await coro_compute_int(url);
	std::cout << "return: " << ret << std::endl;
	co_return;
}

bool global_exit_loop = false;

ucoro::awaitable<void> coro_compute()
{
	static std::string urls[] = {
		"https://www.google.com",
		"https://www.github.com",
		"https://microcai.org",
	};
	for (auto i = 0; i < 3; i++)
	{
		co_await coro_compute_exec(urls[i]);
	}
	global_exit_loop = true;
}

int main(int argc, char** argv)
{
	curl_global_init(CURL_GLOBAL_ALL);
	auto curl = curl_multi_init();

	if (curl)
	{
		coro_start(coro_compute(), curl);
	}

	// 注意这里，执行到这里的时候，上诉 NOTE 里的 2号位代码执行完毕，return 了。1号位的代码，尚未执行到。

	int msgs_left = -1;
	CURLMsg* msg;

	for (;;)
	{
		int still_alive = 1;
		curl_multi_perform(curl, &still_alive);

		/* !checksrc! disable EQUALSNULL 1 */
		while ((msg = curl_multi_info_read(curl, &msgs_left)) != NULL)
		{
			if (msg->msg == CURLMSG_DONE)
			{
				CURL* e = msg->easy_handle;
				fprintf(stderr, "R: %d - %s\n", msg->data.result, curl_easy_strerror(msg->data.result));
				void* completion_job_data = nullptr;
				curl_easy_getinfo(e, CURLINFO_PRIVATE, &completion_job_data);
				if (completion_job_data)
				{
					handler_wrapper<CURLcode>::invoke_once(completion_job_data, msg->data.result);
				}

				curl_multi_remove_handle(curl, e);
				curl_easy_cleanup(e);
			}
			else
			{
				fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
			}
		}

		if (global_exit_loop)
		{
			break;
		}

		curl_multi_wait(curl, NULL, 0, 1000, NULL);
	}

	curl_multi_cleanup(curl);
	curl_global_cleanup();
	return 0;
}
