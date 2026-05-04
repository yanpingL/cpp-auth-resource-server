#include "resource_service.h"
#include "dao/resource_dao.h"
#include "dao/user_dao.h"
#include "service/storage_service.h"

res_json ResourceService::create_resource(const std::string& sql){

     bool ok = ResourceDAO::create_resource(sql);
     res_json res;

     if (!ok){
          res["error"] = ResourceDAO::msg;
     } else {
          res["status"] = "created";
     }
     return res;
}


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


res_json ResourceService::update_resource(const std::string& sql){
     bool ok = ResourceDAO::update_resource(sql);

     res_json res;
     if (!ok){
          res["error"] = ResourceDAO::msg;
     } else {
          res["status"] = "updated";
     }
     return res;
}


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
