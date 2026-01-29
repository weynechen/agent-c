# arena
为了减少内存的malloc和free。我将模型改为了session形式。这意味着，一个session内会有固定的内存开销，不进行频繁的free。
arena在框架中的使用，实际有两个作用域
1. session_open 和 session_close 中。整个session 内公用。适用于mcp等的共用资源使用。
2. create_agent 和 destroy_agent中。整个agent内部使用。包括llm / message等。


# code excute sandbox
- 为了丰富agent的工具能力，可能会引入动态语言，比如quickjs支持的ECAM2023。
- 引入沙盒层，在linux/macos上支持沙盒特性

# add more examples
增加更多的使用场景

# 可观测性
需要在llm的交互路径上面引入log文本记录（类似litellm的dump功能)
