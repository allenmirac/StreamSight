#ifndef STREAMSIGHT_RTSP_H264_PARSER_H
#define STREAMSIGHT_RTSP_H264_PARSER_H

#include <cstdint> 
#include <utility> 

namespace streamsight::rtsp {

typedef std::pair<uint8_t*, uint8_t*> Nal; // <nal begin, nal end>

class H264Parser
{
public:    
    static Nal findNal(const uint8_t *data, uint32_t size);
        
private:
  
};
    
}  // namespace streamsight::rtsp

#endif 

