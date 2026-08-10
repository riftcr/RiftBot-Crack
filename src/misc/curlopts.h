#pragma once
#include <curl/curl.h>

std::string_view CurlOptToString(CURLoption opt);
std::string_view CurlInfoToString(CURLINFO info);
