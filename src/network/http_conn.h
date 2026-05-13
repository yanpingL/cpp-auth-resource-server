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
#include <set>
#include "thread/locker.h"

using json = nlohmann::ordered_json;

class http_conn {
public:
    // Shared by all active HTTP connections.
    static int m_epollfd;
    static int m_user_count;

    static const int READ_BUFFER_SIZE = 65536;
    static const int WRITE_BUFFER_SIZE = 1024;
    static const int FILENAME_LEN = 200;

    enum METHOD {GET = 0, POST = 1, HEAD = 2, PUT = 3, DELETE = 4};
    
    /*
        The possible states of the state machine when parsing the client request
        CHECK_STATE_REQUESTLINE:analysing the request line
        CHECK_STATE_HEADER: analysing the head data
        CHECK_STATE_CONTENT: parsing the request body
    */
    
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER = 1, CHECK_STATE_CONTENT = 2 };
    
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
    
    // Line parser result while scanning CRLF-delimited HTTP input.
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD = 1, LINE_OPEN = 2 };


    http_conn() {};
    ~http_conn() {};

    void init(int sockfd, const sockaddr_in &addr);
    void process();
    bool read();
    bool write();
    void close_conn();


private:
    void init();

    HTTP_CODE process_read();

    // Request parsing helpers used by process_read().
    LINE_STATUS parse_line();
    char * get_line() { return m_read_buf + m_start_line;} // Current line start in read buffer
    HTTP_CODE parse_request_line(char * text);
    HTTP_CODE parse_headers(char * text);
    HTTP_CODE parse_content(char * text);

    // RESTful API business logic handlers.
    HTTP_CODE handle_register(char* text);
    HTTP_CODE handle_login(char* text);
    HTTP_CODE handle_logout();

    void parse_query(char* query_string, std::string& key, std::string& value);
    HTTP_CODE handle_get_resources();
    HTTP_CODE handle_delete_resource();
    HTTP_CODE handle_post_resource(char * text);
    HTTP_CODE handle_put_resource(char * text);

    HTTP_CODE handle_create_upload_url(char* text);
    HTTP_CODE handle_create_download_url();

    // Static file serve
    HTTP_CODE do_request();


    bool process_write(HTTP_CODE ret);

    // Response construction helpers used by process_write().
    void unmap();
    bool add_response( const char* format, ... );

    bool add_status_line( int status, const char* title );

    bool add_content_type(const char* type);
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    bool add_headers( int content_length, const char* content_type );

    bool add_content( const char* content );

private:
    int m_sockfd;               // Client socket for this connection.
    sockaddr_in m_address;      // Client network address.

    char m_read_buf[READ_BUFFER_SIZE];  // Raw request bytes read from the socket.
    int m_read_index;                   // Next read position in m_read_buf.

    // Parser cursor state inside m_read_buf.
    int m_checked_index;        // Current scan position.
    int m_start_line;           // Start offset of the current line.

    CHECK_STATE m_check_stat;   // Current HTTP parser state.
    METHOD m_method;            // Parsed HTTP method.


    char m_real_file[ FILENAME_LEN ];   // Resolved static file path.
    char* m_url;                        // Parsed request target.
    char* m_version;                    // Parsed HTTP version.
    char* m_host;                       // Parsed Host header.
    int m_content_length;               // Request body length from headers.
    bool m_linger;                      // Whether to keep the connection alive.
    std::string token;                  // Bearer token from Authorization header.

    std::string json_res;       // JSON response body for API requests.
    int apireq;                 // Internal route marker for API handlers.


    char m_write_buf[ WRITE_BUFFER_SIZE ];  // Response header / small-body buffer.
    int m_write_index;                      // Next write position in m_write_buf.
    char* m_file_address;                   // mmap address for static file responses.
    struct stat m_file_stat;                // Metadata for the requested static file.
    struct iovec m_iv[2];                   // writev buffers: headers plus body/file.
    int m_iv_count;                         // Number of active iovec entries.
};


#endif
