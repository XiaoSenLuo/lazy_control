
#ifndef _HTTP_DOWNLOAD_H_
#define _HTTP_DOWNLOAD_H_


#include <stdint.h>



int http_download_file(const char *host, const int port, const char *file_path, const char *des_path);

int http_download_file_by_url(const char *url, const char *des_path);


#endif