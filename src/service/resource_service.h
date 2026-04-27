#ifndef RESOURCE_SERVICE_H
#define RESOURCE_SERVICE_H

#include <nlohmann/json.hpp>
#include <string>

using res_json = nlohmann::ordered_json;

class ResourceService {
public:
        static res_json create_resource(const std::string& sql);
        static res_json get_resources(int user_id);
        static res_json get_resource(int user_id, int id);
        static res_json get_file_download_url(int user_id, int id);
        static res_json update_resource(const std::string& sql);
        static res_json delete_resource(int user_id, int id);
};

#endif
