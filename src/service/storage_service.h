#ifndef STORAGE_SERVICE_H
#define STORAGE_SERVICE_H

#include <nlohmann/json.hpp>
#include <string>

using storage_json = nlohmann::ordered_json;

class StorageService {
public:
    static storage_json create_upload_url(
        int user_id,
        const std::string& filename,
        const std::string& content_type);
    static storage_json create_download_url(const std::string& public_url);
    static bool delete_file(const std::string& public_url, std::string& error);
};

#endif
