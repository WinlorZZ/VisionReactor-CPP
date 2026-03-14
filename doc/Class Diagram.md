    %% 关系定义
    Server *-- ThreadPool : 组合 (Server 拥有 ThreadPool)
    Server o-- Connection : 聚合 (Server 管理 Connection)



```mermaid
classDiagram
    %% ==========================================
    %% 核心排版秘诀 1：把关系连线写在最前面，且从最顶层往下写！
    %% 引擎会根据这里的顺序，自顶向下（Top-Down）构建树形结构
    %% ==========================================
    
    %% Server 
    Server *-- Acceptor
    Server *-- ThreadPool
    Server *-- AsyncAIEngine 
    Server *-- Connection 
    Server o-- EventLoop
    
    %% 业务层依赖
    Acceptor *-- Socket
    Acceptor *-- Channel
    Acceptor o-- EventLoop
    
    Connection *-- Socket 
    Connection *-- Channel
    Connection o-- EventLoop
    Connection ..> AsyncAIEngine : 使用但不持有

    AsyncAIEngine o-- ThreadPool : 回调投递
    
    %% 底层核心依赖
    EventLoop *-- Epoll 
    Channel o-- EventLoop
    Epoll o-- Channel : epoll_event

    %% ==========================================
    %% 使用 namespace 强制物理分区隔离
    %% ==========================================

    namespace 01_Controller_Layer {
        class Server {
            -EventLoop *loop
            -Acceptor *acceptor
            -ThreadPool *threadPool
            -unique_ptr~AsyncAIEngine~ aiengine
            -map~int, shared_ptr~Connection~~ conns
            +Server(EventLoop *loop)
            +handleNewConnection(Socket *sock) void
            +handleOnMessage(shared_ptr~Connection~ conn) void
        }
    }

    namespace 02_Business_and_Gateway {
        class Acceptor {
            -Socket *sock
            -Channel *acceptChannel
            +acceptConnection() void
        }

        class Connection {
            -Socket *sock
            -Channel *channel
            -Buffer *inputBuffer
            +business(AsyncAIEngine* ai_engine) void
            +send(string msg) void
        }
        
        class AsyncAIEngine {
            -CompletionQueue cq_
            -thread cq_thread
            -ThreadPool* threadPool_
            +AnalyzeFrameAsync(uint64_t id, string&& data) void
        }
    }

    namespace 03_Resource_Layer {
        class Socket {
            -int fd
            +bind(InetAddress*) void
            +listen() void
            +accept(InetAddress*) int
        }

        class ThreadPool {
            -vector~thread~ workers
            -queue~function~ tasks
            +add(function task) future
        }
    }

    namespace 04_Core_Reactor {
        class EventLoop {
            -Epoll *ep
            +loop() void
            +updateChannel(Channel* ch) void
        }

        class Epoll {
            -int epfd
            +poll() vector~Channel*~
            +updateChannel(Channel* ch) void
        }

        class Channel {
            -int fd
            -uint32_t events
            +enableReading() void
            +handleEvent() void
        }
    }
```



