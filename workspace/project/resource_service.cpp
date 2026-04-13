#include "resource_service.h"
#include "resource_dao.h"
#include "user_dao.h"

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
     if (!vec.size()){
          res["error"] = ResourceDAO::msg;
          return res;
     }

     res["data"] = res_json::array();

     for (const auto& r : vec){
          res_json item;
          item["id"] = r.id;
          item["title"] = r.title;
          item["content"] = r.content;

          res["data"].push_back(item);
     }

     return res;
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
     bool ok = ResourceDAO::delete_resource(user_id, id);

     res_json res;
 
     if (!ok) {
         res["error"] = UserDAO::msg;
     } else {
         res["status"] = "deleted";
     }
     return res;  
}



