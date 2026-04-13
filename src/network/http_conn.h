#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/epoll.h>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <cstring>
#include <string.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <mysql/mysql.h>
#include <set>
#include "thread/locker.h"

using json = nlohmann::ordered_json;

class http_conn {
public:
    // public static class variable member not belong to any instance
    // shared by all instance
    // accessible everywhere
    static int m_epollfd; // all connections share a single epoll instance, set it to static
    static int m_user_count; // #users

    static const int READ_BUFFER_SIZE = 2048; // size of read buffer
    static const int WRITE_BUFFER_SIZE = 1024; //size of write buffer
    static const int FILENAME_LEN = 200;   

    // HTTP request method, only GET is supported in this version
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        The possible states of the state machine when parsing the client request
        CHECK_STATE_REQUESTLINE:analysing the request line
        CHECK_STATE_HEADER: analysing the head data
        CHECK_STATE_CONTENT: parsing the request body
    */
    
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        Possible result of process_read
        NO_REQUEST          :   Request not complete, continue reading request
        GET_REQUEST         :   get complete client request
        BAD_REQUEST         :   client request has grammar error
        NO_RESOURCE         :   No resource in server
        FORBIDDEN_REQUEST   :   no access to the resource
        FILE_REQUEST        :   Request File, get file successfully
        INTERNAL_ERROR      :   Server intrenal error
        CLOSED_CONNECTION   :   client closed
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, 
                    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION,
                    GET_RESOURCE, ADD_RESOURCE, UPDATE_RESOURCE, DELETE_RESOURCE};
    
    // 3 possible states of state machine --> read state of line:
    // 1.read complete line 2.error in line 3.incomplete data of the line
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };


    http_conn() {};
    ~http_conn() {};

    void init(int sockfd, const sockaddr_in &addr); // init newly connected sock
    void process(); //to process the request from client
    void close_conn(); // close connection
    bool read(); //non-blocking read
    bool write(); //non blocking write

private:
    void init(); // initialize other info

    HTTP_CODE process_read();  //parse the request of HTTP
    // the following functions are called by the process_read to parse the HTTP request
    HTTP_CODE parse_request_line(char * text);  // parse request first line 
    HTTP_CODE parse_headers(char * text);   // parse request head
    HTTP_CODE parse_content(char * text);  // parse request body 
    HTTP_CODE do_request();
    char * get_line() { return m_read_buf + m_start_line;}
    LINE_STATUS parse_line(); // parse one line, check based on \r\n

    // ===== RESTful API =====
    HTTP_CODE handle_get_resources();
    void parse_query(char* query_string, std::string& key, std::string& value);
    // helper fuction for DELETE method
    HTTP_CODE handle_delete_resource();
    // helper function for POST method
    HTTP_CODE handle_post_resource(char * text);
    // helper function for PUT method
    HTTP_CODE handle_put_resource(char * text);
    
    HTTP_CODE handle_login(char* text);
    HTTP_CODE handle_logout();
    HTTP_CODE handle_register(char* text);



    bool process_write(HTTP_CODE ret); // create the HTTP response
    // the following functions are called by the process_write() to create the HTTP response
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type(const char* type);
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length, const char* content_type );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    void distribute_data(){
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_index;
    }



private:
    int m_sockfd; // socket connected with this HTTP
    sockaddr_in m_address; //client's network address information(IP addr + port)

    char m_read_buf[READ_BUFFER_SIZE]; // read buffer
    int m_read_index; //mark the next position of the last read byte of client data in the buffer

    // variabel for parse operation
    int m_checked_index;  // the position of the character currently being processed in the read buffer
    int m_start_line;  // the beginning position of the line currently being parsed

    CHECK_STATE m_check_stat;  //current state of host state machine
    METHOD m_method;   //request method


    char m_real_file[ FILENAME_LEN ];       // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char* m_url;                            // 客户请求的目标文件的文件名
    char* m_version;                        // HTTP协议版本号，我们仅支持HTTP1.1
    char* m_host;                           // 主机名
    int m_content_length;                   // HTTP请求的消息总长度
    bool m_linger;                          // HTTP请求是否要求保持连接
    std::string token;                      // token 

    std::string json_res;                   // string to store the JSON format result
    int apireq;                            // check if it's api request
    // HTTP_CODE api_ret;                       // private variable to record the api request HTTP_CODE
    

    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_write_index;                        // 写缓冲区中待发送的字节数
    char* m_file_address;                   // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;                // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];                   // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;    
};


#endif