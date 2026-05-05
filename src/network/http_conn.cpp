#include "http_conn.h"
#include "db/connection_pool.h"
#include "service/user_service.h"
#include "service/resource_service.h"
#include "service/storage_service.h"
#include "dao/user_dao.h"
#include "utils/logger.h"
#include "utils/auth_utils.h"

#include <algorithm>
#include <cctype>

// Define some stat info of the HTTP response
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


// Root directory for static files inside the Docker/container workspace.
const char* doc_root = "/workspace/src/resources";

/*
- setnonblock()
- addfd()
- removedfd()
- modfd()
are all helper functions, not related to the http_conn class or http_conn object
so when using the function in other files we need the keyword "extern"
*/
// set the fd as nonblocking
void setnonblock(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}


// Add the fd to the epoll instance
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;;
    event.events = EPOLLIN | EPOLLRDHUP;
    // event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    if (one_shot){
        // only one thread process the same socket
        event.events |= EPOLLONESHOT;
    }
    // event is copied into the kernal epoll data structure
    /*
    epoll instance (epollfd)
    ├── fd1 → stored event config
    ├── fd2 → stored event config
    ├── fd3 → stored event config
    */
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // set the fd nonblok to enable the non-blocking I/O
    setnonblock(fd);
}


//  delete fd from epoll 
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


//  Modify the fd, reset the socket to make sure the EPOLLIN can be captured
//  EPOLLONESHOT & EPOLLRDHUP
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//-------------------------------------------------------------------------------
// The codes below start to define the class functions/ variables



int http_conn::m_epollfd = -1;    // the epoll instance shared by all sockets
int http_conn::m_user_count = 0;  // total number of clients


// Close connection
// Each thread/user/connection maintains its own m_sockfd
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // close a connection, #users --;
    }
}


// Initialize newly accepted connection
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;

    // set the port multiplexing
    /* 
    Computing Internet:
    The port multiplexing Key Point to prepare  
    */
    int reuse = 1; // represent to reuse the port to avoid the TIME_WAIT period
    // make the port be reused in TIME_WAIT
    // not allow multiple active servers bind to same port
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    m_user_count++;
    // add to epoll instance
    addfd(m_epollfd, m_sockfd, 1);
    // initialize the other info
    init();
} 


// Initialize other infos
void http_conn::init(){
    m_check_stat = CHECK_STATE_REQUESTLINE;  // initialize the state as parse the first line of request
    m_linger = true;

    m_method = GET; // default request method
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_index = 0;
    m_write_index = 0;
    m_file_address = 0;
    m_iv_count = 0;

    json_res = "";
    token.clear();
    apireq = 0;

    memset(m_read_buf, 0, READ_BUFFER_SIZE);
    memset(m_write_buf, 0, WRITE_BUFFER_SIZE);
    memset(m_real_file, 0, FILENAME_LEN);
    memset(&m_file_stat, 0, sizeof(m_file_stat));
    memset(m_iv, 0, sizeof(m_iv));
}


// Repeately read data from client until no more data or connection closed
// Read data from the connection socket fd and store them in the read buffer
// This function is called in MAIN THREAD by corresponding http_conn object
bool http_conn::read(){
    // if the read buffer is full, return false
    if(m_read_index >= READ_BUFFER_SIZE)
        return false;
    // byte already read
    int bytes_read = 0;
    while(1) {
        /*
        m_sockfd: read from which fd
        m_read_buf+m_read_index: from where to write the read content
        READ_BUFFER_SIZE - m_read_index: how many bytes are safe to read
        0: flag
        */
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        
        // handle the error case
        if(bytes_read == -1) {
            /*
            EAGAIN or EWOULDBLOCK
            “We have read all available data for now”, return True
            */
            if(errno == EAGAIN || errno == EWOULDBLOCK){      
                // no data
                break;
            }
            return false;
        } else if(bytes_read == 0){
            // the other part close the connection
            return false; 
        } 
        // update the m_read_index
        m_read_index += bytes_read;
    }
    return true;
}



// From now on, the functions below called in WORKER THREADS (by individual http_conn object)
// Parse line by line, check based on \r\n
/*
.......\r\n
\r: Carriage Return --> moves cursor to the beginning of the line
\n: Line Feed --> Move the cursor down to the next line
*/

// read the request line by line from the reaad buffer
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(; m_checked_index < m_read_index; ++m_checked_index){
        // parse from 0th byte
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            // didn't read the '\n', stop at '\r', incomplete
            if ((m_checked_index + 1) == m_read_index){
                return LINE_OPEN;
                // if the next character is '\n', return LINE_OK
            } else if (m_read_buf[m_checked_index + 1] == '\n'){
                // fill the '\r' and '\n' as '\0'
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;  
            }
            return LINE_BAD;
        } else if (temp == '\n'){
            if ((m_checked_index > 1) && (m_read_buf[m_checked_index-1] == '\r')){
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // parse all data being read, but still yet reach \r or \n, so incomplete line
    return LINE_OPEN;
}


// Parse HTTP reuqest line, get the method, target URL, HTTP version
/*
GET /index.html HTTP/1.1
or
GET /HTTP/1.1\r\n
or  (API GET request line)
GET /api/user?id=123 HTTP/1.1
or
GET http://192.168.110.129:10000/index.html HTTP/1.1
*/
http_conn::HTTP_CODE http_conn::parse_request_line(char * text){
    // GET /index.html HTTP/1.1
    // put the pointer to the position of the first appearance of the specified character
    // blank space or \t --> tab character
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    // fill the first ' ' as '\0' and move the pointer 1 position forward
    *m_url++ = '\0';
    char *method = text; // method: GET, POST, PUT, DELETE

    // need to update to corresponding METHODS
    if (strcasecmp(method, "POST") == 0){
        m_method = POST;
    } else if (strcasecmp(method, "PUT") == 0){
        m_method = PUT;
    } else if (strcasecmp(method, "GET") == 0){
        m_method = GET;
    } else if (strcasecmp(method, "DELETE") == 0){
        m_method = DELETE;
    } 

    // check the HTTP version:  METHOD /index.html HTTP/1.1
    // if the m_version points to null, return bad request
    // result: /index.html\0hTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version){
        return BAD_REQUEST;
    } 
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }

    /*
    check the request URL
    0. GET /api/user?id=x HTTP/1.1 
    1. POST /api/login or /api/logout
    2. GET /api/resources 
    */
    // request to the user owned resources
    if(strncmp(m_url, "/api/resources", 14) == 0){
        apireq = 1;
    }
    // request for user account
    if(strncmp(m_url, "/api/login", 10) == 0){
        apireq = 2;
    }
    if(strncmp(m_url, "/api/logout", 11) == 0){
        apireq = 3;
    }
    if(strncmp(m_url, "/api/register", 13) == 0){
        apireq = 4;
    }
    // request for file upload URL
    if(strncmp(m_url, "/api/files/upload-url", 21) == 0){
        apireq = 5;
    }
    if(strncmp(m_url, "/api/files/download-url", 23) == 0){
        apireq = 6;
    }

    /**
     * eg website addr: http://192.168.110.129:10000/index.html
    */
    // non-api request, file request
    if (strncasecmp(m_url, "http://", 7) == 0 ){
        m_url += 7;
        m_url = strchr(m_url, '/'); // index.html
    }
    if (!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    // The request line parse finished
    // the check state become the check_state_header
    m_check_stat = CHECK_STATE_HEADER;
    return NO_REQUEST;
}



// Parse request header
/*
common format:

GET Request (No Body)
GET /index.html HTTP/1.1 (request line)
(headers)
Host: localhost:8080
Connection: keep-alive
User-Agent: Mozilla/5.0
Accept: text/html
(after headers, a blank line, start of body if any)

-----------------------------------------------------
POST Request (with Body)
Host: localhost:8080
Connection: keep-alive
Content-Length: 123 
Content-Type: application/x-www-form-urlencoded 

- every header ends with \r\n
- There is a blank line \r\n after the last header
- Then optional body
*/

http_conn::HTTP_CODE http_conn::parse_headers(char * text){
    if(text[0] == '\0') {
    // Blank line encountered, header fully parsed
    // There is a blank line \r\n after the last header
    // Decide the next step:

        // request like DELETE /api/user?id=... usually has no body.
        // Execute handler once headers are fully parsed.
        // All query with type 1 need to validate the user token
        if (apireq == 1){
            if (m_method == DELETE){
                return handle_delete_resource();
            } else if(m_method == GET){
                return handle_get_resources();
            } 
        }
        // request for user account
        if (m_method == POST && (apireq == 2 || apireq == 3 || apireq == 4 || apireq == 5)){
            m_check_stat = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // request for file download URL
        if (apireq == 6 && m_method == GET){
            return handle_create_download_url();
        }

        if (m_content_length != 0 ) {
            m_check_stat = CHECK_STATE_CONTENT;
            return NO_REQUEST; // request incomplete, still need to parse the body
        }
        
        // last request option: static file request (only for exercise, not integrated into business logic)
        return GET_REQUEST;


    } else if (strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // Parse Connection header bytes  [Connection: keep-alive]
        text += 11;
        // Move the pointer text forward past all leading spaces and tabs.
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        } else if ( strcasecmp( text, "close" ) == 0 ) {
            m_linger = false;
        }

    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // Parse Content-Length [Content-Length: XXX]
        text += 15;
        text += strspn( text, " \t" );
        // convert a numeric string to a long integer
        m_content_length = atol(text);

    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // parse Host header bytes: [Host: localhost:8080]
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;

    } else if (strncasecmp(text, "Authorization:", 14) == 0) {
        text += 14;
        text += strspn(text, " \t");
    
        std::string auth = text;
        // expect: Bearer xxx
        if (auth.find("Bearer ") == 0) {
            token = auth.substr(7);
        }
    }
    // Request still incomplete
    return NO_REQUEST;
}


// Parse request body
// Didn't really parse the HTTP request body, only check if it's fully read
// Then route it to the corresponding business logic
http_conn::HTTP_CODE http_conn::parse_content(char * text){
    // Request body is fully read
    if (m_read_index >= (m_content_length + m_checked_index ) ) {   
        // ensure string end
        text[ m_content_length ] = '\0';
        // Route it to the corresponding business logic
        if (apireq == 1){
            if (m_method == POST){
                return handle_post_resource(text);
            } else if(m_method == PUT){ 
                return handle_put_resource(text);
            }
        } else if (m_method == POST && apireq == 2){
            return handle_login(text);
        } else if (m_method == POST && apireq == 3){
            return handle_logout();
        } else if (m_method == POST && apireq == 4){
            return handle_register(text);
        } else if (m_method == POST && apireq == 5){
            return handle_create_upload_url(text);
        }
        return GET_REQUEST;
    }

    // Request body is not fully read
    return BAD_REQUEST;
}


/*
Request handle function ----------------------------------------------
*/

//Implement GET/api/user (USING nlohmann/json)
//GET/api/user?id=123 HTTP/1.1
//Helper function used in handle_get_resource(), to get key and value
void http_conn::parse_query(char* query_string, std::string& key, std::string& value) {
    char* equal = strchr(query_string, '=');
    if (equal) {
        *equal = '\0';
        key = query_string;
        value = equal + 1;
    }
}

/*===================
Business handle logic
========================
*/

// GET /api/resources  
// or GET api/resources?id=x
http_conn::HTTP_CODE http_conn::handle_get_resources(){

    // Protect API
    std::optional<int> user_id = UserDAO::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }

    char* query = strchr(m_url, '?');
    std::string key, value;
    if (query) {
        *query++ = '\0';
        parse_query(query, key, value);

        // validate id
        if (key != "id" || value.empty() ||
            !std::all_of(value.begin(), value.end(), ::isdigit)) {
            json_res = "{\"error\":\"invalid parameter\"}";
            return BAD_REQUEST;
        }
    }

    // UserService layer to handle the service request
    // User_dao called inside the UserService handle the data layer
    Logger::get_instance()->log(INFO, "GET /api/resources");
    json res = key == "id"
        ? ResourceService::get_resource(user_id.value(), atoi(value.c_str()))
        : ResourceService::get_resources(user_id.value());

    json_res = res.dump();
    if (res.contains("error")){
        return BAD_REQUEST;
    } else {
        return GET_RESOURCE;
    }
}


/*
Handle Request: DELETE /api/resources?id=1
*/
http_conn::HTTP_CODE http_conn::handle_delete_resource(){

    // protect API
    std::optional<int> user_id = UserDAO::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }

    char* query = strchr(m_url, '?');
    std::string key, value;

    if (query){
        *query++ = '\0';
        parse_query(query, key, value);
    }

    // validate id
    if (key != "id" || value.empty()
        || !std::all_of(value.begin(), value.end(), ::isdigit)){

        json_res = "{\"error\":\"invalid parameter\"}";
        return BAD_REQUEST;
    }

    Logger::get_instance()->log(INFO, "DELETE /api/resource?id=" + value);
    json res = ResourceService::delete_resource(user_id.value(), atoi(value.c_str()));
    json_res = res.dump();

    if (res.contains("error")){
        return BAD_REQUEST;
    } else {
        return DELETE_RESOURCE;
    }
}


// Handle Request: POST /api/resources
http_conn::HTTP_CODE http_conn::handle_post_resource(char * text){

    // protect API
    std::optional<int> user_id = UserDAO::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }
    
    // extract body
    std::string body(text);
    Logger::get_instance()->log(INFO, "POST /api/resource body=" + body);

    json j;
    try {
        // next step: parse JSON
        j = json::parse(body);
    } catch(const json::parse_error& e){
        // malformed JSON
        json_res = "{\"error\":\"invalid JSON format\"}";
        return BAD_REQUEST;
    } catch (...){
        // other unexpected errors
        json_res = "{\"error\":\"internal error\"}";
        return INTERNAL_ERROR;
    }

    // validate id field in the request body
    if (!j.contains("id") || !j["id"].is_number_integer()){
        json_res = "{\"error\":\"invalid id\"}";
        return BAD_REQUEST;
    }
    // valid the is_file field
    if (j.contains("is_file") && !j["is_file"].is_boolean()){
        json_res = "{\"error\":\"invalid is_file\"}";
        return BAD_REQUEST;
    }

    // add the user resource back to db
    std::set<std::string> allowed = {"id", "title", "content", "is_file"};
    std::string cols;
    std::string values;
    cols += "user_id,";
    values += std::to_string(user_id.value()) + ",";

    for (auto it = j.begin(); it != j.end(); ++it){
        std::string key = it.key();

        // attribute whitelist
        if(!allowed.count(key)) continue;

        cols += key + ",";

        // number 
        if (key == "is_file"){
            values += it.value().get<bool>() ? "1," : "0,";
        } else if(it.value().is_number()){
            values += it.value().dump() + ",";
        } else if (it.value().is_string()){
            std::string val = it.value();
            values += "'" + val + "',";
        }
    }
    // pop the last semicolon
    if(!cols.empty()) cols.pop_back();
    if(!values.empty()) values.pop_back();

    std::string sql =  std::string("INSERT INTO resources ") + 
                        "(" + cols + ") values (" + values + ")";
    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    json res = ResourceService::create_resource(sql);
    json_res = res.dump();
    if (res.contains("error")){
        return BAD_REQUEST;
    } else {
        return ADD_RESOURCE;
    }
}


// Handle Method: PUT api/resources
http_conn::HTTP_CODE http_conn::handle_put_resource(char* text){
    // protect API
    std::optional<int> user_id = UserDAO::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }

    // debug
    std::string body(text);
    Logger::get_instance()->log(INFO, "PUT /api/resource body=" + body);
    json j;

    try {
        // next step: parse JSON
        j = json::parse(body);
    } catch(const json::parse_error& e){
        // malformed JSON
        json_res = "{\"error\":\"invalid JSON format\"}";
        return BAD_REQUEST;
    } catch (...){
        // other unexpected errors
        json_res = "{\"error\":\"internal error\"}";
        return INTERNAL_ERROR;
    }

    // 1. validate id
    if (!j.contains("id") || !j["id"].is_number_integer()){
        json_res = "{\"error\":\"invalid id\"}";
        return BAD_REQUEST;
    }
    int id = j["id"];

    // 2. build SET clause
    std::string set_clause;
    std::set<std::string> allowed = {"id", "title", "content"};

    for (auto it = j.begin(); it != j.end(); ++it){
        std::string key = it.key();

        if(key == "id" || !allowed.count(key)) continue;

        if (it.value().is_number()){
            set_clause += key + "=" + it.value().dump() + ",";
        } else if (it.value().is_string()){
            std::string val = it.value();
            set_clause += key + "='" + val + "',";
        }
    }

    if(set_clause.empty()){
        json_res = "{\"error\":\"no fields to update\"}";
        return BAD_REQUEST;
    }

    set_clause.pop_back(); // remove last comma

    // 4. build SQL
    std::string sql = std::string("UPDATE resources SET ") 
                        + set_clause + " WHERE user_id=" + std::to_string(user_id.value()) + 
                        " AND id=" +  std::to_string(id);
    Logger::get_instance()->log(DEBUG, "SQL: " + sql);
    
    json res = ResourceService::update_resource(sql);
    json_res = res.dump();

    if (res.contains("error")){
        return BAD_REQUEST;
    } else {
        return UPDATE_RESOURCE;
    }
}


// login function
http_conn::HTTP_CODE http_conn::handle_login(char* text) {

    std::string body(text);
    Logger::get_instance()->log(INFO, "POST /api/login body=" + body);

    json j;
    try {
        // next step: parse JSON
        j = json::parse(body);
    } catch(const json::parse_error& e){
        // malformed JSON
        json_res = "{\"error\":\"invalid JSON format\"}";
        return BAD_REQUEST;
    } catch (...){
        // other unexpected errors
        json_res = "{\"error\":\"internal error\"}";
        return INTERNAL_ERROR;
    }


    if (!j.contains("email") || !j.contains("password")) {
        json_res = "{\"error\":\"missing fields\"}";
        Logger::get_instance()->log(ERROR, json_res);
        return BAD_REQUEST;
    }

    std::string email = j["email"];
    std::string password = j["password"];

    json res = UserService::login(email, password);
    json_res = res.dump();

    if (res.contains("error")) {
        Logger::get_instance()->log(ERROR, res["error"]);
        return BAD_REQUEST;
    }

    return GET_RESOURCE;  // return JSON
}


// logout
http_conn::HTTP_CODE http_conn::handle_logout() {
    // validdate token
    if (!UserDAO::validate_token(token)) {
        json_res = "{\"error\":\"invalid token\"}";
        return FORBIDDEN_REQUEST;
    }

    if (token.empty()) {
        json_res = "{\"error\":\"no token\"}";
        Logger::get_instance()->log(ERROR, "no token");
        return BAD_REQUEST;
    }

    if (!UserDAO::delete_session(token)) {
        json_res = "{\"error\":\"logout failed\"}";
        Logger::get_instance()->log(ERROR, "logout failed.");
        return INTERNAL_ERROR;
    }

    json_res = "{\"message\":\"logout success\"}";
    return DELETE_RESOURCE;
}


// Handle POST api/files/upload-url
http_conn::HTTP_CODE http_conn::handle_create_upload_url(char* text) {
    std::optional<int> user_id = UserDAO::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }

    std::string body(text);
    Logger::get_instance()->log(INFO, "POST /api/files/upload-url body=" + body);

    json j;
    try {
        j = json::parse(body);
    } catch (const json::parse_error& e) {
        json_res = "{\"error\":\"invalid JSON format\"}";
        return BAD_REQUEST;
    } catch (...) {
        json_res = "{\"error\":\"internal error\"}";
        return INTERNAL_ERROR;
    }

    if (!j.contains("filename") || !j["filename"].is_string() ||
        !j.contains("content_type") || !j["content_type"].is_string()) {
        json_res = "{\"error\":\"missing fields\"}";
        return BAD_REQUEST;
    }

    json res = StorageService::create_upload_url(
        user_id.value(),
        j["filename"].get<std::string>(),
        j["content_type"].get<std::string>());

    json_res = res.dump();
    if (res.contains("error")) {
        return BAD_REQUEST;
    }
    return GET_RESOURCE;
}


// Handle GET api/files/download-url?resource_id=xx
http_conn::HTTP_CODE http_conn::handle_create_download_url() {
    std::optional<int> user_id = UserDAO::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }

    char* query = strchr(m_url, '?');
    std::string key, value;
    if (query) {
        *query++ = '\0';
        parse_query(query, key, value);
    }

    if (key != "resource_id" || value.empty() ||
        !std::all_of(value.begin(), value.end(), ::isdigit)) {
        json_res = "{\"error\":\"invalid parameter\"}";
        return BAD_REQUEST;
    }

    json res = ResourceService::get_file_download_url(
        user_id.value(),
        atoi(value.c_str()));

    json_res = res.dump();
    if (res.contains("error")) {
        return BAD_REQUEST;
    }
    return GET_RESOURCE;
}


// Handle POST api/register
http_conn::HTTP_CODE http_conn::handle_register(char * text){
        // extract body
        std::string body(text);
        // debug
        std::cout << "Body: " << body << std::endl;
        Logger::get_instance()->log(INFO, "POST /api/register body=" + body);
    
        json j;
        try {
            // next step: parse JSON
            j = json::parse(body);
        } catch(const json::parse_error& e){
            // malformed JSON
            json_res = "{\"error\":\"invalid JSON format\"}";
            return BAD_REQUEST;
        } catch (...){
            // other unexpected errors
            json_res = "{\"error\":\"internal error\"}";
            return INTERNAL_ERROR;
        }

    
        // validate id
        if (!j.contains("id") || !j["id"].is_number_integer()){
            json_res = "{\"error\":\"invalid id\"}";
            return BAD_REQUEST;
        }

        if (!j.contains("name") || !j["name"].is_string() ||
            !j.contains("email") || !j["email"].is_string() ||
            !j.contains("password") || !j["password"].is_string()) {
            json_res = "{\"error\":\"missing fields\"}";
            return BAD_REQUEST;
        }
        
        // add the user resource back to db
        std::set<std::string> allowed = {"id", "name", "email", "password"};
        std::string cols;
        std::string values;
    
        for (auto it = j.begin(); it != j.end(); ++it){
            std::string key = it.key();
    
            // whitelist
            if(!allowed.count(key)) continue;
    
            cols += key + ",";
            // number 
            if(it.value().is_number()){
                values += it.value().dump() + ",";
            // string
            } else if (it.value().is_string()){
                std::string val = it.value();
                //hash password only
                if (key == "password") {
                    val = sha256(val);
                }
                values += "'" + val + "',";
            }
        }
    
        if(!cols.empty()) cols.pop_back();
        if(!values.empty()) values.pop_back();
    
        std::string sql =  std::string("INSERT INTO users ") + 
                            "(" + cols + ") values (" + values + ")";
        Logger::get_instance()->log(DEBUG, "SQL: " + sql);
    
        json res = UserService::create_user(sql);
        json_res = res.dump();
        if (res.contains("error")){
            return BAD_REQUEST;
        } else {
            return ADD_RESOURCE;
        }
}


// Host state machine
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;

    // We process the content in the read buffer line by line
    // Based on the state line to 
    // In this block, the [break] only jump out of the switch, not whiles
    while(((m_check_stat == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
            || (line_status = parse_line()) == LINE_OK) {

        text = get_line();
        m_start_line = m_checked_index; // move pointer to the poition needs to be parsed
        switch(m_check_stat){
            // after parse a line, we check a state
            // at this state, we never return unless BAD_REQUEST detected
            // cause we still need to 
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    // grammaer error
                    return BAD_REQUEST;
                } 
                break;
            }

            // parse headers
            case CHECK_STATE_HEADER:   
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST){
                    return do_request();
                } else if(ret == NO_REQUEST){
                    break;
                }
                // in this logic, ret can also be other values besides GET_REQUEST
                return ret;
            }
            // parse body
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST){
                    return do_request();
                } else if (ret != NO_REQUEST){
                    return ret;
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
        // parse a line 
    }
    return NO_REQUEST;
} //parse the request of HTTP line by line



// When get a complete and correct HTTP request, we analyse the nature of the target file
// if the target file exist & readable for all clients, not directory, use mmap to map 
// the file to memory adress m_file_address, tell the caller that target file successfully get
http_conn::HTTP_CODE http_conn::do_request(){

    // "/workspace/project/resources"
    // need to update the doc_root
    strcpy(m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // get the stat info about getting the m_real_file, in the struct
    // m_file_stat, -1 fail, 0 sucess
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // check the access right
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // check whether is directory
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // open the file with read only
    int fd = open( m_real_file, O_RDONLY );
    // create memroy map
    /*
    mmap: map a file to memory
    mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
    addr: start of mapped memory
    length: length of mapping
    prot: protection flags
    flags: flags
    fd: file descriptor
    offset: offset of the file
    return: pointer to the start of mapped memory
    we can then access the file as the array in the memory eg: m_file_address[i]
    */
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}


// Reverse the mapping
// munmap to memory
void http_conn::unmap() {
    // only reverse the valid m_file_address
    if( m_file_address )
    {
        // m_file_address: start of mapped memory
        // m_file_stat.st_size: length of mapping
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}


// write the data to be sent in the write buffer
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_index >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    // arg_list = a cursor that walks through arguments
    /*
        va_list is a type used to access variable arguments in a function.
        va_start initializes the argument list by pointing to the first
        unnamed argument after the last fixed parameter. It allows functions
        like vsnprintf to iterate through the arguments safely.
    */
    va_list arg_list;
    va_start( arg_list, format );

    /*
    vsnprintf: Formats a string (like printf) and writes it into a buffer safely 
    int vsnprintf(char *str, size_t size, const char *format, va_list ap);
    - str: destination buffer
    - size: maximum number of bytes to write
    - format: format string ("%d %s")
    - ap: list of arguments (from va_start)

    return value: number of characters THAT WOULD HAVE BEEN WRITTEN
    */

    /*
    va_start(arg_list, last_param);  // initialize
    va_arg(arg_list, type);         // get next argument
    va_end(arg_list);               // cleanup
    */
    int len = vsnprintf( m_write_buf + m_write_index, WRITE_BUFFER_SIZE - 1 - m_write_index, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_index ) ) {
        return false;
    }
    m_write_index += len;
    va_end( arg_list );
    return true;
}


bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}


bool http_conn::add_headers(int content_len, const char* type) {
    return add_content_length(content_len)
        && add_content_type(type)
        && add_linger()
        && add_blank_line();
}


bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_content_type(const char* type) {
    return add_response("Content-Type: %s\r\n", type);
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}


bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}


// write the response to the write buffer
bool http_conn::process_write(HTTP_CODE ret) {
    auto add_json_response = [this](int status, const char* title) {
        if (!add_status_line(status, title)) {
            return false;
        }
        if (!add_headers(json_res.size(), "application/json")) {
            return false;
        }

        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_index;
        m_iv[1].iov_base = const_cast<char*>(json_res.c_str());
        m_iv[1].iov_len = json_res.size();
        m_iv_count = 2;
        return true;
    };

    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ), "text" );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            if (!json_res.empty()) {
                return add_json_response(400, error_400_title);
            } else {
                add_status_line(400, error_400_title);
                add_headers(strlen(error_400_form), "text");
                if (!add_content(error_400_form)) {
                    return false;
                }
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ), "text"  );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form), "text");
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size, "text");
            // points to where the m_write_buf start
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_index;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        case GET_RESOURCE:
            return add_json_response(200, ok_200_title);
        case ADD_RESOURCE:
            return add_json_response(200, ok_200_title);
        case UPDATE_RESOURCE:
            return add_json_response(200, ok_200_title);
        case DELETE_RESOURCE:
            return add_json_response(200, ok_200_title);
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_index;
    m_iv_count = 1;
    return true;
}



// Again, this function is called in the MAIN THREAD, by the corresponding http_conn object
// write HTTP response to the m_sockfd
bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;  // bytes already sent
    // int bytes_to_send = m_write_index; //bytes wait to be sent 
    int bytes_to_send = m_iv[0].iov_len + (m_iv_count == 2 ? m_iv[1].iov_len : 0); //bytes wait to be sent 

    if(bytes_to_send == 0) {
        // bytes wait to be sent is 0, response terminates
        // resset the socket as wait for read
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        // reset everything related to this socket
        init();
        return true;
    }

    while(1) {
        // wirte distributely
        // writev() is non-blocking 
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp <= -1) {
            // in this case, the socket buffer is full, writev() is non-blocking 
            // and cannot progressing anymore.
            // no need to modify the m_iv
            // If TCP no longer has write buffer, wait for the next EPOLLOUT event,
            // To ensure the connection completness.
            if( errno == EAGAIN ) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        // adjust iov
        bytes_have_send = temp;

        // case 1: header not fully sent
        if (m_iv[0].iov_len > 0) {
            if (bytes_have_send < (int)m_iv[0].iov_len){
                m_iv[0].iov_base = static_cast<char*>(m_iv[0].iov_base) + bytes_have_send;
                m_iv[0].iov_len -= bytes_have_send;
                continue;
            } else {
                // header fully sent
                bytes_have_send-= m_iv[0].iov_len;
                m_iv[0].iov_base = NULL;
                m_iv[0].iov_len = 0;
            }

        }

        // case 2: file/body part
        if (m_iv_count == 2 && bytes_have_send > 0){
            m_iv[1].iov_base = (char*)m_iv[1].iov_base + bytes_have_send;
            m_iv[1].iov_len -= bytes_have_send;
        }

        // recompute remaining bytes
        bytes_to_send = m_iv[0].iov_len +(m_iv_count == 2 ? m_iv[1].iov_len : 0);

        // bytes_to_send -= temp;
        // bytes_have_send += temp;
        if ( bytes_to_send <= 0 ) {
            // send HTTP response succeeds, decide whether close connection based on the "keep_alive" of 
            // HTTP request
            unmap();
            if(m_linger) {
                // reset the socket, wati another EPOLLIN event
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}



// Called by the worker threads in the threads poll
// Interface to process the request of HTTP
void http_conn::process() {

    // parse the request of HTTP
    // from the connection socket fd
    HTTP_CODE read_ret = process_read();
    // after finishing the process_read(), 
    
    // when the request is incomplete, worker modify the fd as EPOLLIN event 
    // and return 
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // we proceed with the process_write() based on the result of process_read().
    // create response
    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    // after generating the response, we register EPOLLOUT to the socket tracked by m_epollfd
    // Notify app when this socket is writable so I can actually send data.
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
