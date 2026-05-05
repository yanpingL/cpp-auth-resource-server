#include "storage_service.h"

#include <miniocpp/client.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <string>
#include <set>

namespace {

struct MinioEndpoint {
    std::string host;
    bool https;
};

// Reads an environment variable, returning the fallback when it is missing or empty.
std::string get_env_or_default(const char* name, const std::string& fallback) {
    // Look for an environment variable whose name is stored in [name],
    // and store its value in [value]
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

// Reads a positive integer from an environment variable, or uses the fallback.
int get_env_int_or_default(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    /*
    1) std::strtol --> convert the c-style string [value] into a long type number
    2) [value]: the string to parse; [&end]: a pointer to [end], so "strtol" can update it
    3) After parsing, [end] points to the first character that was not part of the number
    4) 10: number base, base 10 means decimal numbers
    */
    long parsed = std::strtol(value, &end, 10);

    /*
    check 3 invalid cases:
    1) end == value: [strtol] could not parse any number at all like value = "abc", 
        so end points to the start of hte string
    2) *end != '\0': parsing stopped before the end of the string (eg. value = "123acb"),
        --> string is not a clean integer
    3) parsed <= 0: number is zero or negative
    */ 
    if (end == value || *end != '\0' || parsed <= 0) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

// Removes all trailing slashes so endpoint strings can be joined predictably.
// Useful for building paths or URLs
std::string trim_trailing_slash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

// Converts an endpoint URL into the host and scheme format expected by MinIO SDK.
MinioEndpoint parse_minio_endpoint(const std::string& endpoint) {
    MinioEndpoint parsed{trim_trailing_slash(endpoint), true};

    const std::string http_prefix = "http://";
    const std::string https_prefix = "https://";

    // check if the http_prefix start from index 0 of the parsed.host
    // the return value is the starting index of the found text
    // rfind(substr, 0): consider the match starting at or before index 0
    // rfind(substr): start searching from right to left 
    if (parsed.host.rfind(http_prefix, 0) == 0) {
        // string.substr(x): return everything of string after 7 position
        parsed.host = parsed.host.substr(http_prefix.size());
        parsed.https = false;
    } else if (parsed.host.rfind(https_prefix, 0) == 0) {
        parsed.host = parsed.host.substr(https_prefix.size());
        parsed.https = true;
    }

    return parsed;
}


// Creates a MinIO SDK base URL from the configured endpoint and region.
/*
1) endpoint: MinIO server endpoint 
    (eg. "http://localhost:9000"; "https://minio.example.com")

2) minio::s3::BaseUrl --> type from the MinIO C++ SDK. represents the base URL/configuration
    the SDK should use when seding S3-compatible requests
Note: Take configured MinIO endpoint, normalize it into host + HTTP/HTTPS, read the MinIO regions
from the environment, then create the SDK's base URL object.

*/
minio::s3::BaseUrl make_minio_base_url(const std::string& endpoint) {
    // calls the helper function to cleans & splits the endpoint into:
    // parsed.host; parsed.https
    const MinioEndpoint parsed = parse_minio_endpoint(endpoint);
    // reads the environment variable [MINIO_REGION], if empty or not exist, 
    //  use the default value
    const std::string region = get_env_or_default("MINIO_REGION", "us-east-1");
    // constructs & returns a [BaseUrl] object for the MinIO SDK.
    return minio::s3::BaseUrl(parsed.host, parsed.https, region);
}


// Creates a static credential provider from the MinIO access key and secret.
// This function creates the credentials object that the MinIO SDK uses to authenticate requests.
minio::creds::StaticProvider make_minio_provider() {
    // access key & secre key are fixed, not dynamically refreshed from another service
    // Build a MinIO crednetial provider, which is responsible for giving the SDK credentials
    // when it performs operations like upload/download/delete, or list objects
    return minio::creds::StaticProvider(
        get_env_or_default("MINIO_ACCESS_KEY", "minioadmin"),
        get_env_or_default("MINIO_SECRET_KEY", "minioadmin"));
}

// Replaces unsafe filename characters and rejects empty or dot-only names.
// Validates file name
std::string sanitize_filename(const std::string& filename) {
    std::string clean;
    clean.reserve(filename.size());
    // we use unsigned char instead of plain char because functions like 
    // std::isalnum expect either EOF or a value representable as unsigned char (0-225)
    // char is signed (can be negative, --> cause undefined behaviour)
    // [End of file]: special constant integer (usually -1)
    /*
    Allowed characters are: letters; umbers; .; -; _
    */
    for (unsigned char c : filename) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') {
            // converts the unsigned char back to normal char before appending it to the string
            clean.push_back(static_cast<char>(c));
        } else {
            clean.push_back('_');
        }
    }
    // after clean, it rejects unsafe or useless results
    if (clean.empty() || clean == "." || clean == "..") {
        return "";
    }
    return clean;
}

// Lowercases a string for case-insensitive validation checks.
std::string to_lower(std::string value) {
    /*
    Make a lowercase copy of the string so later checks can ignore uppercase/lowercase differences

    1) std::transform --> applies a function to every element in a range
    2) the range in this function is [value.begin(), value.end()]
    3) output destination: value.begin() --> function writes the transformed characters back
        into the same string; in-place transformation of the local copy
    4) the lambda function: takes unsigned char for safety. std::tolower() receive values valid
        as unsigned char or EOF (0-225)
    5) std::tolower(c): converts an [int], not a [char], need to convert back to [char]
    
    */
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Returns the lowercase file extension, including the leading dot.
std::string file_extension(const std::string& filename) {
    // return the index of the last dot
    // if no dot in the filename, returns [std::string::npos]
    const std::size_t dot = filename.find_last_of('.');
    // npos --> means "not found" then return empty string
    if (dot == std::string::npos) {
        return "";
    }
    // turn the file extension into lowercase letter
    return to_lower(filename.substr(dot));
}

// Rejects filenames that try to include directories or parent-directory markers.
bool has_path_traversal(const std::string& filename) {
    return filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos;
}

// Checks whether an upload MIME type is allowed by this application.
bool is_allowed_content_type(const std::string& content_type) {
    static const std::set<std::string> allowed = {
        "text/plain",
        "application/pdf",
        "image/png",
        "image/jpeg",
        "application/msword",
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
    };
    return allowed.count(to_lower(content_type)) > 0;
}

// Blocks executable or script-like extensions even when the MIME type is allowed.
bool is_blocked_extension(const std::string& filename) {
    static const std::set<std::string> blocked = {
        ".bat", ".cmd", ".com", ".dll", ".exe", ".js", ".ps1", ".sh"
    };
    return blocked.count(file_extension(filename)) > 0;
}

// Returns the current Unix timestamp in milliseconds for unique object keys.
long long current_epoch_millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

// Asks the MinIO SDK to create a presigned object URL for the requested method.
storage_json create_presigned_url(
    minio::http::Method method,
    const std::string& endpoint,
    const std::string& bucket,
    const std::string& object_key,
    int expires_seconds) {

    storage_json res;
    minio::s3::BaseUrl base_url = make_minio_base_url(endpoint);
    minio::creds::StaticProvider provider = make_minio_provider();
    /*
    build client. This client is local to the function
    created each time this function is called, used once to generate the URL,
    then destroyed when function returns.
    */
    minio::s3::Client client(base_url, &provider); 

    minio::s3::GetPresignedObjectUrlArgs args;
    args.bucket = bucket;
    args.object = object_key;
    args.method = method;
    // converts from [int] to [unsigned int] as SDK expects unsigned value
    args.expiry_seconds = static_cast<unsigned int>(expires_seconds);

    // ask the SDK to generate the presigned URL
    minio::s3::GetPresignedObjectUrlResponse resp =
        client.GetPresignedObjectUrl(args);
    if (!resp) {
        res["error"] = resp.Error().String();
        return res;
    }

    res["url"] = resp.url;
    return res;
}


// Extracts the stored object key from a public MinIO URL.
/*
object key: path/name of the stored object inside a bucket
http://localhost:9000/resources/users/42/avatar.png
public_endpoint = "http://localhost:9000"
bucket = "resources"
object key:
users/42/avatar.png


*/
std::string extract_object_key_from_public_url(
    const std::string& public_url,
    const std::string& public_endpoint,
    const std::string& bucket) {

    // part of URL come before the object key
    const std::string prefix = public_endpoint + "/" + bucket + "/";
    // public URL should start with [prefix]
    if (public_url.rfind(prefix, 0) != 0) {
        return "";
    }
    return public_url.substr(prefix.size());
}

} // namespace


// Validates upload metadata and returns a presigned PUT URL plus public file info.
storage_json StorageService::create_upload_url(
    int user_id,
    const std::string& filename,
    const std::string& content_type) {

    storage_json res;

    // validate filename
    const std::string clean_filename = sanitize_filename(filename);
    if (clean_filename.empty()) {
        res["error"] = "invalid filename";
        return res;
    }

    const int max_filename_length =
        get_env_int_or_default("MINIO_MAX_FILENAME_LENGTH", 255);
    if (filename.size() > static_cast<std::size_t>(max_filename_length)) {
        res["error"] = "filename too long";
        return res;
    }
    // check if the file contains directories or parent directory markers
    if (has_path_traversal(filename)) {
        res["error"] = "invalid filename";
        return res;
    }
    // check if the file extension is blocked
    if (is_blocked_extension(clean_filename)) {
        res["error"] = "blocked file type";
        return res;
    }

    if (content_type.empty()) {
        res["error"] = "invalid content_type";
        return res;
    }

    if (!is_allowed_content_type(content_type)) {
        res["error"] = "unsupported content_type";
        return res;
    }

    const std::string bucket = get_env_or_default("MINIO_BUCKET", "webserver-files");
    const std::string public_endpoint = trim_trailing_slash(
        get_env_or_default("MINIO_PUBLIC_ENDPOINT", "http://localhost:9000"));

    const std::string object_key =
        "users/" + std::to_string(user_id) + "/uploads/" +
        std::to_string(current_epoch_millis()) + "-" + clean_filename;

    const std::string public_url =
        public_endpoint + "/" + bucket + "/" + object_key;

    const int expires_seconds =
        get_env_int_or_default("MINIO_UPLOAD_URL_EXPIRES", 300);

    storage_json upload = create_presigned_url(
        minio::http::Method::kPut,
        public_endpoint,
        bucket,
        object_key,
        expires_seconds);
    if (upload.contains("error")) {
        return upload;
    }

    res["upload_url"] = upload["url"];
    res["public_url"] = public_url;
    res["object_key"] = object_key;
    res["bucket"] = bucket;
    res["content_type"] = content_type;
    res["expires_in"] = expires_seconds;

    return res;
}


// Returns a short-lived presigned GET URL for an existing public file URL. 
// To download file
storage_json StorageService::create_download_url(const std::string& public_url) {
    storage_json res;

    const std::string bucket = get_env_or_default("MINIO_BUCKET", "webserver-files");
    const std::string public_endpoint = trim_trailing_slash(
        get_env_or_default("MINIO_PUBLIC_ENDPOINT", "http://localhost:9000"));

    const std::string object_key =
        extract_object_key_from_public_url(public_url, public_endpoint, bucket);
    if (object_key.empty()) {
        res["error"] = "invalid file url";
        return res;
    }

    const int expires_seconds =
        get_env_int_or_default("MINIO_DOWNLOAD_URL_EXPIRES", 300);

    storage_json download = create_presigned_url(
        minio::http::Method::kGet,
        public_endpoint,
        bucket,
        object_key,
        expires_seconds);
    if (download.contains("error")) {
        return download;
    }

    res["download_url"] = download["url"];
    res["public_url"] = public_url;
    res["object_key"] = object_key;
    res["bucket"] = bucket;
    res["expires_in"] = expires_seconds;

    return res;
}


// Deletes the MinIO object referenced by a public URL.
bool StorageService::delete_file(const std::string& public_url, std::string& error) {
    const std::string bucket = get_env_or_default("MINIO_BUCKET", "webserver-files");
    const std::string public_endpoint = trim_trailing_slash(
        get_env_or_default("MINIO_PUBLIC_ENDPOINT", "http://localhost:9000"));
    const std::string internal_endpoint = trim_trailing_slash(
        get_env_or_default("MINIO_ENDPOINT", public_endpoint));

    const std::string object_key =
        extract_object_key_from_public_url(public_url, public_endpoint, bucket);
    if (object_key.empty()) {
        error = "invalid file url";
        return false;
    }

    minio::s3::BaseUrl base_url = make_minio_base_url(internal_endpoint);
    minio::creds::StaticProvider provider = make_minio_provider();
    minio::s3::Client client(base_url, &provider);

    minio::s3::RemoveObjectArgs args;
    args.bucket = bucket;
    args.object = object_key;

    minio::s3::RemoveObjectResponse resp = client.RemoveObject(args);
    if (!resp) {
        error = resp.Error().String();
        return false;
    }

    return true;
}
