#ifndef HTTP_SERVICE_H_
#define HTTP_SERVICE_H_

#include <string.h>
#include <string>
#include <map>
#include <list>
#include <memory>

#include "HttpMessage.h"

#define DEFAULT_BASE_URL        "/v1/api"
#define DEFAULT_DOCUMENT_ROOT   "/var/www/html"
#define DEFAULT_HOME_PAGE       "index.html"

#define HANDLE_CONTINUE 0
#define HANDLE_DONE     1
typedef int (*http_api_handler)(HttpRequest* req, HttpResponse* res);

struct http_method_handler {
    http_method         method;
    http_api_handler    handler;
    http_method_handler(http_method m = HTTP_POST, http_api_handler h = NULL) {
        method = m;
        handler = h;
    }
};
// method => http_api_handler
typedef std::list<http_method_handler> http_method_handlers;
// path => http_method_handlers
typedef std::map<std::string, std::shared_ptr<http_method_handlers>> http_api_handlers;

struct HttpService {
    // preprocessor -> api -> web -> postprocessor
    http_api_handler    preprocessor;
    http_api_handler    postprocessor;
    // api service
    std::string         base_url;
    http_api_handlers   api_handlers;
    // web service
    std::string document_root;
    std::string home_page;
    std::string error_page;
    std::string index_of;

    HttpService() {
        preprocessor = NULL;
        postprocessor = NULL;
        base_url = DEFAULT_BASE_URL;
        document_root = DEFAULT_DOCUMENT_ROOT;
        home_page = DEFAULT_HOME_PAGE;
    }

    void AddApi(const char* path, http_method method, http_api_handler handler);
    // @retval 0 OK, else HTTP_STATUS_NOT_FOUND, HTTP_STATUS_METHOD_NOT_ALLOWED
    int GetApi(const char* url, http_method method, http_api_handler* handler);
    // RESTful API /:field/ => req->query_params["field"]
    int GetApi(HttpRequest* req, http_api_handler* handler);
};

#endif // HTTP_SERVICE_H_
