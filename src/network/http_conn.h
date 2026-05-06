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
    // Public static class variable member not belong to any instance
    // Shared by all instance
    // Accessible everywhere
    static int m_epollfd; // All  connections share a single epoll instance, set it to static
    static int m_user_count; // #users

    static const int READ_BUFFER_SIZE = 2048; // size of read buffer
    static const int WRITE_BUFFER_SIZE = 1024; //size of write buffer
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
    
    // 3 possible states of state machine --> read state of line:
    // 1.read complete line 2.error in line 3.incomplete data of the line
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD = 1, LINE_OPEN = 2 };


    http_conn() {};
    ~http_conn() {};

    void init(int sockfd, const sockaddr_in &addr); // Init newly connected sock
    void process(); // Process the request received from client
    bool read(); //Non-blocking read from connection socket to the read buffer
    bool write(); //Non blocking write from write buffer to connection socket
    void close_conn(); // Close connection


private:
    void init(); // Initialize other info

    HTTP_CODE process_read();  //Parse the request of HTTP stoed in read buffer

    // ==================================================================================
    // The following functions are called by the process_read to parse the HTTP request
    LINE_STATUS parse_line(); // Parse one line, check based on \r\n
    char * get_line() { return m_read_buf + m_start_line;} // Move the pointer to the next parseing position
    HTTP_CODE parse_request_line(char * text);  // Process request line 
    HTTP_CODE parse_headers(char * text);   // Parse request header
    HTTP_CODE parse_content(char * text);  // Parse request body
    //===================================================================================

    // ======================== RESTful API business logic handlers ================================
    HTTP_CODE handle_register(char* text);  // Register new account
    HTTP_CODE handle_login(char* text);  // User login
    HTTP_CODE handle_logout(); // User logout
    
    void parse_query(char* query_string, std::string& key, std::string& value);  // Helper function to parse key and value 
    HTTP_CODE handle_get_resources();    // Return list of resources or specific resource of user
    HTTP_CODE handle_delete_resource();  // Handle Delete resoruce request
    HTTP_CODE handle_post_resource(char * text);  // Handle create new resource 
    HTTP_CODE handle_put_resource(char * text);   // Handle modify the resource
    
    HTTP_CODE handle_create_upload_url(char* text); // Create upload url
    HTTP_CODE handle_create_download_url();  // Created download url
    // ===========================================================================================

    // Static file serve
    HTTP_CODE do_request();


    bool process_write(HTTP_CODE ret); // Create the HTTP response

    // ============ Helper functions to generate response used by [process_write] ====================
    // GThe following functions are called by the process_write() to create the HTTP response
    void unmap(); // Release the memory used by mmap
    bool add_response( const char* format, ... ); 

    bool add_status_line( int status, const char* title ); // Add response status [eg.400 Bad Request]

    bool add_content_type(const char* type); // Add [content-type] in header
    bool add_content_length( int content_length ); // Add [Content-Length] in header
    bool add_linger(); // Add [Connection: keep-alive] in header
    bool add_blank_line(); // Add blank_line after the header
    bool add_headers( int content_length, const char* content_type ); // Generate response hearder by calling the above functions
    
    bool add_content( const char* content ); // Add response content
    // ==============================================================================================



private:
    int m_sockfd; // Socket connected with this HTTP connection
    sockaddr_in m_address; //Connection's network address information(IP addr + port)

    char m_read_buf[READ_BUFFER_SIZE]; // Read buffer
    int m_read_index; // Mark the next position to read

    // ================= Variables for parse operation ==========================
    int m_checked_index;  // The position of the character currently being processed in the read buffer
    int m_start_line;  // The beginning position of the line currently being parsed

    CHECK_STATE m_check_stat;  // Current parsing state of host state machine
    METHOD m_method;   // Request method


    char m_real_file[ FILENAME_LEN ];       // The full path of the target file, doc_root + m_url, doc_root is the root directory of the website
    char* m_url;                            // Target file name requested by the client
    char* m_version;                        // HTTP version, only support 1.1
    char* m_host;                           // Host name
    int m_content_length;                   // Total length of the HTTP request
    bool m_linger;                          // Whether keep the connection
    std::string token;                      // Token 

    std::string json_res;                   // String to store the JSON format result
    int apireq;                            // Check the type of API request
    

    char m_write_buf[ WRITE_BUFFER_SIZE ];      //  Write buffer
    int m_write_index;                          //  Where the next write should start
    char* m_file_address;                       //  Start position in the memory of the mmap file 
    struct stat m_file_stat;                    //  State of the target file, with which to check if the file exist, is dictionary or not, readable or note, file size
    struct iovec m_iv[2];                       //  Use writev to write, m_iv_count represents the number of memory blocks
    int m_iv_count;    
};


#endif
