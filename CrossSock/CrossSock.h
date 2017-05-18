

/**********************************************************************************************************
*  AUTHOR: Brandon Wilson  ********************************************************************************
*  A type-safe cross-platform header-only lightweight socket library developed on top of berkely sockets  *
**********************************************************************************************************/


#ifndef CROSS_SOCK
#define CROSS_SOCK


/* 
 * The low level socket API - includes an address class, TCP and
 * UDP socket classes, as well as a static utility class for
 * socket factory functions, as well as namespace resolution 
 * functions and an Select function implementation.
 *
 * This code was initially taken from the book 'Multiplayer Game
 * Programming' by Joshua Glazer and Sanjay Madhav. Although it has
 * undergone renaming and modifications, the initial inspiration is
 * still there. Please check out their book for a in-depth
 * explanation of the concepts in this file.
 */


 // The major and minor version of CrossSock. (Version = Major.Minor)
#define CROSSSOCK_VERSION_MAJOR 1
#define CROSSSOCK_VERSION_MINOR 0


/* Includes and defines */
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <stdio.h>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include "Windows.h"
	#pragma comment(lib, "Ws2_32.lib")
	#include "WinSock2.h"
	#include "Ws2tcpip.h"
	#include "stdint.h"
	typedef int socklen_t;
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <sys/types.h>
	#include <netdb.h>
	#include <errno.h>
	#include <fcntl.h>
	#include <unistd.h>
	typedef int SOCKET;
	const int NO_ERROR = 0;
	const int INVALID_SOCKET = -1;
	const int WSAECONNRESET = ECONNRESET;
	const int WSAEWOULDBLOCK = EAGAIN;
	const int SOCKET_ERROR = -1;
	const int WSAEISCONN = EISCONN;
	const int WSAEALREADY = EALREADY;
	const int WSAEINPROGRESS = EINPROGRESS;
#endif

#include "CrossUtil.h"

/* Socket Addressing */
	namespace CrossSock {
		enum CrossSockAddressFamily
		{
			INET = AF_INET,
			INET6 = AF_INET6
		};

		class CrossSockAddress
		{
		public:
			CrossSockAddress(uint32_t inAddress, uint16_t inPort, CrossSockAddressFamily inAddressFamily = CrossSockAddressFamily::INET)
			{
				memset(GetAsSockAddrIn()->sin_zero, 0, sizeof(GetAsSockAddrIn()->sin_zero));
				GetAsSockAddrIn()->sin_family = inAddressFamily;
				GetIP4Ref() = htonl(inAddress);
				GetAsSockAddrIn()->sin_port = htons(inPort);
			}

			CrossSockAddress(uint8_t inB1, uint8_t inB2, uint8_t inB3, uint8_t inB4, uint16_t inPort, CrossSockAddressFamily inAddressFamily = CrossSockAddressFamily::INET)
			{
				memset(GetAsSockAddrIn()->sin_zero, 0, sizeof(GetAsSockAddrIn()->sin_zero));
				GetAsSockAddrIn()->sin_family = inAddressFamily;
				GetAsSockAddrIn()->sin_port = htons(inPort);
				uint32_t address = 0;
				address = CrossSysUtil::SetStateToFlag<uint32_t>(address, inB1, 24, 31);
				address = CrossSysUtil::SetStateToFlag<uint32_t>(address, inB2, 16, 23);
				address = CrossSysUtil::SetStateToFlag<uint32_t>(address, inB3, 8, 15);
				address = CrossSysUtil::SetStateToFlag<uint32_t>(address, inB4, 0, 7);
				GetIP4Ref() = htonl(address);
			}

			CrossSockAddress(const sockaddr& inSockAddr)
			{
				memcpy(&mSockAddr, &inSockAddr, sizeof(sockaddr));
			}

			CrossSockAddress()
			{
				memset(GetAsSockAddrIn()->sin_zero, 0, sizeof(GetAsSockAddrIn()->sin_zero));
				GetAsSockAddrIn()->sin_family = AF_INET;
				GetIP4Ref() = INADDR_ANY;
				GetAsSockAddrIn()->sin_port = 0;
			}

			bool operator==(const CrossSockAddress& inOther) const
			{
				return (mSockAddr.sa_family == AF_INET &&
					GetAsSockAddrIn()->sin_port == inOther.GetAsSockAddrIn()->sin_port) &&
					(GetIP4Ref() == inOther.GetIP4Ref());
			}

			size_t Hash() const
			{
				return (GetIP4Ref()) |
					((static_cast< uint32_t >(GetAsSockAddrIn()->sin_port)) << 13) |
					mSockAddr.sa_family;
			}

			uint16_t GetPort() const
			{
				return ntohs(GetAsSockAddrIn()->sin_port);
			}

			uint8_t GetB1() const
			{
				return CrossSysUtil::GetStateFromFlag(GetAddress(), 24, 31);
			}

			uint8_t GetB2() const
			{
				return CrossSysUtil::GetStateFromFlag(GetAddress(), 16, 23);
			}

			uint8_t GetB3() const
			{
				return CrossSysUtil::GetStateFromFlag(GetAddress(), 8, 15);
			}

			uint8_t GetB4() const
			{
				return CrossSysUtil::GetStateFromFlag(GetAddress(), 0, 7);
			}

			int GetFamily() const
			{
				return GetAsSockAddrIn()->sin_family;
			}

			uint32_t GetAddress() const
			{
				return ntohl(GetIP4Ref());
			}

			uint32_t				GetSize()			const { return sizeof(sockaddr); }

			std::string ToString() const
			{
				std::ostringstream stream;
				stream << (int)GetB1() << "." << (int)GetB2() << "." << (int)GetB3() << "." << (int)GetB4() << ":" << (int)GetPort();
				return stream.str();
			}

			operator std::string() const { return ToString(); }
			operator const char*() const { return ToString().c_str(); }

			const static uint32_t ANY_ADDRESS = INADDR_ANY;

		private:
			friend class UDPSocket;
			friend class TCPSocket;

			sockaddr mSockAddr;

#if _WIN32
			uint32_t&				GetIP4Ref() { return *reinterpret_cast< uint32_t* >(&GetAsSockAddrIn()->sin_addr.S_un.S_addr); }
			const uint32_t&			GetIP4Ref()	  const { return *reinterpret_cast< const uint32_t* >(&GetAsSockAddrIn()->sin_addr.S_un.S_addr); }
#else
			uint32_t&				GetIP4Ref() { return GetAsSockAddrIn()->sin_addr.s_addr; }
			const uint32_t&			GetIP4Ref()   const { return GetAsSockAddrIn()->sin_addr.s_addr; }
#endif

			sockaddr_in*			GetAsSockAddrIn() { return reinterpret_cast< sockaddr_in* >(&mSockAddr); }
			const	sockaddr_in*	GetAsSockAddrIn()	const { return reinterpret_cast< const sockaddr_in* >(&mSockAddr); }

		};
	}

	namespace std
	{
		template<> struct hash< CrossSock::CrossSockAddress >
		{
			size_t operator()(const CrossSock::CrossSockAddress& inAddress) const
			{
				return inAddress.Hash();
			}
		};
	}

/* 
 * Cross Sock error handling - return codes are used throughout cross sock instead
 * of exception handling. This is by design, as errors can be common in CrossSock
 * and often are ignored. With most exception implementations, this would cause
 * significant overhead when compared to returning a value.
 */
	namespace CrossSock {
		enum CrossSockError
		{
			/* Action Succesful */
			SUCCESS = NO_ERROR,

			/* Generic Socket Error */
			INVALID = SOCKET_ERROR,

			/* The connection has been reset/terminated */
			CONNRESET = WSAECONNRESET,

			/* The action needs more time to complete and must be called again */
			WOULDBLOCK = WSAEWOULDBLOCK,

			/* The action is in progress */
			INPROGRESS = WSAEINPROGRESS,

			/* The socket is already connected */
			ISCONN = WSAEISCONN,

			/* This action has already completed succesfully */
			ALREADY = WSAEALREADY
		};
	}

/* UDP Sockets */
	namespace CrossSock {
		class UDPSocket
		{
		public:

			~UDPSocket()
			{
				Close();
			}

			/* Bind this socket to an address - allows sending and receiving data from/to this address */
			int Bind(const CrossSockAddress& inToAddress)
			{
				return bind(mSocket, &inToAddress.mSockAddr, inToAddress.GetSize());
			}

			/* Send data to an address - returns the number of bytes sent*/
			int SendTo(const char* inToSend, int inLength, const CrossSockAddress& inToAddress)
			{
				return sendto(mSocket,
					inToSend,
					inLength,
					0, &inToAddress.mSockAddr, inToAddress.GetSize());
			}

			/* Receive data - returns the number of bytes received and the peer address*/
			int ReceiveFrom(char* inToReceive, int inMaxLength, CrossSockAddress& outFromAddress)
			{
				socklen_t fromLength = outFromAddress.GetSize();

				return recvfrom(mSocket,
					inToReceive,
					inMaxLength,
					0, &outFromAddress.mSockAddr, &fromLength);
			}

			/* Sets this socket to blocking or non-blocking */
			int SetNonBlockingMode(bool inShouldBeNonBlocking)
			{
#if _WIN32
				u_long arg = inShouldBeNonBlocking ? 1 : 0;
				int result = ioctlsocket(mSocket, FIONBIO, (unsigned long *)&arg);
#else
				int flags = fcntl(mSocket, F_GETFL, 0);
				flags = inShouldBeNonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
				int result = fcntl(mSocket, F_SETFL, flags);
#endif
				return result;
			}

			/*Gets the local address associated with this socket */
			CrossSockAddress GetLocalAddress() const
			{
				CrossSockAddress addr;
				socklen_t length = sizeof(addr.mSockAddr);
				getsockname(mSocket, &addr.mSockAddr, &length);
				return addr;
			}

			/* Gets the remote address associated with this socket */
			CrossSockAddress GetPeerAddress() const
			{
				CrossSockAddress addr;
				socklen_t length = sizeof(addr.mSockAddr);
				getpeername(mSocket, &addr.mSockAddr, &length);
				return addr;
			}

			/* Close this socket */
			int Close()
			{
#if _WIN32
				return closesocket(mSocket);
#else
				return close(mSocket);
#endif
			}

		private:
			friend class CrossSockUtil;
			UDPSocket(SOCKET inSocket) : mSocket(inSocket) {}
			SOCKET mSocket;

		};

		typedef std::shared_ptr<CrossSock::UDPSocket> UDPSocketPtr;
	}

/* TCP Sockets */
	namespace CrossSock {
		class TCPSocket
		{

		public:

			~TCPSocket()
			{
				Close();
			}

			/* Connect to a remote client with the given address */
			int Connect(const CrossSockAddress& inAddress)
			{
				return connect(mSocket, &inAddress.mSockAddr, inAddress.GetSize());
			}

			/* Binds this socket to the given address */
			int	Bind(const CrossSockAddress& inToAddress)
			{
				return bind(mSocket, &inToAddress.mSockAddr, inToAddress.GetSize());
			}

			/* Listens for any clients attempting to connect - Should be used with Accept */
			int	Listen(int inBackLog = 32)
			{
				return listen(mSocket, inBackLog);
			}

			/* Accepts any remote connection attemps, returns the TCP socket and it's peer address */
			std::shared_ptr<TCPSocket> Accept(CrossSockAddress& outFromAddress)
			{
				socklen_t length = outFromAddress.GetSize();
				SOCKET newSocket = accept(mSocket, &outFromAddress.mSockAddr, &length);

				if (newSocket != INVALID_SOCKET)
				{
					return std::shared_ptr<TCPSocket>(new TCPSocket(newSocket));
				}
				else
				{
					return nullptr;
				}
			}

			/* Send data to the peer - returns the number of bytes sent */
			int	Send(const char* inData, int inLen)
			{
				return send(mSocket, inData, inLen, 0);
			}

			/* Receive data from the peer - returns the number of bytes received */
			int	Receive(char* inBuffer, int inLen)
			{
				return recv(mSocket, inBuffer, inLen, 0);
			}

			/* Sets this socket to blocking or non-blocking */
			int SetNonBlockingMode(bool inShouldBeNonBlocking)
			{
#if _WIN32
				u_long arg = inShouldBeNonBlocking ? 1 : 0;
				int result = ioctlsocket(mSocket, FIONBIO, (unsigned long *)&arg);
#else
				int flags = fcntl(mSocket, F_GETFL, 0);
				flags = inShouldBeNonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
				int result = fcntl(mSocket, F_SETFL, flags);
#endif
				return result;
			}

			/* Gets the local address associated with this socket */
			CrossSockAddress GetLocalAddress() const
			{
				CrossSockAddress addr;
				socklen_t length = sizeof(addr.mSockAddr);
				getsockname(mSocket, &addr.mSockAddr, &length);
				return addr;
			}

			/* Gets the remote address associated with this socket */
			CrossSockAddress GetPeerAddress() const
			{
				CrossSockAddress addr;
				socklen_t length = sizeof(addr.mSockAddr);
				getpeername(mSocket, &addr.mSockAddr, &length);
				return addr;
			}
			/* Close this socket, terminating the connection on both sides */
			int Close()
			{
#if _WIN32
				return closesocket(mSocket);
#else
				return close(mSocket);
#endif
			}

		private:
			friend class CrossSockUtil;
			TCPSocket(SOCKET inSocket) : mSocket(inSocket) {}
			SOCKET		mSocket;
		};

		typedef std::shared_ptr<CrossSock::TCPSocket> TCPSocketPtr;
	}

/* CrossSockUtil Class */
	namespace CrossSock {
		class CrossSockUtil
		{
		private:

			static fd_set* FillSetFromVector(fd_set& outSet, const std::vector< TCPSocketPtr >* inSockets, int& ioNaxNfds)
			{
				if (inSockets)
				{
					FD_ZERO(&outSet);

					for (const TCPSocketPtr& socket : *inSockets)
					{
						FD_SET(socket->mSocket, &outSet);
#if !_WIN32
						ioNaxNfds = std::max(ioNaxNfds, socket->mSocket);
#endif
					}
					return &outSet;
				}
				else
				{
					return nullptr;
				}
			}

			static void FillVectorFromSet(std::vector< TCPSocketPtr >* outSockets, const std::vector< TCPSocketPtr >* inSockets, const fd_set& inSet)
			{
				if (inSockets && outSockets)
				{
					outSockets->clear();
					for (const TCPSocketPtr& socket : *inSockets)
					{
						if (FD_ISSET(socket->mSocket, &inSet))
						{
							outSockets->push_back(socket);
						}
					}
				}
			}

		public:

			/* Mandatory static initialization for windows sockets - can be called safely on all systems */
			static bool Init()
			{
#if _WIN32
				WSADATA wsaData;
				int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
				if (iResult != NO_ERROR)
				{
					return false;
				}
#endif
				return true;
			}

			/* Mandatory static deinitialization for windows sockets - can be called safely on all systems for a no-op */
			static void CleanUp()
			{
#if _WIN32
				WSACleanup();
#endif
			}

			/* Utility function for accessing select-style IO handling */
			static int Select(const std::vector< TCPSocketPtr >* inReadSet,
				std::vector< TCPSocketPtr >* outReadSet,
				const std::vector< TCPSocketPtr >* inWriteSet,
				std::vector< TCPSocketPtr >* outWriteSet,
				const std::vector< TCPSocketPtr >* inExceptSet,
				std::vector< TCPSocketPtr >* outExceptSet)
			{
				//build up some sets from our vectors
				fd_set read, write, except;

				int nfds = 0;

				fd_set *readPtr = FillSetFromVector(read, inReadSet, nfds);
				fd_set *writePtr = FillSetFromVector(write, inWriteSet, nfds);
				fd_set *exceptPtr = FillSetFromVector(except, inExceptSet, nfds);

				int toRet = select(nfds + 1, readPtr, writePtr, exceptPtr, nullptr);

				if (toRet > 0)
				{
					FillVectorFromSet(outReadSet, inReadSet, read);
					FillVectorFromSet(outWriteSet, inWriteSet, write);
					FillVectorFromSet(outExceptSet, inExceptSet, except);
				}
				return toRet;
			}

			/* Creates a UDP socket for the given address family */
			static UDPSocketPtr CreateUDPSocket(CrossSockAddressFamily inFamily)
			{
				SOCKET s = socket(inFamily, SOCK_DGRAM, IPPROTO_UDP);

				if (s != INVALID_SOCKET)
				{
					return std::shared_ptr<UDPSocket>(new UDPSocket(s));
				}
				else
				{
					return nullptr;
				}
			}

			/* Creates a TCP socket for the given address family */
			static TCPSocketPtr CreateTCPSocket(CrossSockAddressFamily inFamily)
			{
				SOCKET s = socket(inFamily, SOCK_STREAM, IPPROTO_TCP);

				if (s != INVALID_SOCKET)
				{
					return std::shared_ptr<TCPSocket>(new TCPSocket(s));
				}
				else
				{
					return nullptr;
				}
			}

			/* Gets the most recent cross sock error code */
			static int GetLastError()
			{
#if _WIN32
				return WSAGetLastError();
#else
				return errno;
#endif
			}

			/* Returns an IPv4 address from the given string in the form of "b1.b2.b3.b4:port". If no port is given, 0 is used. */
			static CrossSockAddress* CreateIPv4FromString(const std::string& inString)
			{
				auto pos = inString.find_last_of(':');
				std::string host, service;
				if (pos != std::string::npos)
				{
					host = inString.substr(0, pos);
					service = inString.substr(pos + 1);
				}
				else
				{
					host = inString;
					service = "0";
				}
				addrinfo hint;
				memset(&hint, 0, sizeof(hint));
				hint.ai_family = AF_INET;

				addrinfo* result;
				int error = getaddrinfo(host.c_str(), service.c_str(), &hint, &result);
				if (error != 0 && result != nullptr)
				{
					return nullptr;
				}

				while (!result->ai_addr && result->ai_next)
				{
					result = result->ai_next;
				}

				if (!result->ai_addr)
				{
					return nullptr;
				}

				auto toRet = *result->ai_addr;

				freeaddrinfo(result);

				return new CrossSockAddress(toRet);
			}

			/* Returns the local host name of this system */
			static std::string GetHostName() 
			{
				struct addrinfo hints, *info, *p;
				int gai_result;

				char hostname[MAX_NAME_SIZE];
				hostname[MAX_NAME_SIZE - 1] = '\0';
				gethostname(hostname, MAX_NAME_SIZE - 1);

				memset(&hints, 0, sizeof hints);
				hints.ai_family = AF_UNSPEC; /* either IPV4 or IPV6 */
				hints.ai_socktype = SOCK_STREAM;
				hints.ai_flags = AI_CANONNAME;

				if ((gai_result = getaddrinfo(hostname, "http", &hints, &info)) != 0) {
					return "";
				}

				std::string outStr = "";

				for (p = info; p != NULL; p = p->ai_next) {
					if(p->ai_canonname)
					outStr += p->ai_canonname;
				}

				freeaddrinfo(info);

				return outStr;
			}

			/* Returns the local host address of this system */
			static CrossSockAddress* GetHostAddress()
			{
				return CreateIPv4FromString(GetHostName());
			}

			static const int MAX_NAME_SIZE = 1024;
		};
	}

#endif