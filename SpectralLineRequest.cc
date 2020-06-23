#include <cstdlib>
#include <string>
#include <curl/curl.h>
 
#include "SpectralLineRequest.h"

using namespace carta;

const std::string SpectralLineRequest::SplatalogueURL = "https://www.cv.nrao.edu/php/splat/c_export.php?&sid%5B%5D=&data_version=v3.0&lill=on&displayJPL=displayJPL&displayCDMS=displayCDMS&displayLovas=displayLovas&displaySLAIM=displaySLAIM&displayToyaMA=displayToyaMA&displayOSU=displayOSU&displayRecomb=displayRecomb&displayLisa=displayLisa&displayRFI=displayRFI&ls1=ls1&ls2=ls2&ls3=ls3&ls4=ls4&ls5=ls5&el1=el1&el2=el2&el3=el3&el4=el4&show_unres_qn=show_unres_qn&submit=Export&export_type=current&export_delimiter=tab&offset=0&limit=100000&range=on";
 
size_t SpectralLineRequest::WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
 
  char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */ 
    std::cout << "not enough memory (realloc returned NULL)\n";
    return 0;
  }
 
  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
 
  return realsize;
}
 
void SpectralLineRequest::SendRequest(CARTA::DoubleBounds frequencyRange) {
  CURL *curl_handle;
  CURLcode res;
 
  struct MemoryStruct chunk;
 
  chunk.memory = (char *)malloc(1);  /* will be grown as needed by the realloc above */ 
  chunk.size = 0;    /* no data at this point */ 
 
  curl_global_init(CURL_GLOBAL_ALL);
 
  /* init the curl session */ 
  curl_handle = curl_easy_init();
 
  /* specify URL to get */
  std::string frequencyRangeStr = "&frequency_units=MHz&from=" + std::to_string(frequencyRange.min()) + "&to=" + std::to_string(frequencyRange.max());
  std::string URL = SpectralLineRequest::SplatalogueURL + frequencyRangeStr;
  curl_easy_setopt(curl_handle, CURLOPT_URL, URL.c_str());
 
  /* send all data to this function  */ 
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, SpectralLineRequest::WriteMemoryCallback);
 
  /* we pass our 'chunk' struct to the callback function */ 
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
 
  /* some servers don't like requests that are made without a user-agent
     field, so we provide one */ 
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
 
  /* get it! */ 
  res = curl_easy_perform(curl_handle);
 
  /* check for errors */ 
  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  }
  else {
    SpectralLineRequest::parsingQueryResult(chunk);
  }
 
  /* cleanup curl stuff */ 
  curl_easy_cleanup(curl_handle);
 
  free(chunk.memory);
 
  /* we're done with libcurl, so clean it up */ 
  curl_global_cleanup();
 
  return;
}

void SpectralLineRequest::parsingQueryResult(MemoryStruct& results) {
  std::cout << (unsigned long)results.size << "bytes retrieved\n\n";
}
