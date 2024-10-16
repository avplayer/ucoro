#include <future>
#include <iostream>
#include <thread>
#include "ucoro/awaitable.hpp"

#include "curl/curl.h"

struct completion_job_data
{
	static void do_completion_job(void* completion_data)
	{
		(*reinterpret_cast<completion_job_data*>(completion_data))();
		delete reinterpret_cast<completion_job_data*>(completion_data);
	}

	virtual ~completion_job_data()
	{
	}

	virtual void operator() ()
	{
	}
};


template<typename continuation_t>
struct completion_job_data_for_coro : public completion_job_data
{
	continuation_t continuation;

	completion_job_data_for_coro(continuation_t&& continuation)
		: continuation(continuation)
	{
	}

	virtual void operator() () override
	{
		continuation();
	}
};

ucoro::awaitable<void> async_curl_http_post(std::string url)
{
	CURLM* curl = co_await ucoro::local_storage_t<CURLM*>{};

	co_await callback_awaitable<void>([curl, url](auto continuation)
	{
		auto http_handle = curl_easy_init();

		auto complete_op = new completion_job_data_for_coro<decltype(continuation)>(std::move(continuation));

		/* set the options (I left out a few, you get the point anyway) */
		curl_easy_setopt(http_handle, CURLOPT_URL, url.c_str());
		curl_easy_setopt(http_handle, CURLOPT_PRIVATE, complete_op);
		curl_multi_add_handle(curl, http_handle);
		curl_multi_wakeup(curl);

		// NOTE: 记住这个位置，叫 2 号位
		return;
	});

	// NOTE: 记住这个位置，叫 1 号位
	co_return;
}

ucoro::awaitable<int> coro_compute_int(std::string url)
{
	co_await async_curl_http_post(url);

	co_return url.size();
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
					completion_job_data::do_completion_job(completion_job_data);
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
