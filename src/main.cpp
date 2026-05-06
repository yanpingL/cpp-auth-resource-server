#include <iostream>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <libgen.h>

#include "thread/locker.h"
#include "thread/threadpool.h"
#include "network/http_conn.h"
#include "db/connection_pool.h"
#include "utils/logger.h"
#include "cache/redis_client.h"



#define MAX_FD 65535  // maximum #file descriptors
#define MAX_EVENT_NUMBER 10000  // maximum #events to be listned

// Installs a process signal handler.
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// epoll helpers implemented in http_conn.cpp.
extern void removefd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);
extern void addfd(int epollfd, int fd, bool one_shot);


// Set the port number by command line
// Boots the server, initializes shared services, and runs the epoll event loop.
int main(int argc, char* argv[]){

    if(argc <= 1){
        std::cout << "Run based on the format below: "
                    << basename(argv[0]) << " "
                    << "port_number" << std::endl;
        return -1;
    }

    int port = atoi(argv[1]);

    // Ignore SIGPIPE so a broken client connection does not terminate the server.
    addsig(SIGPIPE, SIG_IGN);

    threadpool<http_conn> * pool = NULL;
    try{
        pool = new threadpool<http_conn>(10, 10000);
    } catch(...){
        exit(-1);
    }

    connection_pool* connPool = connection_pool::get_instance();
    connPool->init(
        "sys-mysql",
        "webuser",
        "webpass123",
        "webdb",
        3306,
        10
    );

    RedisClient::get_instance()->connect("sys-redis", 6379);

    Logger::get_instance()->init("server.log");

    // Connection objects are indexed directly by socket fd.
    http_conn * users = new http_conn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (listenfd == -1){
        perror("socket");
        Logger::get_instance()->log(ERROR, "listen socket fails.");
        delete [] users;
        delete pool;
        return 1;
    }

    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    ret = listen(listenfd, 4096);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    addfd(epollfd, listenfd, 0);
    http_conn::m_epollfd = epollfd;

    // Proactor-style event loop: main thread handles socket IO, workers process requests.
    while(1) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((num < 0) && (errno != EINTR)){
            Logger::get_instance()->log(ERROR, "epoll failure");
            break;
        }

        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);

                if (connfd < 0) {
                    Logger::get_instance()->log(ERROR,
                        "accept error, errno=" + std::to_string(errno));
                    continue;
                }

                if(http_conn::m_user_count >= MAX_FD) {
                    close(connfd);
                    continue;
                }

                users[connfd].init(connfd, client_address);

            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR )){
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN){
                if(users[sockfd].read()){
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if(!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}
