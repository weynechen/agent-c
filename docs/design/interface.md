# Core

## LLM
封装openai / claude 等等大模型厂商的连接接口,提供和LiteLLM一样的统一封装接口。
```
typedef strcut{
    // LLM base info
    const char* model; //model name
    const char* api_key; 
    const char* api_base;
    const char* instructions // system prompt

    // LLM parameters
    float temperature;                  // Default: 0.7
    int max_tokens;                     // Default: 0 (no limit)
    int topk;
    int topp;
}ac_llm_params_t;

```



## tools
使用 arc_tool来标记这个函数，随后moc程序会扫描到这个标记，解析出来：
1. description : 工具的描述
2. function name : 函数名
3. function params : 函数参数
4. function return : 函数的返回值

moc 负责将上述四个部分提供出来，并转换为json schema。同时提供函数注册。同时检查规范性
初步定义参数仅支持内置的几个类型。(string : char *)

tools_group的用途是给tools划分群组，以便给到不同的agent使用。


```
/**
 * @arc_tool
 * @tools_group:test1,test2 ...
 * @description Get the current weather for a city
 */
char* tool_get_weather(const char* city) {
    char* result = malloc(256);
    snprintf(result, 256, "The weather in %s is sunny with 25°C.", city);
    return result;
}
```


## memory
提供会话记忆和持久记忆
会话记忆就是将message记录在内存里面，当session结束后，message消失。message使用dict形式记录
```
{"system":"You are a helpful assistant",
"user":"who are you"
"assistant":"Hi , I am AI"
}
```
持久记忆存储在文件系统中，使用sqlite。（预留，暂不实现）

## agent
agent包含上述的四个基础组件
```
typedef strcut{
    const char *name;
    ac_llm_params_t llm;
    ac_tools_t * tools;
    ac_memory_t * memory;
    int max_iterations;
    uint32_t timeout_ms;
}ac_agent_params_t;
```

# 内部实现
## 线程
Core内部使用生产-消费模型来解耦云端连接和用户逻辑处理，防止用户传入的逻辑耗时过长导致连接中断。
这意味着一个agent启动，就会有两个线程。
生产线程：仅负责发送和接收llm的数据。
消费线程：根据情况调用工具，回调用户的逻辑。

线程使用posix接口来调用，对于不同的平台实现，需要做兼容。Linux/Windows/FreeRTOS。

跨平台可能需要调整的部分，需要抽离到类似config.h的文件夹中进行配置调整。比如最大缓冲区大小。

## 内存管理
由框架负责所有内存的使用和释放。当agent被创建后，内存就被创建，过程中如果需要动态增加的
则动态增加。合适的时机该释放就释放。agent销毁后，相关内存，都必须释放。

用户不需要负责内存的释放。

## 环境管理
使用load_env，各个provider分配和其名字相同的API_KEY，API_BASE（默认）。

## 日志管理
使用 `ac_log` 接口来做。当前仅简单封装printf即可。后续扩展。

## 错误管理
暂时不做重试的逻辑，使用报错，日志记录，然后结束的方式。

# 用户使用示例
## 同步调用
```c
ac_llm_t *llm = ac_llm_create(
    .model = "deepseek/deepseek-chat",
    .instructions="You are a helpful assistant"
);

ac_memory_t *memory = ac_memory_create(
    .session = "session-id"
);

ac_agent_t *agent = ac_agent_create(
    .llm = llm,
    .tools = tools["group"],
    .memory = memory,
);

agent_result *result = agent_run_sync()
ac_log("%s\n",result->response);

```


## stream调用
定义的方式和同步一样，但输出的方式采用循环读的方式。

```c
ac_stream_t* stream = arc_run(agent, "今天北京天气怎么样？");

while(stream->is_runing())
{
    ac_stream_result_t *result = ac_stream_next(stream,timeout); //block here wait queue
    ac_log(AC_LOG_INFO,result->response); 
}

```

# 多agent 联合编排
初步的想法是参考LangGraph的图的形式。还没有完全确定应该如何做，待定。
