//PHZ
//2019-10-6

#ifndef STREAMSIGHT_RTSP_DIGEST_AUTHENTICATION_H
#define STREAMSIGHT_RTSP_DIGEST_AUTHENTICATION_H

#include <cstdint>
#include <string>

namespace streamsight::rtsp {

class DigestAuthentication
{
public:
	DigestAuthentication(std::string realm, std::string username, std::string password);
	virtual ~DigestAuthentication();

	std::string GetRealm() const
	{ return realm_; }

	std::string GetUsername() const
	{ return username_; }

	std::string GetPassword() const
	{ return password_; }

	std::string GetNonce();
	std::string GetResponse(std::string nonce, std::string cmd, std::string url);

private:
	std::string realm_;
	std::string username_;
	std::string password_;

};

}  // namespace streamsight::rtsp

#endif
