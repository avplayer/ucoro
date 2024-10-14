
# 什么是 µcoro (ucoro) [![actions workflow](https://github.com/avplayer/ucoro/actions/workflows/ci.yml/badge.svg)](https://github.com/avplayer/ucoro/actions)

µcoro 是一个最小化的c++20协程库。精简到不能再删一行代码。

## 什么是 c++20协程

要理解 c++20 协程，首先要理解 无栈协程。 要理解无栈协程，首先要理解“调用链”。

### 调用链

函数，是被“调用”的。函数 A 调用 函数 B, 函数 B 再调用 函数 C。意思就是当 C 执行完工作，它返回就会回到 B 函数里调用C的那个地方。然后继续执行。
B 函数返回的时候， 它会返回 A 函数里调用B的那个地方。

这种链试的返回控制流，就是调用链。

在线程里，调用链是存储在栈上的。在函数返回的地方，编译器生成 ret 指令。而 ret 指令的执行步骤，就是从栈指针获取返回的目标地址。然后将栈指针退行并跳转到目标地址。
现代的调试器，都能在暂停代码执行的时候，检查栈内存，从而获取调用链。

而有栈协程，就是指多个 “栈” 共享一个内核调度单元——线程。多个协程之间进行切换，实质上就是直接切换了 栈。而切换栈，是一个主动操作，而不是像线程那样由内核抢占式调度。
所以协程又被成为协作式多任务。

在无栈协程里， 调用链并不存储于栈上。当 协程函数C 完成任务要返回 B, 控制流程会在另外的地方找到它的调用者B,然后跳转到B继续执行。
如果使用调试器，那么调试器按传统的方式找调用栈，在函数C里下断点，也看不到B和A的调用帧。 函数C，函数B，函数A在调试器里看，永远都是由一种复杂的“协程调度器”代码调用的。
如果非要调试 C 返回B 的过程，会发现 C 的代码会先返回到内部的某种“协程调度器“然后紧接着进入 函数B. 而且 B 函数明明是被框架调用的，但是并不会从头执行，而是在上次挂起的地方继续。所谓上次刮起的地方，实际上这个地方就是安排了对C的调用。

最简单的实现一个 无栈协程 的方式，就是写闭包。在闭包里存储上次挂起的位置。下次执行函数的时候，就从挂起位置继续。可以使用状态机很容易就实现。

只不过，用手写状态机实现的闭包模拟的无栈协程，任务代码本身就会被为了实现状态机而添加的代码打乱。因此微软为编译器添加了自动化实现可重入状态机的功能，这个机制，就是 c++20协程啦。

so, c++20协程，是无栈协程的一种。

## 挂起和恢复

在无栈协程里，协程函数之间的调用，是“间接”进行的。 我们思考如下的一个片段

```cpp

ucoro::awaitable<int> C(int value) {
    std::cout << value << " value\n";
	co_return (value * 100 + value);
}

ucoro::awaitable<void> B(int value)
{
	auto ret = co_await C(value);
	std::cout << "return: " << ret << std::endl;
	co_return;
}

ucoro::awaitable<void> A() {
	for (auto i = 0; i < 1000000; i++) {
		co_await B(i);
	}
}

```

如果在 函数C里下个断点进行调试，则在调试器里看到的调用栈，绝对不会是 A -> B -> C，而是 某个内部代码 -> C。
进一步，进行单步调试的时候会发现， C 函数里执行 co_return , 并不会直接返回B， 而是会回到某个内部代码，然后又重新进入B。栈上的调用链条变成某个内部代码 -> B。

也就是说，在无栈协程里，如果从传统的栈上调用链看， 所有的协程函数都是“平级”的。都是被某种魔法代码平级调用。
真正的调用链，则隐藏在这魔法代码里。

为了支持这种操作模式，编译器需要对协程函数进行某种转换。也就是将 co_await C 的调用，替换成
某种类似下面的代码

```
// 初始化 C 协程函数
C_setup();
// 配置接下来跳转到 C
set_next(&C_body);
// 返回到神秘代码。
return


// 接着在神秘代码里调用

C_body();

```

而在 C协程函数的 co_return 里，要进行这种

```
set_next(B);
return;

// 接着在神秘代码里调用
B_body();
```

而 B_body() 初次调用是被 A, 接着 C ”返回“ 的时候，再次调用 B_body. 这个 B_body 就是所谓的”可重入函数“。经过编译器改造后的 B_body, 重入的时候，并不从头执行，而是一波跳转直接从上次 return 的地方继续，也就是 代码如下

```
func B_body()
{
    switch(stage)
    {
        case 0:
        // 初始化 C 协程函数
        C_setup();
        // 配置接下来跳转到 C
        set_next(&C_body);
        stage = 1;
        // 返回到神秘代码。
        return ;
        case 1:
        // 从 C 返回了。
	    std::cout << "return: " << ret << std::endl;
    }
}

```


B_body 的首次 return ， 并不是真的函数返回里，而是“挂起”， 等待 C 的结果。
当 C 任务完成，B_body 的再度执行，会记住上次的状态，从“挂起”的地方继续执行。
这种操作，就是所谓的”恢复“。

这就是 无栈协程里的 挂起/恢复 两个操作。

挂起，是为了等待另一个协程的”返回“。
恢复，是被调用的协程干完活了，就通过恢复把控制权叫回来。然后进行后续处理。

## awitable 对象

我们注意到，在 协程函数定义返回类型的时候，其类型是 awaitable<传统的返回值类型>。

这是为何呢？

### 携带状态的函数

注意到协程在挂起和恢复的时候，需要额外保留状态。包括函数内部定义的本地变量，也要保留状态。重入后，这些变量的数据可是要原样保留的。

这意味着，一个协程函数，必须得是一个**闭包**。它需要一些额外的空间存储自己的“状态”。

同时，协程函数，由必须要能在传统函数里被调用，以便把整个魔法循环开动起来。

这就要求，协程函数，必须同时仍然是一个传统的函数，而不是特立独行，完全创造新的函数调用体系。

为达成这个目的，一个 协程函数，它的返回值，从传统函数的视角来看，就得是一个“闭包”。
而协程的魔法，就在这个闭包里完成。

因此，我们看上述例子的一个全貌


```cpp

ucoro::awaitable<int> C(int value) {
    std::cout << value << " value\n";
	co_return (value * 100 + value);
}

ucoro::awaitable<void> B(int value)
{
	auto ret = co_await C(value);
	std::cout << "return: " << ret << std::endl;
	co_return;
}

ucoro::awaitable<void> A() {
	for (auto i = 0; i < 1000000; i++) {
		co_await B(i);
	}
}

int main()
{
	A().resume();
	return 0;
}

```

main 是一个传统函数， 它调用了 A() 以后，在它的视角，它获得了一个 awaitable<void> 对象。
此时 A 函数其实并没有真正运行, 也就是 A 函数处于“挂起”状态。
接着 main 在 A 返回的 awaitable 对象上调用 resume(), A 函数这才在 “挂起” 状态恢复，进入“恢复” 状态。

接下来 A B C 之间的魔法流转，就都在协程内部的代码里消化吸收了。

在 main 的视角， A 函数彻底执行完毕， 它的 "resume" 才会彻底返回。这就是所谓的非“detached”协程。
也就是“阻塞”协程。而我们一般使用协程，是为了处理“大并发”。是不能阻塞传统函数的。

而不会阻塞传统函数的协程，被称作 detached 协程。main 调用完 A的 resume, 就会立即返回，此时 ABC 的活，其实并没有立即执行。需要通过一个叫 "executor" 的执行器去“调度”。在执行器的调度下，完成 ABC的工作。

在调度器里执行的协程，就是过去程序员讲的“纤程”。(win 下的 Fiber 或者 unix 下的 ucontext)。
而未在调度器里执行的协程，就是过去程序员讲的“Generator"。

## awaitable 里面的魔法

### awaitable 的构造魔法

虽然 例子上的函数 A、B、C 其返回类是 ```awaitable<>``` 但是，函数内部并没有构造这个对象。
也就是说，编译器看到函数内部使用了 ```co_return```/```co_await``` 关键字，就自动的构造 awaitable<> 对象。
但是， ```awaitable<>``` 实际上并不是标准库类型，而是用户自定义类。因此，c++必须定义某种协议，帮助编译器将协程和用户自定义类给联系起来。

这个协议就是, 对  T func_A() 这样的函数来说，如果 func_A 内部出现了 co_await/co_return关键字，就会寻找 T 类型的 T::promise_type::get_return_object 函数。

这种寻找用户自定义类型里的特定函数以实现编译器功能的桥接的协议，自c++11始就大行其道了。

首先要明确一点，awaitable 对象，是由程序员安排生命周期的。例如例子里，main 里调用 A().resume()， 就是构造了一个临时对象。
而 awaitalbe::promise_type 对象，则是由编译器安排在堆上分配的。promise_type 是跟着协程的生命期走的。

当协程调用发生的时候， 编译器调用 awaitable::promise_type::operator new() 操作符分配一个新的 promise_type 对象，并调用他的 get_return_object 构造一个 awaitable 对象然后返回。 因此 awaitable 对象也是要求不可复制，但是可移动。确保 awaitable 对象的唯一性。

###  co_await 和 co_return 的魔法

讲完构造，接下来讲 co_await 和 co_return 分别发生了什么。

在 A 函数里， co_await B(); 指令发生的时候，编译器实际上生成的代码，是调用了 B() 创建了一个临时对象。然后调用这个临时对象的 await_suspend, 传入 A 的引用，以便 B 建立“返回地址为A” 的链。接着调用 B临时对象的 resume , 将控制权交给 B ，从而执行 B 的函数体。

在 B 函数的 co_return 指令发生的时候， 编译器实际上生成的代码，是调用 B 对象的 promise_type 里面的 final_suspend . 在 final_suspend 里， B 找到了自己的“返回地址”（其实这里应该叫 调用者，不是程序地址”），然后调用 调用者的 await_resume. 这样控制权就回到了 A 函数。由于前文说过，协程函数，就是一种可重入函数。因此 await_resume 会“自动”的跳入上一次 suspend 的地方。于是这个地方，就恰如其事的 就是 ```co_await B();``` 这个地方。

一句话总结：协程的 co_return 就是调用父级的 resume。协程的  co_await 就是调用 父级的 suspend + 子级的 resume。

那么，思考这么一个代码

```cpp

ucoro::awaitable<void> bar()
{
	debugstop2();
	co_return;
}

ucoro::awaitable<void> foo()
{
	debugstop1();
	co_await bar();
	debugstop3();
}

int main()
{
	foo().resume();
}

```

- 在 debugstop1 这个地方，调用栈看起来是  main -> foo.resume -> foo.corobody
- 在 debugstop2 这个地方，调用栈看起来是  main -> foo.resume -> foo.corobody -> bar.resume -> bar.corobody
- 在 debugstop3 这个地方，调用栈看起来是  main -> foo.resume -> foo.corobody -> bar.resume -> bar.corobody -> foo.resume -> foo.corobody
- 在 debugstop3 完毕后，会层层 ret 最终回到 main.

这看起来，在协程里，调用栈是单向增长的。直到最终执行完毕，然后突然伴随着海量的 ret 返回到传统函数的调用处。


微软在提交 coro 提案多年后，才突然意识到这个爆栈问题，因此进行了一次补丁更新。解决之道就是强迫编译器为 协程相关代码打开 **尾调用优化**。

在开启 **尾调用优化** 后，

- 在 debugstop1 这个地方，调用栈看起来是  main -> foo.corobody
- 在 debugstop2 这个地方，调用栈看起来是  main -> bar.corobody
- 在 debugstop3 这个地方，调用栈看起来是  main -> foo.corobody
- 在 debugstop3 完毕后，直接到 main.

为了能让编译器 100% 确保 尾调用优化 能实施，微软又双叒叕修改了 协程里 awaiter 对象的 await_suspend 函数定义。确保新定义下，不管你内部代码怎么写，编译器总能使用尾调用优化。

### awaiter 和 promise 角色关系

能被放到 co_await 关键字后面的对象，叫 awaiter。如本库的 ucoro::awaitable<> 类型。 awaiter 必须要有 await_suspend/await_resume/await_ready 成员。

一个能运转起来的 coro 库，必须要至少包括3个类： general awaiter / promise / final awaiter。
其中， general awaiter 就是用户可以写在 函数签名上的那个返回类型。它必须要有一个内嵌的 promsie_type 类声明。然后这个 promise 必须要有一个负责收尾的 final awaiter。

由于一个协程是一个闭包，它需要有一个上下文环境来存储中间状态。这个上下文环境就是 promsie。

对于一个
```c++
T mycoro()
{
	co_await ...
}
```
这样的一个函数， mycoro 的上下文环境其实就是 T::promise_type,
- 它创建的时候， 会调用 if (! T::promsie_type::await_ready() ) T::promsie_type::initial_suspend
- 它内部使用 co_await 的时候，调用的是 T::promsie_type::await_transform 。
- 它返回的时候，会调用 T::promsie_type::await_transform::final_suspend


对于 ```ucoro::awaitable<int> B()``` 这样的函数，其上下文环境就存储在 ```ucoro::awaitalbe<int>::promise_type``` 里。

如果在 A() 函数代码里使用 co_await B(); 这样的表达式，意味着编译器会调用 ucoro::awaitalbe<int> 这个 awaiter。

事实上 A() 函数里调用 auto b_ret_value = co_await B(); 转换后的代码如下

```cpp
auto temp_b = B();
handle_of_A.coro_state = coro_state_after_B;
handle_of_A.promise().await_transform(temp_b).await_suspend(this->corohandle).resume();
coro_state_after_B:
auto b_ret_value = temp_b.await_resume();
```

如果 A() 的 promise_type 里没有 await_transform , 则使用编译器默认的 await_transform, aka 传啥返回啥。

也就是这样

```cpp
temp_b.await_suspend(this->corohandle).resume();
```

注意代码中的 coro_state_after_B：标签，这个是用来当 B 里面 co_return 的时候， B 会在 final_awaiter 里调用 A的 resume()。 因为协程是实现为“可重入函数”，所以第二次调用 resume 就会 goto 到这个 lable。

await_transform 一般可以用来实现 co_await 一个非协程对象。比如实现 类似**线程本地存储**的**协程本地存储**。

只要 co_await ucoro::local_storage; 这样的一个写法，就可以跳入

```
 handle_of_this_coro.promise().await_transform(ucoro::local_storage)。
```

为何不直接将 ucoro::local_storage 实现为一个 awiter 呢？

因为如果 将 ucoro::local_storage 实现为一个 awiter，则 local_storage 拿不到调用处的 promise() 对象。
而使用 await_transform , 则可以在 awaitable<>::promise_type::await_transform 里直接拿到 awaitable<> 的 promise 对象，从而获取到和该协程绑定的数据。因为 promise 对象是用户自定义的。用户可以自己往里面赛东西。

除此之外，还可以在 await_transform 里实现对其他人编写的协程库的兼容。

## 和 executor 集成

### 协程替换回调地狱

前面的例子里，coro 其实并没有发挥出应有的威力。因为程序员使用 coro, 并不是为了coro而coro, 而是为了简化异步代码。

现有的异步IO, 是围绕 executor 展开的。

executor 实际上是一个闭包列队执行器 + OS 事件派发器。

executor 的内部逻辑通常是


```fakecode
for(;;){
ev = GetOSEvents();

dispatchEvents(ev);

runHandlers();
}

```

在 dispatchEvents 主要就是把 ”OS报告某个IO完成“ 这个事情，通知到当初启动IO操作的那段代码，以便代码完成后续工作。这个”通知“的形式，就是“回调”。

有时候，这些回调是“立即执行”，也就是在 dispatchEvents 里至今运行回调。也有时候，这些回调要投递执行。也就是把回调的执行放到一个列队里。稍后由 runHandlers() 统一调用。
在执行回调的过程中，用户代码可能也会“投递”一个稍后运行的代码。这些任务，就会统统延后到 runHandlers 里运行。

因此，在一个“事件驱动+回调通知”的 executor 模型里，协程就是一个改进“回调地狱”的一个利器。例如，下面这个回调地狱

```
void some_IO_task()
{
	async_connect(home_host, []()
	{
		// callback on connected
		async_send("hello", []()
		{
			// callback on data sent
			async_read(buf, []()
			{
				// callback on data read
				// do some work on data
				// and send reply
				async_send("hello", []()
				{
					// callback on data sent
					// now reading more data
					async_read(buf, []()
					{
						// ......... 这嵌套没完没了里啊？
					});
				});
			});
		});
	});
}

```

而使用协程，则优雅的多，相关代码可以改进如下：

```
void some_IO_task()
{
	co_await async_connect(home_host);

	co_await async_send("hello");

	auto read_size = co_await async_read(buf);

	// do some work on data
	// and send reply
	co_await async_send("ack");

	// read more data
	read_size = co_await async_read(buf);

	// ...... more read/reply logic here
}

```

这下就再也没有回调地狱里。而且代码看起来就非常清爽，就像是使用“同步阻塞”的 IO操作一样。

但是，executor 原生提供的 IO 操作机制，必然是基于 ```async_发起IO ( 传入完成回调)``` 这种模式的。

因此，能返回 awaitable 进而能进行 co_await 操作，必然是在此基础上进行的二次封装。

那么，如何封装呢？

### 回调转协程

首先我们知道，协程继续运行的关键，其实就在与那个 handle 上的  ```resume()```。
因此，我们只需要考虑

1. 获取到 handle
2. 在executor的完成回调里，调用 handle.resume()
3. 如有返回值，在调用 resume 之前，将返回值转储于 promise 里，以便 ```await_resume()``` 能返回数据给 co_await

一开始，我们尝试使用这样的用法

```
auto callback_coro = co_await callback_coro;

int ret_value;
async_setupIO([^](auto ret_value_)
{
	ret_value = ret_value_;
	callback_coro.resume();
});

co_await callback_coro;

```

但是，显然这种二段式用法，容易出错。如果 返回值为 void 的 async_setupIO 可以被直接 co_await， 则整个代码会更加的通俗易懂，而且不易出错。于是我们有了个新的思路

```cpp

int ret_value;
co_await callback_to_coro([](auto continuation)
{
	async_setupIO([&ret_value, continuation](auto ret_value_)
	{
		ret_value = ret_value_;
		continuation.resume();
	});
});

```

这次，async_setupIO 和等待是一气呵成了，但是，这返回值有点小小的别扭。
如果继续改进会如何？

```cpp
int ret_value = co_await callback_to_coro<int>([](auto continuation)
{
	async_setupIO([continuation](auto ret_value_)
	{
		continuation(ret_value_);
	});
});

// or

int ret_value = co_await callback_to_coro<int>(
	[](auto continuation){async_setupIO(continuation);});

```

这样，和 co_await async_setupIO 相比，增加的额外代码并不多。不会增加多少心智负担。

### 背后的魔法

首先， callback_to_coro<T> 的返回对象，是一个 awaitable_handle<T> 类型，是一个 awaiter。
awaitable_handle 有3个魔法：

- 其一为 T await_resume()，实乃返回内部存储的一个 ret_value, 有第三个魔法设置。
- 其二，为 await_suspend(), 在这里，await_suspend 获取到了 待 resume 的协程的 handle。
  并且调用用户传入的 lambda , 然后给这个 lambda 传递第三个魔法
- 第三个魔法，是一个内部的wrapper函数。这个函数会被提交给 executor 调用。
  因此这个函数，会在内部，保存返回值，然后调用 handle 的 resume();


因此，更新上面的断言，

一个能运转起来的 coro 库，必须要至少包括4个类： general awaiter / promise / final awaiter/ callbackhandle awaiter。
其中， general awaiter 就是用户可以写在 函数签名上的那个返回类型。它必须要有一个内嵌的 promsie_type 类声明。然后这个 promise 必须要有一个负责收尾的 final awaiter。

因为，第四个类，也是必要的。否则一个不能和 executor 搭配起来干活的协程将毫无意义。

