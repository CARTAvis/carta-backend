#include <iostream>
#include <string>

#include "util.h"
#include "build/carta-protobuf/defs.pb.h"
#include "build/carta-protobuf/register_viewer.pb.h"


typedef struct {
  uint8_t _type; 
  uint8_t _icd_vers;
  uint16_t _req_id;
} msg_header;


const uint8_t test_TYPE= 1;
const uint8_t ICD_VERSION= 2;

void
recv_event( char * buff, int length )
{
  msg_header head= *reinterpret_cast<msg_header*>(buff);

  std::printf("type= %d, icd vers= %d, reqid= %d\n",
	      head._type, head._icd_vers, head._req_id );
  
  switch( head._type ) {
  case test_TYPE: {
    CARTA::RegisterViewerAck Message;
    Message.ParseFromArray(buff + sizeof(msg_header), length);
    std::cout << " Got RVack for uuid " << Message.session_id() << std::endl;
    break;
    }
  default:
    std::cerr << " Bad message type : " << head._type << std::endl;
    exit(1);
  }
}


void
send_event( uint8_t evt_type,
	    uint32_t event_id,
	    google::protobuf::MessageLite& Message )
{
  msg_header head;
  char buffer[128]; // Would have buffer pool in production system.
  
  head._type= evt_type;
  head._icd_vers= ICD_VERSION;
  head._req_id= event_id;

  std::memcpy(buffer, &head, sizeof(msg_header));
  Message.SerializeToArray(buffer + sizeof(msg_header), Message.ByteSize());
  
  // Would add to the send queue here in real version, but just pass it straight
  // to receive for this test.
  recv_event(buffer, Message.ByteSize());
}

int main(int argc, const char* argv[])
{
  uint32_t req_id= 1002;
  std::string message("Error string ...");
  std::string uuid("123882"); // Should be uint32_t, but need to update protobuf def
  ::CARTA::SessionType type(CARTA::SessionType::NEW);
  
  CARTA::RegisterViewerAck ackMessage;

  ackMessage.set_session_id(uuid);
  ackMessage.set_success(false);
  ackMessage.set_message(message);
  ackMessage.set_session_type(type);
  
  std::cout << " message type : " << ackMessage.GetTypeName() << std::endl;

  send_event(test_TYPE, req_id, ackMessage);
  
  return 0;
}
  
