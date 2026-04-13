#ifndef RESORUCE_DAO_H
#define RESORUCE_DAO_H
#include <string>
#include <vector>

struct Resource {
    int id;
    std::string title;
    std::string content;
};

class ResourceDAO {
public:
    static std::string msg;

    static bool create_resource(const std::string& sql);
    static std::vector<Resource> get_resources(int user_id);
    static bool update_resource(const std::string& sql);
    static bool delete_resource(int user_id, int id);
};

#endif