
# 什么是 µcoro (ucoro) [![actions workflow](https://github.com/avplayer/ucoro/actions/workflows/ci.yml/badge.svg)](https://github.com/avplayer/ucoro/actions)

µcoro 是一个最小化的c++20协程库。精简到不能再删一行代码。

深入理解 µcoro 可以查阅 wiki [深入理解µcoro](https://github.com/avplayer/ucoro/wiki/%E6%B7%B1%E5%85%A5%E7%90%86%E8%A7%A3-%C2%B5coro)


# 快速开始

使用 µcoro 的方式非常简单，只需要定义一个返回类型为 awaitable<T> 的函数。其中 T 的类型为函数真正的返回值类型，如 void, int, double。而函数的返回值使用 co_return 代替 return 即可。

```cpp
int compute_squre(int value)
{
	return (value * value);
}
```

修改为协程版如下：

```cpp
ucoro::awaitable<int> coro_compute_squre(int value)
{
	co_return (value * value);
}
```

然后，使用它的地方，则从

```cpp
void do_calc()
{
    for (int i=1; i<100; i++)
    {
        auto result = compute_squre(i);
        std::cout << i << "*" << i << "=" << result << std::endl;
    }
    return;
}
```

变成

```cpp
ucoro::awaitable<void> do_calc()
{
    for (int i=1; i<100; i++)
    {
        auto result = co_await coro_compute_squre(i);
        std::cout << i << "*" << i << "=" << result << std::endl;
    }
    co_return;
}
```

>注意，返回值为 ```awaitable<void>``` 的情况下， co_return 可以省去。前提是函数内部必须包含 co_await 指令。也就是说，函数内部必须至少有一个 co_return/co_await。

而调用 do_calc() 的代码，则从

```cpp
int main()
{
    do_calc();
    return 0;
}
```

改成

```cpp
int main()
{
    start_coro(do_calc());
    return 0;
}
```

# 进阶：修改异步回调为 co_await

刚刚展示的用法，实际上协程只是作为一种新的执行序列转移方法。本身其实并没有将任务转变为异步任务。

实际上，使用协程最大的目的，是为了以编写同步模式IO的思维去实际上让代码运行于异步IO模式。

有一些IO库，本身就提供协程支持，例如 "boost::asio"，但是依然有相当多的库，只能使用传统的 “异步回调” 的方式编写异步代码。

因此，我们要做的，其实就是将异步的库，使用协程进行封装。

就拿 Qt 作为例子。

例如，在 Qt 里，进行一次“异步”的 sleep.

```cpp
void some_work_with_sleep()
{
    // do some work

    // sleep

    QTimer::singleShot(1000, []()
    {
        // do other work
    });
}
```

如果不进行异步 sleep，则这段代码就必须开启新的线程，然后使用 ```std::thread::sleep_for```，
若不开启新的线程，直接使用 sleep_for 会导致整个Qt的事件循环被卡住 —— 这通常意味着 GUI 停止响应用户鼠标和键盘事件。

而使用了异步 sleep ， 就需要将 sleep 后的工作放到回调里。如果 睡眠点还不止一个，就会出现回调里再嵌套回调的回调地狱。

如果使用协程进行异步，则代码会轻轻松松，和 **开线程+使用同步sleep** 的代码相差无几：

```cpp
awaitable<void> some_work_with_sleep()
{
    // do some work

    // sleep asynchronously
    co_await async_qtimer_shot(1000);

    // do other work
}
```

async_qtimer_shot 只要使用 executor_awaitable 对 QTimer::singleShot 进行一次小小的封装就可以了。

```cpp
awaitable<void> async_qtimer_shot(int ms)
{
    co_await executor_awaitable<void>([ms](auto continuation) {
        QTimer::singleShot(ms, [continuation = std::move(continuation)]() mutable
        {
			continuation();
        });
    });
    co_return;
}

```

可见这种封装是非常容易的。

如果需要 处理有返回值的回调，则只需要把 executor_awaitable<void> 里的 void 替换为相应的类型即可。

例如为 QTcpSocket 封装为异步读取：

```cpp
awaitable<int> async_read_qsocket(QTcpSocket* s, void* buffer, int buffer_size)
{
    QObject* context = new QObject{s};

    auto read_size = co_await executor_awaitable<int>([&context, s, buffer, buffers_size](auto continuation)
    {
        QObject::connect(s, &QIODevice::readyRead, context,
            [s, &context, buffer, buffers_size]() mutable
            {
                // context 没了，信号和槽自动解绑。
                // 其实用这个是为了实现一次性 信号连接
                // 防止这段 lambda 被反复调用.
                delete context; context = nullptr;

                // 读取内容
                auto read_size = s->read(buffer, buffer_size);

                // 这里调用 continuation, read_size 就会变成 co_await 的返回值
                continuation(read_size);
            }
        );
    });

    if (context)
        delete context;

    co_return read_size;
}
```

这样封装好后，使用的地方就可以使用下面的协程代码像同步模式一样用 QTcpSocket 进行异步读取了。
```cpp
awaitable<void> some_work_on_qsocket()
{
    char buff[4000];
    auto read_size = co_await async_read_qsocket(qsocket, buff, sizeof buff);

    if (read_size > 0)
    {

    }
}

```

# 即时交流群

可以加入 Telegram 群 [µcoro讨论群](https://t.me/ucorogroup/5) 讨论 µcoro
