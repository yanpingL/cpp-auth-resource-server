#include "http_conn.h"

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


// roote directory of the website (need update)
const char* doc_root = "/workspace/project/resources";

/*
- setnonblock()
- addfd()
- removedfd()
- modfd()
are all helper functions, nor related to the http_conn class or http_conn object
so when using the function in other files we need the keyword "extern"
*/
// set the fd as nonblocking
void setnonblock(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);

}


// add the fd to the epoll instance
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
    // set the fd nonblok
    setnonblock(fd);
}


//  delete fd from epoll 
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


// modify the fd, reset the socket to make sure the EPOLLIN can be captured
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


// close connection
// Each thread/user/connection maintains its own m_sockfd
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // close a connection, #users --;
    }
}


// initialize newly accepted connection
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
    // add to epoll instance
    addfd(m_epollfd, m_sockfd, 1);

    // total user number + 1
    m_user_count++;
    init();
} 

// initialize other infos
void http_conn::init(){
    m_check_stat = CHECK_STATE_REQUESTLINE;  // initialize the state as parse the first line of request
    m_linger = false;

    m_method = GET; // default request method
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_index = 0;
    m_write_index = 0;

    json_res = "";
    apireq = 0;
    api_ret = NO_REQUEST;

    memset(m_read_buf, 0, READ_BUFFER_SIZE);
    memset(m_write_buf, 0, READ_BUFFER_SIZE);
    memset(m_real_file, 0, FILENAME_LEN);
}


// repeately read data from client until no more data or connection closed
// read data from the connection socket fd and store them in the read buffer
// This function is called in MAIN THREAD by corresponding http_conn object
bool http_conn::read(){
    if(m_read_index >= READ_BUFFER_SIZE)
        return false;
    // byte already read
    int bytes_read = 0;
    while(1) {
        /*
        m_sockfd: read from which fd
        m_read_buf+m_read_index: write the read content to where
        READ_BUFFER_SIZE - m_read_index: how many bytes are safe to read
        0: flag
        */
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        
        // handle the error case
        if(bytes_read == -1) {
            /*
                EAGAIN
                “We have read all available data for now”
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
    // std::cout << "Read all data in one time\n";
    // std::cout << "read data: \n" << m_read_buf << std::endl;
    return true;
}



// From this position, the functions below called in CHILDREN THREADS (individual http_conn object)
// parse one line, check based on \r\n
/*
.......\r\n
......\r\n
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


// parse HTTP reuqest fist line, get the method, target URL, HTTP version
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
    // fill the first ' ' as '\0' and move the pointer 1 position forward
    *m_url++ = '\0';
    char *method = text; // GET

    // need to update to other METHODS
    if (strcasecmp(method, "GET") == 0){
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0){
        m_method = POST;
    } else if (strcasecmp(method, "PUT") == 0){
        m_method = PUT;
    } else if (strcasecmp(method, "DELETE") == 0){
        m_method = DELETE;
    } 

    // check the HTTP version: /index.html HTTP/1.1
    // if the m_version points to null, error
    m_version = strpbrk(m_url, " \t");
    if (!m_version){
        return BAD_REQUEST;
    } 

    // /index.html\0hTTP/1.1
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }

    // check if the request is /api request
    // GET /api/user?id=x HTTP/1.1 
    if(strncmp(m_url, "/api/user", 9) == 0){
        apireq = 1;
        // GET method
        if (m_method == GET){
            api_ret = GET_RESOURCE;
            return handle_get_user();

            // POST method
        } else if (m_method == POST){
            api_ret = ADD_RESOURCE;
            m_check_stat = CHECK_STATE_HEADER;
            return NO_REQUEST;

            //PUT method
        } else if (m_method == PUT){
            api_ret = UPDATE_RESOURCE;

            // DELETE method
        } else if (m_method == DELETE){
            api_ret = DELETE_RESOURCE;
            handle_delete_user();
        }
    }
    /**
     * http://192.168.110.129:10000/index.html
    */

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


//Implement GET/api/user (USING nlohmann/json)
//GET/api/user?id=123 HTTP/1.1

// helper function used in handle_get_user()
void http_conn::parse_query(char* query_string, std::string& key, std::string& value) {
    char* equal = strchr(query_string, '=');
    if (equal) {
        *equal = '\0';
        key = query_string;
        value = equal + 1;
    }
}

// need to complete the last half of the parse_request_line
http_conn::HTTP_CODE http_conn::handle_get_user(){
    //parse query 
    char* query = strchr(m_url, '?');
    std::string id = "unkonw";

    if (query){
        *query++ = '\0';

        std::string key, value;
        parse_query(query, key, value);

        if(key == "id")
            id = value;
    }

    // server connect to DB
    MYSQL* conn = mysql_init(NULL);

    conn = mysql_real_connect(
        conn,
        "sys-mysql",
        "root",
        "root",
        "webdb",
        3306,
        NULL,
        0  
    );

    // if(!conn){
    //     json_res = "{\"error\":\"db connect failed\"}";
    //     return GET_RESOURCE;
    // }

    // build SQL
    std::string sql = "SELECT id, name, email FROM users WHERE id =" + id;

    /*
    Need update
    */
    if(mysql_query(conn, sql.c_str())){
        json_res = "{\"error\":\"query failed\"}";
        return GET_RESOURCE;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(result);

    // build JSON
    json res;
    if (row){
        res["id"] = row[0];
        res["name"] = row[1];
        res["email"] = row[2];
    } else {
        res["error"] = "user not found";
    }

    json_res = res.dump();
    // // build HTTP respons
    // add_status_line(200, ok_200_title);
    // add_content_length(json_str.size());
    // add_json_type();
    // add_linger();
    // add_blank_line();
    // add_content(json_str.c_str());
    m_check_stat = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/*
DELETE /api/user?id=1 HTTP/1.1
*/
void http_conn::handle_delete_user(){
    char* query = strchr(m_url, '?');
    std::string key, value;

    if (query){
        *query++ = '\0';
        parse_query(query, key, value);
        // if(key == "id"){
        //     id = value;
        // }
    }
    /*
    DB operation:
    1. if user found, delete
    2. if user not found, do nothing
    
    */

    json res;
    res["id"] = value;
    json_res = "Delete User Id: " + res.dump();
    return;
}


// parse request header
/*
common format:

GET Request (No Body)
GET /index.html HTTP/1.1 (request line)
(headers)
Host: localhost:8080
Connection: keep-alive
User-Agent: Mozilla/5.0
Accept: text/html
(after headers, start of body if any)

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

    // blank line encountered, header fully parsed
    // There is a blank line \r\n after the last header
    if(text[0] == '\0') {
        // if HTTP request has body, need to read m_content_length bytes body
        // state machine change to CHECK_STATE_CONTENT
        if (m_content_length != 0 ) {
            m_check_stat = CHECK_STATE_CONTENT;
            return NO_REQUEST; // request incomplete, still need to parse the body
        }
        // parsed complete HTTP request
        // if (apireq){
        //     return api_ret;
        // }
        return GET_REQUEST;

    } else if (strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // Parse Connection header bytes  [Connection: keep-alive]
        text += 11;
        // Move the pointer text forward past all leading spaces and tabs.
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
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

    } else {
        std::cout << "Oops! Unknown header " << text << std::endl;
    }
    // still incomplete
    return NO_REQUEST;
}


// parse request body
// Didn't really parse the HTTP request body, only check if it's fully read
http_conn::HTTP_CODE http_conn::parse_content(char * text){
    if (m_read_index >= (m_content_length + m_checked_index ) )
    {   
        // ensure string end
        text[ m_content_length ] = '\0';

        // need to parse the content with POST method
        if (m_method == POST){
            handle_post_content(text);

            /*========================
            add the resource to the DB

            ==========================
            */

        } else if (m_method == PUT){
            // only parse the full body content
            handle_put_content(text);
            /*========================
            update the resource to the DB
            if the resource not in the DB
            add it to the DB

            ==========================
            */

        }

        return GET_REQUEST;
    }
    return NO_REQUEST;
}


void http_conn::handle_post_content(char * text){
    // extract body
    std::string body(text);
    // debug
    std::cout << "Body: " << body << std::endl;
    // next step: parse JSON
    json j = json::parse(body);
    std::string id = j["id"];
    std::string user = j["name"];

    // now we compose the response content and assign that to json_res
    json res_msg;
    res_msg["id"] = id;
    res_msg["name"] = user; 
    json_res = res_msg.dump();
    return;
}


void http_conn::handle_put_content(char* text){
    // debug
    std::string body(text);
    std::cout << "Body: " << body << std::endl;

    json j = json::parse(body);
    int count = 0;

    for (auto it = j.begin(); it != j.end();++it){
        std::string key = it.key();
        std::string value = it.value();
        count++;
        std::cout << key << " : " << value << std::endl;
    }
    /*
    Now do the operation in the DB
    
    */
    json_res = body + std::to_string(count);
    return;
}




// host state machine
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;

    // we process the content in the read buffer line by line
    // In this block, the [break] only jump out of the switch, not whiles
    while(((m_check_stat == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
            || (line_status = parse_line()) == LINE_OK) {
        // parse a line of full complete data, 
        // or parse the request body also complete data 
        text = get_line();
        m_start_line = m_checked_index; // move pointer to the poition needs to be parsed
        std::cout << "got 1 http line: " << text << std::endl;

        switch(m_check_stat){
            // parse a line
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
                    if (apireq) {return api_ret;}
                    return do_request();
                } 
                break;
            }
            // parse body
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                // if(apireq){
                //     return api_ret;
                // }
                if (ret == GET_REQUEST){
                    if(apireq){return api_ret;}
                    return do_request();
                }
                // 
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
} //parse the request of HTTP



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


// 对内存映射区执行munmap操作, reverse the mapping
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


// write the data to be send in the write buffer
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
    add_content_length(content_len);
    add_content_type(type);
    add_linger();
    add_blank_line();
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


// bool http_conn::add_json_type(){
//     return add_response("Content-Type: application/%s\r\n", "json");
// }


// write the response to the write buffer
bool http_conn::process_write(HTTP_CODE ret) {
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
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ), "text"  );
            if ( ! add_content( error_400_form ) ) {
                return false;
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
            add_status_line(200, ok_200_title);
            add_headers(json_res.size(), "application/json");
            add_content(json_res.c_str());
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_index;
            m_iv_count = 1;
            return true;
        case ADD_RESOURCE:
            add_status_line(200, ok_200_title);
            add_headers(json_res.size(), "application/json");
            add_content(json_res.c_str());
            // m_iv[0].iov_base = m_write_buf;
            // m_iv[0].iov_len = m_write_index;
            distribute_data();
            m_iv_count = 1;
            return true;
        case UPDATE_RESOURCE:
            add_status_line(200, ok_200_title);
            add_headers(json_res.size(), "application/json");
            add_content(json_res.c_str());
            // m_iv[0].iov_base = m_write_buf;
            // m_iv[0].iov_len = m_write_index;
            distribute_data();
            m_iv_count = 1;
            return true;
        case DELETE_RESOURCE:
            add_status_line(200, ok_200_title);
            add_headers(json_res.size(), "application/json");
            add_content(json_res.c_str());
            // m_iv[0].iov_base = m_write_buf;
            // m_iv[0].iov_len = m_write_index;
            distribute_data();
            m_iv_count = 1;
            return true;
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
    int bytes_to_send = m_iv[0].iov_len + m_iv[1].iov_len; //bytes wait to be sent 

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
        temp = writev(m_sockfd, m_iv, m_iv_count);
        /* modify the m_iv[].iov_base & m_iv[1].iov_len
        */

        if (temp <= -1) {
            // in this case, the socket buffer is full, writev() is non-blocking 
            // and cannot progressing anymore.
            // no need to modify the m_iv

            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
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

        // case 2: file part
        if (m_iv_count == 2 && bytes_have_send > 0){
            m_iv[1].iov_base = (char*)m_iv[1].iov_base + bytes_have_send;
            m_iv[1].iov_len -= bytes_have_send;
        }

        // recompute remaining bytes
        bytes_to_send = m_iv[0].iov_len +(m_iv_count == 2 ? m_iv[1].iov_len : 0);

        // bytes_to_send -= temp;
        // bytes_have_send += temp;
        if ( bytes_to_send <= 0 ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
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



// called by the threads in the threads poll
// interface to process the request of HTTP
void http_conn::process() {

    // parse the request of HTTP
    // from the connection socket fd
    HTTP_CODE read_ret = process_read();
    // after finishing the process_read(), 
    
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
    // after generating the response, we register EPOLLOUT to the event to the socket tracked by m_epollfd
    // Notify me when this socket is writable so I can actually send data.
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}