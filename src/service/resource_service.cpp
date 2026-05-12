#include "resource_service.h"
#include "dao/resource_dao.h"
#include "dao/user_dao.h"
#include "service/storage_service.h"

// Creates a resource and returns a JSON status object.
res_json ResourceService::create_resource(const ResourceInfo& Info){
     Resource resource;
     resource.user_id = Info.user_id;
     resource.title = Info.title;
     resource.content = Info.content;
     resource.is_file = Info.is_file;

     bool ok = ResourceDAO::create_resource(resource);
     res_json res;

     if (!ok){
          res["error"] = ResourceDAO::msg;
     } else {
          res["status"] = "created";
     }
     return res;
}

// Returns all resources for a user as a JSON array.
res_json ResourceService::get_resources(int user_id){
     auto vec = ResourceDAO::get_resources(user_id);
     
     res_json res;
     if (!ResourceDAO::msg.empty()){
          res["error"] = ResourceDAO::msg;
          return res;
     }

     res["data"] = res_json::array();

     for (const auto& r : vec){
          res_json item;
          item["id"] = r.id;
          item["title"] = r.title;
          item["content"] = r.content;
          item["is_file"] = r.is_file;

          res["data"].push_back(item);
     }
     return res;
}

// Returns one resource as a JSON object.
res_json ResourceService::get_resource(int user_id, int id){
     auto resource = ResourceDAO::get_resource(user_id, id);

     res_json res;
     if (!resource.has_value()){
          res["error"] = ResourceDAO::msg;
          return res;
     }

     const auto& r = resource.value();
     res["id"] = r.id;
     res["title"] = r.title;
     res["content"] = r.content;
     res["is_file"] = r.is_file;

     return res;
}

// Creates a presigned download URL for a file resource.
res_json ResourceService::get_file_download_url(int user_id, int id){
     auto resource = ResourceDAO::get_resource(user_id, id);

     res_json res;
     if (!resource.has_value()){
          res["error"] = ResourceDAO::msg;
          return res;
     }

     const auto& r = resource.value();
     if (!r.is_file){
          res["error"] = "resource is not file";
          return res;
     }

     return StorageService::create_download_url(r.content);
}

// Updates a resource and returns a JSON status object.
res_json ResourceService::update_resource(const ResourceInfo& Info){
     Resource resource;
     resource.id = Info.id;
     resource.user_id = Info.user_id;
     resource.title = Info.title;
     resource.content = Info.content;

     bool ok = ResourceDAO::update_resource(resource);
     res_json res;
     if (!ok){
          res["error"] = ResourceDAO::msg;
     } else {
          res["status"] = "updated";
     }
     return res;
}

// Deletes a resource and removes the MinIO object when the resource is a file.
res_json ResourceService::delete_resource(int user_id, int id){
     auto resource = ResourceDAO::get_resource(user_id, id);
     if (!resource.has_value()) {
         res_json res;
         res["error"] = ResourceDAO::msg;
         return res;
     }

     // if the resource is file, we also need to delete the file stored in MinIO
     if (resource->is_file) {
         std::string error;
         if (!StorageService::delete_file(resource->content, error)) {
             res_json res;
             res["error"] = error;
             return res;
         }
     }

     bool ok = ResourceDAO::delete_resource(user_id, id);

     res_json res;
 
     if (!ok) {
         res["error"] = ResourceDAO::msg;
     } else {
         res["status"] = "deleted";
     }
     return res;  
}
