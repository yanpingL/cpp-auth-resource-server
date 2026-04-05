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
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include <signal.h>

#include <libgen.h>

#define MAX_FD 65535  // maximum #file descriptors
#define MAX_EVENT_NUMBER 10000  // maximum #events to be listned

//172.18.0.2

// when one part disconnect, the other part is not closed, some signals may be recieved
// we need to process that signal
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    // temp block all the bit
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}


// help fucntion to add fd into epoll, not belongs to http_conn class, so we need keywor
//  extern to tell compilor 
// also delete from epoll
extern void removefd(int epollfd, int fd);
// modify the fd
extern void modfd(int epollfd, int fd, int ev);

extern void addfd(int epollfd, int fd, bool one_shot);


// set the port number by command line
int main(int argc, char* argv[]){

    if(argc <= 1){
        std::cout << "Run based on the format below: "
                    << basename(argv[0]) << " "
                    << "port_number" << std::endl;
        return -1;
    }

    int port = atoi(argv[1]);

    // process the sigpie
    addsig(SIGPIPE, SIG_IGN);

    // creat thread pool and initiate the pool
    threadpool<http_conn> * pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    } catch(...){
        exit(-1);
    }

    // create an array to store all of the client infos, connection info
    http_conn * users = new http_conn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    // check error

    // set socket complexing before bind
    // ✅ Allow the socket to reuse a local address (IP + port) immediately.
    // handle the TIME_WAIT mechanisme
    int reuse = 1; // represent to reuse the port
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // bind
    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    // check error

    // listen
    ret = listen(listenfd, 5);

    // epoll
    // create epoll instance, and the array of events, add the fd
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // add the listen fd to the epoll instance
    addfd(epollfd, listenfd, 0);
    http_conn::m_epollfd = epollfd;

    while(1) {
        // block until events occur
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        //
        if ((num < 0) && (errno != EINTR)){
            std::cout << "epoll failure\n";
            break;
        }

        // repeatly scan the events array
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                // has client connected
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);

                if (connfd < 0) {
                    std::cout << "errno is : " << errno << std::endl;
                    continue;
                }
                
                if(http_conn::m_user_count >= MAX_FD) {
                    // current connection number is full
                    // send info to client about server busy

                    close(connfd);
                    continue;
                }

                // initialize the new client data and add to array (connection/task)
                users[connfd].init(connfd, client_address);


            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR )){
                // client disconnect with mistake or error
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN){
                // check if read request
                // main thread read the bytes from the connection socket fd
                // to the read buffer related to the users instance
                if(users[sockfd].read()){
                    // read all data in one time
                    // after that, add the task related to the connection into task queue
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                // detect write task in task queue, main thread writes from the write buffer
                // of the connection task to the socket
                if(!users[sockfd].write()) {
                    // fail to write in one time, close connection.
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



/*
This system simulate the Proactor pattern by 
- Letting the main thread handle the read & write from/to socket to/from
buffer write(), read()
- & the other threads process the data read & generate response
    process_read(), process_write()

*/