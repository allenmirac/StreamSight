
#ifndef STREAMSIGHT_NET_PIPE_H
#define STREAMSIGHT_NET_PIPE_H

#include "TcpSocket.h"

namespace streamsight::net {
	
class Pipe
{
public:
	Pipe();
	virtual ~Pipe();
	bool  Create();
	int   Write(void *buf, int len);
	int   Read(void *buf, int len);
	void  Close();

	SOCKET Read() const { return pipe_fd_[0]; }
	SOCKET Write() const { return pipe_fd_[1]; }
	
private:
	SOCKET pipe_fd_[2];
};

}  // namespace streamsight::net

#endif
