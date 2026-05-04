# C++ Webserver

A Scalable C++17 HTTP web server with user authentication, resource management, Redis-backed sessions, MySQL persistence, Nginx load balancing, and MinIO file storage.

## What It Does

This project exposes a small authenticated API for users and resources.

At the application level, it supports:

- User registration.
- User login and logout.
- Token-based API authentication.
- Resource CRUD operations.
- Text resources stored directly in MySQL.
- File resources stored in MinIO, with the resource table storing the file URL.
- Presigned MinIO upload URLs so clients can upload files directly to storage.
- Presigned MinIO download URLs so clients can download files directly from storage.
- Deleting a file resource also deletes the related MinIO object.
- Python API tests for login, resource management, file upload/download URL flow, validation, and cleanup.

The typical file flow is:

```text
Client Upload File:
client -> backend: ask for upload URL
backend -> client: return presigned MinIO PUT URL(upload) + public URL
client -> MinIO: upload file bytes
client -> backend: create resource with content=<public_url>, is_file=true

Client Downlaod File:
client -> backend: ask for download URL by resource_id
backend -> client: return presigned MinIO GET URL
client -> MinIO: download file bytes
```

## Environment

The Docker Compose environment runs:

- `sys-nginx`: reverse proxy and load balancer, exposed at `localhost:8080`.
- `sys-web-1`: first C++ server container, exposed directly at `localhost:8081`.
- `sys-web-2`: second C++ server container, exposed directly at `localhost:8082`.
- `sys-mysql`: MySQL 8.0, exposed at `localhost:3306`.
- `sys-redis`: Redis 7, exposed at `localhost:6379`.
- `sys-minio`: MinIO object storage, API at `localhost:9000`, console at `localhost:9001`.

Important configured values:

```yaml
MYSQL_DATABASE: webdb
MYSQL_USER: webuser
MYSQL_PASSWORD: webpass123
MYSQL_ROOT_PASSWORD: root123

MINIO_ENDPOINT: http://minio:9000
MINIO_PUBLIC_ENDPOINT: http://localhost:9000
MINIO_BUCKET: webserver-files
MINIO_ACCESS_KEY: minioadmin
MINIO_SECRET_KEY: minioadmin
MINIO_UPLOAD_URL_EXPIRES: 300
MINIO_DOWNLOAD_URL_EXPIRES: 300
MINIO_MAX_FILENAME_LENGTH: 255
```

MinIO console login:

```text
URL: http://localhost:9001
Username: minioadmin
Password: minioadmin
```

Create a bucket named:

```text
webserver-files
```

## Run The Project

From the project root:

```bash
cd /Volumes/codefield/webserver
```

Build and start the containers:

```bash
docker compose build
docker compose up -d
```

Build the C++ server inside both web containers:

```bash
docker exec sys-web-1 bash -lc "cd /workspace && cmake -S . -B build && cmake --build build"
docker exec sys-web-2 bash -lc "cd /workspace && cmake -S . -B build && cmake --build build"
```

Start the server process from inside each web container.

In one terminal, enter the first container and run the server:

```bash
docker exec -it sys-web-1 bash
cd /workspace
./build/bin/webserver 8080
```

In a second terminal, enter the second container and run the server:

```bash
docker exec -it sys-web-2 bash
cd /workspace
./build/bin/webserver 8080
```

Check that the containers are running:

```bash
docker ps
```


The main API entry point is:

```text
http://localhost:8080
```

This goes through Nginx and load-balances between `web1` and `web2`.

## Database Setup

The application expects MySQL tables for users, sessions, and resources. If your database is empty, create tables like this:

```bash
# enter the mysql container 
docker exec -it sys-mysql mysql -uroot -proot123 webdb
```

Then run:

```sql
CREATE TABLE users (
    id INT PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    email VARCHAR(255) NOT NULL UNIQUE,
    password VARCHAR(255) NOT NULL
);

CREATE TABLE sessions (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    token VARCHAR(255) NOT NULL UNIQUE,
    expires_at DATETIME NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE resources (
    id INT PRIMARY KEY,
    user_id INT NOT NULL,
    title VARCHAR(255),
    content TEXT,
    is_file BOOLEAN DEFAULT FALSE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

For the current API tests, the database also expects a sample user and two resources. After the tables exist and the webserver processes are running, create those records through the API from a local terminal.

Create the sample user:

```bash
curl -s -X POST http://localhost:8080/api/register \
  -H "Content-Type: application/json" \
  -d '{"id":1,"name":"Andrew","email":"andrew@test.com","password":"hash3"}'
```

Login as the sample user and copy the returned token:

```bash
curl -s -X POST http://localhost:8080/api/login \
  -H "Content-Type: application/json" \
  -d '{"email":"andrew@test.com","password":"hash3"}'
```

The response contains a token:

```json
{"token":"...","user_id":1}
```

Save the token in your local terminal:

```bash
TOKEN="paste_the_login_token_here"
```

Create the two sample resources:

```bash
curl -s -X POST http://localhost:8080/api/resources \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"id":1,"title":"First resource","content":"hello world","is_file":false}'

curl -s -X POST http://localhost:8080/api/resources \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"id":2,"title":"Second resource","content":"hello People","is_file":false}'
```

Log out the account:
```bash
curl -s -X POST http://localhost:8080/api/logout \
  -H "Authorization: Bearer $TOKEN"
```


## For Specific Function Test, Send Requests From Your Local Terminal
### Login 

```bash
curl -s -X POST http://localhost:8080/api/login \
  -H "Content-Type: application/json" \
  -d '{"email":"andrew@test.com","password":"hash3"}'
```

The response contains a token:

```json
{"token":"...","user_id":1}
```

Save it in your shell:

```bash
TOKEN="<paste-token-here>"
```

### List Resources

```bash
curl -s http://localhost:8080/api/resources \
  -H "Authorization: Bearer $TOKEN"
```

### Get One Resource

```bash
curl -s "http://localhost:8080/api/resources?id=1" \
  -H "Authorization: Bearer $TOKEN"
```

### Create A Text Resource

```bash
curl -s -X POST http://localhost:8080/api/resources \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"id":1001,"title":"Note","content":"hello from curl","is_file":false}'
```

### Update A Resource

```bash
curl -s -X PUT http://localhost:8080/api/resources \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"id":1001,"title":"Updated note","content":"updated text"}'
```

### Delete A Resource

```bash
curl -s -X DELETE "http://localhost:8080/api/resources?id=1001" \
  -H "Authorization: Bearer $TOKEN"
```

### Upload A file: Two Step Approach
### First Step: Get A File Upload URL

```bash
curl -s -X POST http://localhost:8080/api/files/upload-url \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"filename":"hello.txt","content_type":"text/plain"}'
```

The response contains:

- `upload_url`: temporary MinIO URL for uploading bytes.
- `public_url`: stable URL stored later in the resource content.
- `object_key`: MinIO object key.
- `expires_in`: upload URL lifetime in seconds.

Save the necessary URL in your shell 

### Upload File Bytes To MinIO

Copy the `upload_url` from the previous response:
UPLOAD_URL="<paste-upload-url-here>"
Copy the `public_url` from the upload URL response:
PUBLIC_URL="<paste-public-url-here>"

```bash
echo "hello file" > hello.txt #write content into file hello.txt

curl -X PUT "$UPLOAD_URL" \
  -H "Content-Type: text/plain" \
  --data-binary @hello.txt
```

### Second Step: Create the File Resource URL in the DB
### Create A File Resource

```bash
curl -s -X POST http://localhost:8080/api/resources \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d "{\"id\":1002,\"title\":\"Uploaded file\",\"content\":\"$PUBLIC_URL\",\"is_file\":true}"
```

### Get A File Download URL
```bash
curl -s "http://localhost:8080/api/files/download-url?resource_id=1002" \
  -H "Authorization: Bearer $TOKEN"
```

The response contains `download_url`.

### Download The File From MinIO

```bash
DOWNLOAD_URL="<paste-download-url-here>"
curl -L "$DOWNLOAD_URL" -o downloaded-hello.txt
# then the file is downloaded in local directory
```

### Logout

```bash
curl -s -X POST http://localhost:8080/api/logout \
  -H "Authorization: Bearer $TOKEN"
```

## Run Tests
Run the Python API tests in local terminal:

```bash
python3 -m pytest tests/api_tests.py
```

The API tests expect the Docker services, both webserver processes, MySQL data, Redis, and MinIO bucket to be available.

## Architecture

The project uses three main storage systems:

- **MySQL** is the persistent database. It stores users, sessions, and resource metadata.
- **Redis** is the cache/session acceleration layer. It stores token-to-user mappings so authenticated requests can avoid hitting MySQL every time.
- **MinIO** is the object storage layer. It stores uploaded file bytes, while MySQL stores the related resource metadata and public file URL.

Main MySQL tables:

```sql
users (
    id INT PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    email VARCHAR(255) NOT NULL UNIQUE,
    password VARCHAR(255) NOT NULL
)

sessions (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    token VARCHAR(255) NOT NULL UNIQUE,
    expires_at DATETIME NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
)

resources (
    id INT PRIMARY KEY,
    user_id INT NOT NULL,
    title VARCHAR(255),
    content TEXT,
    is_file BOOLEAN DEFAULT FALSE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
)
```

For text resources, `resources.content` stores the text body. For file resources, `resources.content` stores the MinIO public URL and `resources.is_file` is `true`.

### Network Layer

Files:

- `src/main.cpp`
- `src/network/http_conn.h`
- `src/network/http_conn.cpp`
- `src/thread/threadpool.h`
- `src/thread/locker.h`

This layer owns the HTTP server mechanics:

- Creates the listening socket.
- Uses `epoll` for event-driven IO.
- Uses a thread pool to process requests.
- Parses HTTP request lines, headers, and bodies.
- Routes API requests by URL and method.
- Builds HTTP responses.
- Serves static files from `src/resources`.

Static file serving was added as early practice for the HTTP server and file response path. It is useful for learning and testing the lower-level webserver behavior, but it is not fully integrated into the current authenticated business logic. The main application flow is the JSON API plus MinIO-backed resource file upload/download.

`http_conn.cpp` is the main request handler. It maps paths such as `/api/login`, `/api/resources`, `/api/files/upload-url`, and `/api/files/download-url` to handler functions.

### Service Layer

Files:

- `src/service/user_service.cpp`
- `src/service/resource_service.cpp`
- `src/service/storage_service.cpp`

This layer owns business logic:

- `UserService`: register/login behavior, password verification, session token creation.
- `ResourceService`: resource list/get/update/delete behavior, file-resource checks, delete-file cleanup.
- `StorageService`: MinIO URL generation, AWS Signature V4 signing, upload validation, file deletion.

The service layer sits between HTTP handlers and DAO/storage code.

### DAO Layer

Files:

- `src/dao/user_dao.cpp`
- `src/dao/resource_dao.cpp`

This layer talks to MySQL:

- `UserDAO`: user lookup, session insert/delete, token validation, Redis token cache fallback.
- `ResourceDAO`: create, list, get, update, and delete resources.

### Database Layer

Files:

- `src/db/connection_pool.cpp`
- `src/db/connection_pool.h`

This layer manages a pool of MySQL connections. The server initializes it in `src/main.cpp` with:

```cpp
connPool->init("sys-mysql", "webuser", "webpass123", "webdb", 3306, 10);
```

### Cache Layer

Files:

- `src/cache/redis_client.cpp`
- `src/cache/redis_client.h`

Redis stores session token lookups so normal authenticated requests do not always need to hit MySQL.

### Storage Layer

Files:

- `src/service/storage_service.cpp`
- `docker-compose.yml`

MinIO stores uploaded file bytes. The backend does not usually stream file bytes through itself. Instead, it generates presigned URLs:

- `PUT` presigned URL for upload.
- `GET` presigned URL for download.
- `DELETE` presigned URL used internally when deleting file resources.

### Utility Layer

Files:

- `src/utils/auth_utils.h`
- `src/utils/logger.cpp`
- `src/utils/logger.h`

This layer contains password hashing helpers and logging.

## Main API Endpoints

### `POST /api/register`

Registers a user.

Body:

```json
{
  "id": 2,
  "name": "Alice",
  "email": "alice@test.com",
  "password": "password"
}
```

Logic:

- Parses JSON.
- Validates required fields.
- Hashes/stores user information through service and DAO layers.
- Returns success or error JSON.

### `POST /api/login`

Logs in a user.

Body:

```json
{
  "email": "andrew@test.com",
  "password": "hash3"
}
```

Logic:

- Looks up user by email.
- Verifies password.
- Creates a session token.
- Stores session in MySQL.
- Caches token in Redis.
- Returns `token` and `user_id`.

### `POST /api/logout`

Logs out the current user.

Header:

```text
Authorization: Bearer <token>
```

Logic:

- Validates token.
- Deletes token from Redis.
- Deletes session row from MySQL.

### `GET /api/resources`

Lists resources owned by the authenticated user.

Header:

```text
Authorization: Bearer <token>
```

Logic:

- Validates token.
- Finds user ID.
- Loads resources for that user from MySQL.
- Returns JSON array including `id`, `title`, `content`, and `is_file`.

### `GET /api/resources?id=X`

Gets one resource owned by the authenticated user.

Logic:

- Validates token.
- Validates `id`.
- Loads resource by `user_id + id`.
- Returns text resource or file resource metadata.

### `POST /api/resources`

Creates a resource.

Text resource body:

```json
{
  "id": 1001,
  "title": "Note",
  "content": "hello",
  "is_file": false
}
```

File resource body:

```json
{
  "id": 1002,
  "title": "Uploaded file",
  "content": "http://localhost:9000/webserver-files/users/1/uploads/file.txt",
  "is_file": true
}
```

Logic:

- Validates token.
- Reads `id`, `title`, `content`, and optional `is_file`.
- Stores the row in MySQL.
- If `is_file=true`, `content` should be the MinIO public URL.

### `PUT /api/resources`

Updates a resource.

Body:

```json
{
  "id": 1001,
  "title": "New title",
  "content": "new content"
}
```

Logic:

- Validates token.
- Validates `id`.
- Updates allowed fields for a resource owned by the user.

### `DELETE /api/resources?id=X`

Deletes a resource.

Logic:

- Validates token.
- Loads resource by `user_id + id`.
- If `is_file=true`, deletes the object from MinIO.
- Deletes the database row.

### `POST /api/files/upload-url`

Creates a presigned MinIO upload URL.

Body:

```json
{
  "filename": "report.pdf",
  "content_type": "application/pdf"
}
```

Logic:

- Validates token.
- Validates filename and content type.
- Rejects path traversal and blocked extensions.
- Creates an object key under `users/<user_id>/uploads/...`.
- Returns a presigned `PUT` URL and a `public_url`.

### `GET /api/files/download-url?resource_id=X`

Creates a presigned MinIO download URL for a file resource.

Logic:

- Validates token.
- Loads the resource by `user_id + resource_id`.
- Requires `is_file=true`.
- Extracts MinIO object key from the stored public URL.
- Returns a presigned `GET` URL.

## Notes

- The backend usually does not download file bytes itself. It returns presigned URLs so the client uploads/downloads directly with MinIO.
- Static file requests are mainly for initial HTTP server practice and are separate from the authenticated resource business logic.
- The `content` column currently stores either plain text or a file public URL. A future improvement would be storing `object_key` in a separate database column.
- If you change C++ source files, rebuild the server executable and restart both webserver processes.
- If you change `Dockerfile`, rebuild the Docker images.
- If you change Docker Compose environment variables, recreate the containers.
