#include "storage_service.h"

#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <set>
#include <vector>

namespace {

std::string get_env_or_default(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

int get_env_int_or_default(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

std::string trim_trailing_slash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string sanitize_filename(const std::string& filename) {
    std::string clean;
    clean.reserve(filename.size());

    for (unsigned char c : filename) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') {
            clean.push_back(static_cast<char>(c));
        } else {
            clean.push_back('_');
        }
    }

    if (clean.empty() || clean == "." || clean == "..") {
        return "";
    }
    return clean;
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string file_extension(const std::string& filename) {
    const std::size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos) {
        return "";
    }
    return to_lower(filename.substr(dot));
}

bool has_path_traversal(const std::string& filename) {
    return filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos;
}

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

bool is_blocked_extension(const std::string& filename) {
    static const std::set<std::string> blocked = {
        ".bat", ".cmd", ".com", ".dll", ".exe", ".js", ".ps1", ".sh"
    };
    return blocked.count(file_extension(filename)) > 0;
}

long long current_epoch_millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

std::string bytes_to_hex(const unsigned char* bytes, std::size_t len) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        out << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return out.str();
}

std::string sha256_hex(const std::string& value) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(
        reinterpret_cast<const unsigned char*>(value.data()),
        value.size(),
        hash);
    return bytes_to_hex(hash, SHA256_DIGEST_LENGTH);
}

std::vector<unsigned char> hmac_sha256(
    const std::vector<unsigned char>& key,
    const std::string& value) {

    unsigned int len = 0;
    unsigned char hash[EVP_MAX_MD_SIZE];
    HMAC(
        EVP_sha256(),
        key.data(),
        static_cast<int>(key.size()),
        reinterpret_cast<const unsigned char*>(value.data()),
        value.size(),
        hash,
        &len);
    return std::vector<unsigned char>(hash, hash + len);
}

std::vector<unsigned char> hmac_sha256(
    const std::string& key,
    const std::string& value) {

    return hmac_sha256(
        std::vector<unsigned char>(key.begin(), key.end()),
        value);
}

std::string hmac_sha256_hex(
    const std::vector<unsigned char>& key,
    const std::string& value) {

    const auto hash = hmac_sha256(key, value);
    return bytes_to_hex(hash.data(), hash.size());
}

bool is_unreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string url_encode(const std::string& value, bool preserve_slash) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');

    for (unsigned char c : value) {
        if (is_unreserved(c) || (preserve_slash && c == '/')) {
            out << static_cast<char>(c);
        } else {
            out << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return out.str();
}

std::string extract_host(const std::string& endpoint) {
    std::size_t start = endpoint.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;

    std::size_t end = endpoint.find('/', start);
    if (end == std::string::npos) {
        return endpoint.substr(start);
    }
    return endpoint.substr(start, end - start);
}

std::string utc_time_string(const char* format) {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), format, &tm);
    return buf;
}

std::string build_canonical_query(const std::map<std::string, std::string>& params) {
    std::string query;
    for (const auto& [key, value] : params) {
        if (!query.empty()) {
            query += "&";
        }
        query += url_encode(key, false) + "=" + url_encode(value, false);
    }
    return query;
}

std::string create_presigned_url(
    const std::string& method,
    const std::string& public_endpoint,
    const std::string& bucket,
    const std::string& object_key,
    int expires_seconds) {

    const std::string access_key = get_env_or_default("MINIO_ACCESS_KEY", "minioadmin");
    const std::string secret_key = get_env_or_default("MINIO_SECRET_KEY", "minioadmin");
    const std::string region = get_env_or_default("MINIO_REGION", "us-east-1");
    const std::string service = "s3";

    const std::string amz_date = utc_time_string("%Y%m%dT%H%M%SZ");
    const std::string date_scope = utc_time_string("%Y%m%d");
    const std::string credential_scope =
        date_scope + "/" + region + "/" + service + "/aws4_request";
    const std::string credential = access_key + "/" + credential_scope;
    const std::string host = extract_host(public_endpoint);
    const std::string canonical_uri =
        "/" + bucket + "/" + url_encode(object_key, true);

    std::map<std::string, std::string> query_params = {
        {"X-Amz-Algorithm", "AWS4-HMAC-SHA256"},
        {"X-Amz-Credential", credential},
        {"X-Amz-Date", amz_date},
        {"X-Amz-Expires", std::to_string(expires_seconds)},
        {"X-Amz-SignedHeaders", "host"},
    };

    const std::string canonical_query = build_canonical_query(query_params);
    const std::string canonical_headers = "host:" + host + "\n";
    const std::string signed_headers = "host";
    const std::string payload_hash = "UNSIGNED-PAYLOAD";

    const std::string canonical_request =
        method + "\n" +
        canonical_uri + "\n" +
        canonical_query + "\n" +
        canonical_headers + "\n" +
        signed_headers + "\n" +
        payload_hash;

    const std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" +
        amz_date + "\n" +
        credential_scope + "\n" +
        sha256_hex(canonical_request);

    const auto date_key = hmac_sha256("AWS4" + secret_key, date_scope);
    const auto region_key = hmac_sha256(date_key, region);
    const auto service_key = hmac_sha256(region_key, service);
    const auto signing_key = hmac_sha256(service_key, "aws4_request");
    const std::string signature = hmac_sha256_hex(signing_key, string_to_sign);

    return public_endpoint + canonical_uri + "?" +
        canonical_query + "&X-Amz-Signature=" + signature;
}

std::string extract_object_key_from_public_url(
    const std::string& public_url,
    const std::string& public_endpoint,
    const std::string& bucket) {

    const std::string prefix = public_endpoint + "/" + bucket + "/";
    if (public_url.rfind(prefix, 0) != 0) {
        return "";
    }
    return public_url.substr(prefix.size());
}

bool execute_presigned_delete(const std::string& delete_url, std::string& error) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "failed to initialize curl";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, delete_url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    CURLcode result = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = curl_easy_strerror(result);
        return false;
    }

    if (status_code != 200 && status_code != 204) {
        error = "MinIO delete failed with status " + std::to_string(status_code);
        return false;
    }

    return true;
}

} // namespace

storage_json StorageService::create_upload_url(
    int user_id,
    const std::string& filename,
    const std::string& content_type) {

    storage_json res;

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

    if (has_path_traversal(filename)) {
        res["error"] = "invalid filename";
        return res;
    }

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

    res["upload_url"] = create_presigned_url(
        "PUT",
        public_endpoint,
        bucket,
        object_key,
        expires_seconds);
    res["public_url"] = public_url;
    res["object_key"] = object_key;
    res["bucket"] = bucket;
    res["content_type"] = content_type;
    res["expires_in"] = expires_seconds;

    return res;
}


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

    res["download_url"] = create_presigned_url(
        "GET",
        public_endpoint,
        bucket,
        object_key,
        expires_seconds);
    res["public_url"] = public_url;
    res["object_key"] = object_key;
    res["bucket"] = bucket;
    res["expires_in"] = expires_seconds;

    return res;
}


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

    const std::string delete_url = create_presigned_url(
        "DELETE",
        internal_endpoint,
        bucket,
        object_key,
        60);

    return execute_presigned_delete(delete_url, error);
}
