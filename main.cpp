#include "Epoll.h"
#include "InetAddress.h"
#include "Socket.h"
#include "Channel.h"
#include "util.h"
#include "Server.h"
#include "Acceptor.h"
#include "Connection.h"

#include <iostream>
#include <unistd.h>     // close(), read(), write()
#include <fcntl.h>      // fcntl(), O_NONBLOCK
#include <functional>   // std::function
#include <errno.h>
#include <vector>
const int MAX_EVENTS = 1000;

int main(){
    Epoll* ep = new Epoll();
    Server* server = new Server(ep);
    std::cout << "服务器已启动..." << std::endl;
    while(true){
        std::vector<Channel*> activeChannels = ep->poll();
        
        for(auto channel : activeChannels){
            channel->handleEvent();
        }
    }
    delete server;
    delete ep;
    return 0;
}

