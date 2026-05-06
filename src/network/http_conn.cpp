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

// epoll helper functions shared with main.cpp.
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
    if (one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblock(fd);
}


//  delete fd from epoll
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


// Reset the socket event and keep EPOLLONESHOT protection.
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_epollfd = -1;    // the epoll instance shared by all sockets
int http_conn::m_user_count = 0;  // total number of clients


// Initialize newly accepted connection
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;

    int reuse = 1; // represent to reuse the port to avoid the TIME_WAIT period
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    m_user_count++;
    addfd(m_epollfd, m_sockfd, 1);
    init();
}

// Worker-thread entry point for a completed read event.
void http_conn::process() {
    HTTP_CODE read_ret = process_read();

    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// Main thread reads all currently available bytes into the connection buffer.
bool http_conn::read(){
    if(m_read_index >= READ_BUFFER_SIZE)
        return false;

    int bytes_read = 0;
    while(1) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);

        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }
            return false;
        } else if(bytes_read == 0){
            return false;
        }
        m_read_index += bytes_read;
    }
    return true;
}

// Main thread writes prepared response buffers to the client socket.
bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;  // bytes already sent
    int bytes_to_send = m_iv[0].iov_len + (m_iv_count == 2 ? m_iv[1].iov_len : 0); //bytes wait to be sent

    if(bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp <= -1) {
            // Socket send buffer is full; wait for the next writable event.
            if( errno == EAGAIN ) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send = temp;

        if (m_iv[0].iov_len > 0) {
            if (bytes_have_send < (int)m_iv[0].iov_len){
                m_iv[0].iov_base = static_cast<char*>(m_iv[0].iov_base) + bytes_have_send;
                m_iv[0].iov_len -= bytes_have_send;
                continue;
            } else {
                bytes_have_send-= m_iv[0].iov_len;
                m_iv[0].iov_base = NULL;
                m_iv[0].iov_len = 0;
            }

        }

        if (m_iv_count == 2 && bytes_have_send > 0){
            m_iv[1].iov_base = (char*)m_iv[1].iov_base + bytes_have_send;
            m_iv[1].iov_len -= bytes_have_send;
        }

        bytes_to_send = m_iv[0].iov_len +(m_iv_count == 2 ? m_iv[1].iov_len : 0);

        if ( bytes_to_send <= 0 ) {
            unmap();
            if(m_linger) {
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
// Close connection
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


// Initialize other infos
void http_conn::init(){
    m_check_stat = CHECK_STATE_REQUESTLINE;
    m_linger = true;

    m_method = GET;
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

// Host state machine
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;

    // Parse request line, headers, and optional body as data becomes complete.
    while(((m_check_stat == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
            || (line_status = parse_line()) == LINE_OK) {

        text = get_line();
        m_start_line = m_checked_index;
        switch(m_check_stat){
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }

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
                return ret;
            }
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
    }
    return NO_REQUEST;
}

// Parse one CRLF-terminated line from the read buffer.
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(; m_checked_index < m_read_index; ++m_checked_index){
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            if ((m_checked_index + 1) == m_read_index){
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_index + 1] == '\n'){
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
    return LINE_OPEN;
}


// Parse HTTP reuqest line, get the method, target URL, HTTP version
http_conn::HTTP_CODE http_conn::parse_request_line(char * text){
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }

    *m_url++ = '\0';
    char *method = text;

    if (strcasecmp(method, "POST") == 0){
        m_method = POST;
    } else if (strcasecmp(method, "PUT") == 0){
        m_method = PUT;
    } else if (strcasecmp(method, "GET") == 0){
        m_method = GET;
    } else if (strcasecmp(method, "DELETE") == 0){
        m_method = DELETE;
    }

    m_version = strpbrk(m_url, " \t");
    if (!m_version){
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }

    // Mark API request type for later routing after headers/body are complete.
    if(strncmp(m_url, "/api/resources", 14) == 0){
        apireq = 1;
    }
    if(strncmp(m_url, "/api/login", 10) == 0){
        apireq = 2;
    }
    if(strncmp(m_url, "/api/logout", 11) == 0){
        apireq = 3;
    }
    if(strncmp(m_url, "/api/register", 13) == 0){
        apireq = 4;
    }
    if(strncmp(m_url, "/api/files/upload-url", 21) == 0){
        apireq = 5;
    }
    if(strncmp(m_url, "/api/files/download-url", 23) == 0){
        apireq = 6;
    }

    // Static file requests may arrive with an absolute URL.
    if (strncasecmp(m_url, "http://", 7) == 0 ){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    m_check_stat = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
// Parse request header
http_conn::HTTP_CODE http_conn::parse_headers(char * text){
    if(text[0] == '\0') {
        // Header block is complete. Body-less API routes can be handled now.
        if (apireq == 1){
            if (m_method == DELETE){
                return handle_delete_resource();
            } else if(m_method == GET){
                return handle_get_resources();
            }
        }
        if (m_method == POST && (apireq == 2 || apireq == 3 || apireq == 4 || apireq == 5)){
            m_check_stat = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        if (apireq == 6 && m_method == GET){
            return handle_create_download_url();
        }

        if (m_content_length != 0 ) {
            m_check_stat = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        // Fallback for static file serving.
        return GET_REQUEST;


    } else if (strncasecmp( text, "Connection:", 11 ) == 0 ) {
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        } else if ( strcasecmp( text, "close" ) == 0 ) {
            m_linger = false;
        }

    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);

    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;

    } else if (strncasecmp(text, "Authorization:", 14) == 0) {
        text += 14;
        text += strspn(text, " \t");

        std::string auth = text;
        if (auth.find("Bearer ") == 0) {
            token = auth.substr(7);
        }
    }
    return NO_REQUEST;
}


// Parse request body
// Didn't really parse the HTTP request body, only check if it's fully read
// Then route it to the corresponding business logic

http_conn::HTTP_CODE http_conn::parse_content(char * text){
    if (m_read_index >= (m_content_length + m_checked_index ) ) {
        text[ m_content_length ] = '\0';
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

    return BAD_REQUEST;
}


// Handle POST api/register
http_conn::HTTP_CODE http_conn::handle_register(char * text){
        std::string body(text);
        std::cout << "Body: " << body << std::endl;
        Logger::get_instance()->log(INFO, "POST /api/register body=" + body);

        json j;
        try {
            j = json::parse(body);
        } catch(const json::parse_error& e){
            json_res = "{\"error\":\"invalid JSON format\"}";
            return BAD_REQUEST;
        } catch (...){
            json_res = "{\"error\":\"internal error\"}";
            return INTERNAL_ERROR;
        }

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

        std::set<std::string> allowed = {"id", "name", "email", "password"};
        std::string cols;
        std::string values;

        for (auto it = j.begin(); it != j.end(); ++it){
            std::string key = it.key();

            if(!allowed.count(key)) continue;

            cols += key + ",";
            if(it.value().is_number()){
                values += it.value().dump() + ",";
            } else if (it.value().is_string()){
                std::string val = it.value();
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


// Handle POST api/login
http_conn::HTTP_CODE http_conn::handle_login(char* text) {

    std::string body(text);
    Logger::get_instance()->log(INFO, "POST /api/login body=" + body);

    json j;
    try {
        j = json::parse(body);
    } catch(const json::parse_error& e){
        json_res = "{\"error\":\"invalid JSON format\"}";
        return BAD_REQUEST;
    } catch (...){
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

    return GET_RESOURCE;
}


// Handle POST api/logout
http_conn::HTTP_CODE http_conn::handle_logout() {
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


// Parse simple key=value query strings used by the API routes.
void http_conn::parse_query(char* query_string, std::string& key, std::string& value) {
    char* equal = strchr(query_string, '=');
    if (equal) {
        *equal = '\0';
        key = query_string;
        value = equal + 1;
    }
}

// Handle GET /api/resources and GET /api/resources?id=x.
http_conn::HTTP_CODE http_conn::handle_get_resources(){

    std::optional<int> user_id = UserService::get_user_id_from_token(token);
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

        if (key != "id" || value.empty() ||
            !std::all_of(value.begin(), value.end(), ::isdigit)) {
            json_res = "{\"error\":\"invalid parameter\"}";
            return BAD_REQUEST;
        }
    }

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


// Handle DELETE /api/resources?id=x.
http_conn::HTTP_CODE http_conn::handle_delete_resource(){

    std::optional<int> user_id = UserService::get_user_id_from_token(token);
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


// Handle POST /api/resources.
http_conn::HTTP_CODE http_conn::handle_post_resource(char * text){
    std::optional<int> user_id = UserService::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }

    std::string body(text);
    Logger::get_instance()->log(INFO, "POST /api/resource body=" + body);

    json j;
    try {
        j = json::parse(body);
    } catch(const json::parse_error& e){
        json_res = "{\"error\":\"invalid JSON format\"}";
        return BAD_REQUEST;
    } catch (...){
        json_res = "{\"error\":\"internal error\"}";
        return INTERNAL_ERROR;
    }

    if (!j.contains("id") || !j["id"].is_number_integer()){
        json_res = "{\"error\":\"invalid id\"}";
        return BAD_REQUEST;
    }
    if (j.contains("is_file") && !j["is_file"].is_boolean()){
        json_res = "{\"error\":\"invalid is_file\"}";
        return BAD_REQUEST;
    }

    std::set<std::string> allowed = {"id", "title", "content", "is_file"};
    std::string cols;
    std::string values;
    cols += "user_id,";
    values += std::to_string(user_id.value()) + ",";

    for (auto it = j.begin(); it != j.end(); ++it){
        std::string key = it.key();

        if(!allowed.count(key)) continue;

        cols += key + ",";

        if (key == "is_file"){
            values += it.value().get<bool>() ? "1," : "0,";
        } else if(it.value().is_number()){
            values += it.value().dump() + ",";
        } else if (it.value().is_string()){
            std::string val = it.value();
            values += "'" + val + "',";
        }
    }
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


// Handle PUT /api/resources.
http_conn::HTTP_CODE http_conn::handle_put_resource(char* text){
    std::optional<int> user_id = UserService::get_user_id_from_token(token);
    if (user_id == std::nullopt) {
        json_res = "{\"error\":\"unauthorized\"}";
        Logger::get_instance()->log(ERROR, "unauthorized");
        return FORBIDDEN_REQUEST;
    }

    std::string body(text);
    Logger::get_instance()->log(INFO, "PUT /api/resource body=" + body);
    json j;

    try {
        j = json::parse(body);
    } catch(const json::parse_error& e){
        json_res = "{\"error\":\"invalid JSON format\"}";
        return BAD_REQUEST;
    } catch (...){
        json_res = "{\"error\":\"internal error\"}";
        return INTERNAL_ERROR;
    }

    if (!j.contains("id") || !j["id"].is_number_integer()){
        json_res = "{\"error\":\"invalid id\"}";
        return BAD_REQUEST;
    }
    int id = j["id"];

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

    set_clause.pop_back();

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


// Handle POST api/files/upload-url
http_conn::HTTP_CODE http_conn::handle_create_upload_url(char* text) {
    std::optional<int> user_id = UserService::get_user_id_from_token(token);
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
    std::optional<int> user_id = UserService::get_user_id_from_token(token);
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


// Static file serving path: map a readable file into memory for writev().
http_conn::HTTP_CODE http_conn::do_request(){

    strcpy(m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );

    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    int fd = open( m_real_file, O_RDONLY );
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}


// Write the response to the write buffer
bool http_conn::process_write(HTTP_CODE ret) {
    // Local helper function to generate response where the body is stored in [json_res]
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



// Release memory-mapped static file content.
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}


// Append printf-style formatted text to the response buffer.
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_index >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );

    int len = vsnprintf( m_write_buf + m_write_index,
                WRITE_BUFFER_SIZE - 1 - m_write_index,
                format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_index ) || len < 0) {
        va_end( arg_list );
        return false;
    }
    m_write_index += len;
    va_end( arg_list );
    return true;
}


// Adds the HTTP status line to the response buffer.
bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}


// Adds the Content-Type header.
bool http_conn::add_content_type(const char* type) {
    return add_response("Content-Type: %s\r\n", type);
}

// Adds the Content-Length header.
bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

// Adds the connection persistence header.
bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

// Ends the HTTP header block.
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}


// Adds the standard response headers.
bool http_conn::add_headers(int content_len, const char* type) {
    return add_content_length(content_len)
        && add_content_type(type)
        && add_linger()
        && add_blank_line();
}


// Adds a plain content body to the response buffer.
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}
