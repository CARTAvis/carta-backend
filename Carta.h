#ifndef __CARTA_H__
#define __CARTA_H__

namespace CARTA {
  extern int global_thread_count;
  extern std::string token;
  extern std::string mongo_db_contact_string;
  
  const int MAX_TILING_TASKS = 8;
} // namespace CARTA

#endif // __CARTA_H__
