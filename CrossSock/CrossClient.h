

/**********************************************************************************************************
*  AUTHOR: Brandon Wilson  ********************************************************************************
*  A type-safe cross-platform header-only lightweight socket library developed on top of berkely sockets  *
**********************************************************************************************************/


#ifndef CROSS_SOCK_CLIENT
#define CROSS_SOCK_CLIENT


/*
 * The high-level client implementation.  See CrossClientDemo.cpp for
 * general usage, and CrossClientProperties for configuration. Note
 * that this class is implemented with the RAII standard, and so
 * objects should be created in the highest scope or initialized
 * dynamically using smart pointers (CrossClientPtr can be used for
 * convenience).
 */

#include "CrossSock.h"
#include "CrossPack.h"
#include <unordered_map>


/* declare hash functions */
namespace std
{
	template<> struct hash< CrossSock::CrossDataEvent<const CrossSock::CrossPack*, CrossSock::NetTransMethod>* >
	{
		size_t operator()(const CrossSock::CrossDataEvent<const CrossSock::CrossPack*, CrossSock::NetTransMethod>* inEvent) const
		{
			return inEvent->Hash();
		}
	};
}

namespace CrossSock {

	enum CrossClientState {
		/* Client has not yet connected to a server */
		CLIENT_NEEDS_TO_CONNECT = 0,

		/* Client is attempting to connect to the server */
		CLIENT_CONNECTING = 1,

		/* Client is waiting to receive its client ID from the server */
		CLIENT_RECEIVING_ID = 2,

		/* Client is exchanging its custom data list with the server */
		CLIENT_RECEIVING_DATA_LIST = 3,

		/* Client is attempting to reconnect to the server */
		CLIENT_RECONNECTING = 4,

		/* Client is requesting its old client ID from the server */
		CLIENT_REQUESTING_ID = 5,

		/* Client is connected to the server and ready to transmit custom data */
		CLIENT_CONNECTED = 6
	};

	/* Client properties list */
	struct CrossClientProperties {

		/*
		 * If this client should accept and be able to send connectionless packets
		 */
		bool allowUDPPackets;

		/*
		 * The maximum number of UDP transmits per update step
		 */
		int maxUDPTransmitsPerUpdate;

		/*
		 * The maximum number of UDP transmits per update step
		 */
		int maxTCPTransmitsPerUpdate;

		/*
		 * If this client should bother with trying to reconnect once disconnected
		 */
		bool shouldAttemptReconnect;

		/*
		* The maximum number of connection attempts before a Connect() call will fail
		*/
		int maxConnectionAttempts;

		/*
		 * The maximum number of reconnection attempts before the client moves to a
		 * disconnected state
		 */
		int maxReconnectionAttempts;

		/*
		 * The delay between connection / reconnection attempts
		 */
		double connectionDelay;

		/*
		 * How often (in ms) we should send aliveness tests to the server - the larger
		 * this is the more forgiveness towards network hickups. Likewise, the lower
		 * this is the more responsive a timeout will occur.
		 */
		double alivenessTestDelay;

		CrossClientProperties()
		{
			allowUDPPackets = true;
			maxUDPTransmitsPerUpdate = 256;
			maxTCPTransmitsPerUpdate = 4;
			shouldAttemptReconnect = true;
			maxConnectionAttempts = 50;
			maxReconnectionAttempts = 100;
			connectionDelay = 200.0;
			alivenessTestDelay = 1000.0;
		}
	};

	/* A high level client object */
	class CrossClient {
	private:

		void Init()
		{
			serverSocket = nullptr;
			streamSocket = nullptr;
			clientID = 0;
			streamIsBound = false;
			clientState = CrossClientState::CLIENT_NEEDS_TO_CONNECT;
			connectEvent = nullptr;
			readyEvent = nullptr;
			disconnectEvent = nullptr;
			attemptReconnectEvent = nullptr;
			reconnectEvent = nullptr;
			failedReconnectEvent = nullptr;
			handshakeEvent = nullptr;
			receiveEvent = nullptr;
			transErrorEvent = nullptr;
		}

	public:
		CrossClient()
		{
			Init();
		}

		CrossClient(CrossClientProperties inProperties)
		{
			Init();
			SetClientProperties(inProperties);
		}

		~CrossClient()
		{
			Disconnect();
			for (auto it = dataEvents.begin(); it != dataEvents.end(); ++it) {
				delete *it;
			}
			delete connectEvent;
			delete readyEvent;
			delete disconnectEvent;
			delete attemptReconnectEvent;
			delete reconnectEvent;
			delete failedReconnectEvent;
			delete handshakeEvent;
			delete receiveEvent;
			delete transErrorEvent;
		}

		/* Connect to a server using its address */
		void Connect(const CrossSockAddress& inAddress, const CrossSockAddressFamily& inFamily = CrossSockAddressFamily::INET)
		{
			serverAddress = inAddress;
			addressFamily = inFamily;
			connectionAttempts = 0;
			tcpBufferLength = 0;
			udpBufferLength = 0;
			ping = 0;
			clientState = CrossClientState::CLIENT_CONNECTING;
			Update();
		}

		/* Connect to a server using its address in string format {"b1.b2.b3.b4:port"} */
		void Connect(const std::string& inAddress, const CrossSockAddressFamily& inFamily = CrossSockAddressFamily::INET)
		{
			CrossSockAddress* addr = CrossSockUtil::CreateIPv4FromString(inAddress);
			Connect(*addr, inFamily);
			delete addr;
		}

		/* Connect to a server using its name in string format {"name.domain"} with port override */
		void Connect(const std::string& inAddress, const uint16_t& inPort, const CrossSockAddressFamily& inFamily = CrossSockAddressFamily::INET)
		{
			CrossSockAddress* nameAddress = CrossSockUtil::CreateIPv4FromString(inAddress);
			CrossSockAddress finalAddress(nameAddress->GetB1(),
				nameAddress->GetB2(),
				nameAddress->GetB3(),
				nameAddress->GetB4(),
				inPort,
				inFamily);
			Connect(finalAddress, inFamily);
			delete nameAddress;
		}

		/* Disconnect from the server */
		void Disconnect(const bool AttemptReconnect = false)
		{
			if (IsRunning()) {
				ResetDataEventIDs();
				if (serverSocket) {
					CrossPack pack;
					pack.SetDataID(StaticDataID::DISCONNECT_PACK);
					SendToServer(&pack);
					serverSocket->Close();
					serverSocket.reset();
				}
				if (streamSocket) {
					streamSocket->Close();
					streamSocket.reset();
				}
				streamIsBound = false;

				/* reconnect if possible */
				if (AttemptReconnect && clientProperties.shouldAttemptReconnect && IsReady()) {

					/* reset connection attempts counter */
					connectionAttempts = 0;

					/* reset buffers */
					tcpBufferLength = 0;
					udpBufferLength = 0;

					/* attempt to reconnect */
					clientState = CrossClientState::CLIENT_RECONNECTING;

					/* execute attempt reconnect event */
					if (attemptReconnectEvent && attemptReconnectEvent->IsValid())
						attemptReconnectEvent->Execute();
				}
				else {
					clientState = CrossClientState::CLIENT_NEEDS_TO_CONNECT;
					if (disconnectEvent && disconnectEvent->IsValid())
						disconnectEvent->Execute();
				}
			}
		}

		/* Automaitcally receive data and reconnect to the server */
		void Update()
		{
			/* try to connect or reconnect until the maximum attempts has been reached */
			if (clientState == CrossClientState::CLIENT_CONNECTING || clientState == CrossClientState::CLIENT_RECONNECTING) {

				/* check connection timer */
				double elapsedTime = connectionTimer.GetElapsedTime();
				if (connectionAttempts == 0 || elapsedTime >= clientProperties.connectionDelay) {
					/* declare error code */
					int err;

					/* if the socket is invalid, attempt to create a new one */
					if (!serverSocket) {
						serverSocket = CrossSockUtil::CreateTCPSocket(addressFamily);
						if (serverSocket) {
							serverSocket->SetNonBlockingMode(true);
						}
						else {
							err = CrossSockError::INVALID;
						}
					}

					/* if a valid socket, attempt to connect */
					if (serverSocket) {
						serverSocket->Connect(serverAddress);
						err = CrossSockUtil::GetLastError();
					}

					/* if succesful - continue connection process... */
					if (err == CrossSockError::SUCCESS || err == CrossSockError::ISCONN) {
						if (clientState == CrossClientState::CLIENT_RECONNECTING) {
							clientState = CrossClientState::CLIENT_REQUESTING_ID;
						}
						else {
							clientState = CrossClientState::CLIENT_RECEIVING_ID;
						}

						// reset aliveness and timeout timers
						alivenessTestTimer.SetToNow();
						timeoutTimer.SetToNow();
						timeoutDelay = CROSS_SOCK_MAX_TIMEOUT;
						ping = 0;
					}
					else { /* else has yet to connect - test if the client should disconnect */
						connectionAttempts++;
						connectionTimer.SetToNow();
						if (connectionAttempts >= (clientState == CrossClientState::CLIENT_CONNECTING ? clientProperties.maxConnectionAttempts : clientProperties.maxReconnectionAttempts)
							|| (err != CrossSockError::WOULDBLOCK && err != CrossSockError::ALREADY && err != CrossSockError::INPROGRESS)) {
							Disconnect();
						}
					}
				}
			}
			else if (clientState != CrossClientState::CLIENT_NEEDS_TO_CONNECT) {

				/* do aliveness test if possible */
				if (clientState != CrossClientState::CLIENT_RECONNECTING && alivenessTestTimer.GetElapsedTime() >= clientProperties.alivenessTestDelay) {
					alivenessTestTimer.SetToNow();
					CrossPack alivenessTest;
					alivenessTest.SetDataID(StaticDataID::ALIVENESS_TEST);
					alivenessTest.AddToPayload<float>((float)((clientProperties.alivenessTestDelay + ping) * CROSS_SOCK_TIMEOUT_FACTOR));

					if (SendToServer(&alivenessTest) < 0 || timeoutTimer.GetElapsedTime() >= timeoutDelay) {

						// disconnect and get out of this update iteration to avoid checking tcp / udp data
						Disconnect(true);
						return;
					}
				}

				/* handle TCP data */
				CrossBufferLen bytesReceived;
				int tcpTransmits = 0;
				do {
					tcpTransmits++;
					bytesReceived = serverSocket->Receive(tcpBuffer + tcpBufferLength, CROSS_SOCK_BUFFER_SIZE - tcpBufferLength);
					if (bytesReceived > 0) {
						tcpBufferLength += bytesReceived;
						CrossBufferLen dataUsed;
						CrossBufferLen bufferPos = 0;
						do {
							dataUsed = OnReceiveNewData(tcpBuffer + bufferPos, tcpBufferLength - bufferPos, NetTransMethod::TCP);
							bufferPos += dataUsed;
						} while (IsRunning() && dataUsed > 0 && bufferPos < tcpBufferLength);

						/* reset buffer to front */
						tcpBufferLength -= bufferPos;
						memcpy(tcpBuffer, tcpBuffer + bufferPos, tcpBufferLength);
					}
					else if (CrossSockUtil::GetLastError() == CrossSockError::CONNRESET) {
						Disconnect(true);
					}
				} while (IsRunning() && bytesReceived > 0 && tcpTransmits < clientProperties.maxTCPTransmitsPerUpdate);

				/* handle UDP data */
				if (clientProperties.allowUDPPackets && clientState == CrossClientState::CLIENT_CONNECTED) {
					if (streamIsBound) {
						int udpTransmits = 0;
						do {
							if (IsDisconnected())
								break;
							udpTransmits++;
							CrossSockAddress fromAddress;
							bytesReceived = streamSocket->ReceiveFrom(udpBuffer + udpBufferLength, CROSS_SOCK_BUFFER_SIZE - udpBufferLength, fromAddress);
							if (bytesReceived > 0 && fromAddress == serverAddress) {
								udpBufferLength += bytesReceived;
								CrossBufferLen dataUsed;
								CrossBufferLen bufferPos = 0;
								do {
									dataUsed = OnReceiveNewData(udpBuffer + bufferPos, udpBufferLength - bufferPos, NetTransMethod::UDP);
									bufferPos += dataUsed;
								} while (IsRunning() && streamIsBound && dataUsed > 0 && bufferPos < udpBufferLength);

								/* reset buffer to front */
								udpBufferLength -= bufferPos;
								memcpy(udpBuffer, udpBuffer + bufferPos, udpBufferLength);
							}
							else if (CrossSockUtil::GetLastError() == CrossSockError::CONNRESET) {
								streamIsBound = false;
								udpBufferLength = 0;
								if (streamSocket) {
									streamSocket->Close();
									streamSocket.reset();
								}
							}
						} while (IsRunning() && streamIsBound && bytesReceived > 0 && udpTransmits < clientProperties.maxUDPTransmitsPerUpdate);
					}
					else { /* bind UDP socket if needed */
						if (!streamSocket) {
							streamSocket = CrossSockUtil::CreateUDPSocket(addressFamily);
							if (streamSocket)
								streamSocket->SetNonBlockingMode(true);
						}
						if (streamSocket) {
							int result = streamSocket->Bind(GetLocalAddress());
							int err = CrossSockUtil::GetLastError();
							if (result >= 0 || (err == CrossSockError::SUCCESS || err == CrossSockError::ISCONN))
								streamIsBound = true;
						}
					}
				}
			}
		}

		/* Get this client's local address */
		CrossSockAddress GetLocalAddress() const
		{
			if (serverSocket)
				return serverSocket->GetLocalAddress();
			else {
				CrossSockAddress outAddr;
				return outAddr;
			}
		}

		/* Get the most recent server address */
		CrossSockAddress GetServerAddress() const
		{
			return serverAddress;
		}

		CrossSockAddressFamily GetAddressFamily() const
		{
			return addressFamily;
		}

		/* Get the TCP socket */
		TCPSocketPtr GetTCPSocket() const
		{
			return serverSocket;
		}

		/* Get the UDP socket */
		UDPSocketPtr GetUDPSocket() const
		{
			return streamSocket;
		}

		void SetClientProperties(CrossClientProperties inProperties)
		{
			// consider sending aliveness test if the aliveness test delay is changing
			if (IsRunning() && clientProperties.alivenessTestDelay != inProperties.alivenessTestDelay) {
				CrossPack alivenessTest;
				alivenessTest.SetDataID(StaticDataID::ALIVENESS_TEST);
				alivenessTest.AddToPayload<float>((float)((inProperties.alivenessTestDelay + ping) * CROSS_SOCK_TIMEOUT_FACTOR));
				SendToServer(&alivenessTest);
			}

			clientProperties = inProperties;
		}

		CrossClientProperties GetClientProperties() const
		{
			return clientProperties;
		}

		/* Get the client state*/
		CrossClientState GetClientState() const
		{
			return clientState;
		}

		/* Get the unique client ID */
		CrossClientID GetClientID() const
		{
			if (clientState == CrossClientState::CLIENT_CONNECTED ||
				clientState == CrossClientState::CLIENT_RECEIVING_DATA_LIST)
				return clientID;
			else
				return 0;
		}

		/* Send a packet reliably to the server - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful */
		int SendToServer(const CrossPackPtr inPack, const bool inShouldBlockUntilSent = true) const
		{
			return SendToServer(inPack.get(), inShouldBlockUntilSent);
		}

		/* Send a packet reliably to the server - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful */
		int SendToServer(const CrossPack* inPack, const bool inShouldBlockUntilSent = true) const
		{
			if (clientState == CrossClientState::CLIENT_NEEDS_TO_CONNECT || clientState == CrossClientState::CLIENT_CONNECTING || clientState == CrossClientState::CLIENT_RECONNECTING) {
				return NetTransError::CLIENT_NOT_CONNECTED;
			}
			else {
				/* send until succesful */
				int result;
				do {
					result = serverSocket->Send(inPack->Serialize(), inPack->GetPacketSize());
				} while (inShouldBlockUntilSent && result < 0 && CrossSockUtil::GetLastError() == CrossSockError::WOULDBLOCK);
				return result;
			}
		}

		/* Send a packet unreliably to the server - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful.  WARNING: Will finalize the packet automatically */
		int StreamToServer(const CrossPackPtr inPack, const bool inShouldBlockUntilSent = true) const
		{
			return StreamToServer(inPack.get(), inShouldBlockUntilSent);
		}

		/* Send a packet unreliably to the server - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful.  WARNING: Will finalize the packet automatically */
		int StreamToServer(const CrossPack* inPack, const bool inShouldBlockUntilSent = true) const
		{
			if (clientState == CrossClientState::CLIENT_NEEDS_TO_CONNECT || clientState == CrossClientState::CLIENT_CONNECTING || clientState == CrossClientState::CLIENT_RECONNECTING) {
				return NetTransError::CLIENT_NOT_CONNECTED;
			}
			else if (!streamIsBound)
				return NetTransError::STREAM_NOT_BOUND;
			else {

				/* finalize if necessary */
				if (!inPack->IsFinalized()) {
					inPack->Finalize(false, true, GetClientID());
				}

				/* send until succesful */
				int result;
				do {
					result = streamSocket->SendTo(inPack->Serialize(), inPack->GetPacketSize(), serverAddress);
				} while (inShouldBlockUntilSent && result < 0 && CrossSockUtil::GetLastError() == CrossSockError::WOULDBLOCK);
				return result;
			}
		}

		/* Returns true if the UDP socket has been bound - false otherwise */
		bool IsStreamBound() const
		{
			return streamIsBound;
		}

		/* Returns true if the client is not disconnected - false otherwise */
		bool IsRunning() const
		{
			return clientState != CrossClientState::CLIENT_NEEDS_TO_CONNECT;
		}

		/* Returns true if the client is connected to the server - false otherwise */
		bool IsConnected() const
		{
			return (IsRunning() && clientState != CrossClientState::CLIENT_CONNECTING && clientState != CrossClientState::CLIENT_RECONNECTING);
		}

		/* Returns true if the client is not connected to the server - false otherwise */
		bool IsDisconnected() const
		{
			return clientState == CrossClientState::CLIENT_NEEDS_TO_CONNECT;
		}

		/* Returns true if the client is ready to transmit custom data */
		bool IsReady() const
		{
			return clientState == CrossClientState::CLIENT_CONNECTED;
		}

		/* Add custom data to this clients's custom data list - the event will fire when this data's type is received from the server */
		bool AddDataHandler(std::string inDataName, void(*inFunction)(const CrossPack*, NetTransMethod))
		{
			/* truncate name if too long */
			if (inDataName.length() > CROSS_SOCK_MAX_DATA_NAME_LENGTH)
				inDataName.resize(CROSS_SOCK_MAX_DATA_NAME_LENGTH);

			/* if this data exists, add this callback to the event */
			for (size_t d = 0; d < dataEvents.size(); d++) {
				if (dataEvents[d]->name == inDataName) {
					dataEvents[d]->AddCallback(inFunction);
					return true;
				}
			}

			/* a new event is needed - add if possible  */
			if (clientState == CrossClientState::CLIENT_NEEDS_TO_CONNECT) {
				CrossDataEvent<const CrossPack*, NetTransMethod>* newEvent = new CrossDataEvent<const CrossPack*, NetTransMethod>(inDataName);
				newEvent->AddCallback(inFunction);
				newEvent->dataID = StaticDataID::UNKNOWN_PACK;
				dataEvents.push_back(newEvent);
				return true;
			}

			/* cannot add data handler at this time */
			return false;
		}

		/* Add custom data to this clients's custom data list - the event will fire on the given object (of type Class) when this data's type is received from the server */
		template <class Class>
		bool AddDataHandler(std::string inDataName, void(Class::*inFunction)(const CrossPack*, NetTransMethod), Class* object)
		{
			/* Return failure if the object is a nullptr */
			if (object == nullptr)
				return false;

			/* truncate name if too long */
			if (inDataName.length() > CROSS_SOCK_MAX_DATA_NAME_LENGTH)
				inDataName.resize(CROSS_SOCK_MAX_DATA_NAME_LENGTH);

			/* if this data exists, add this callback to the event */
			for (int d = 0; d < dataEvents.size(); d++) {
				if (dataEvents[d]->name == inDataName) {
					dataEvents[d]->AddObjectCallback<Class>(inFunction, object);
					return true;
				}
			}

			/* a new event is needed - add if possible  */
			if (clientState == CrossClientState::CLIENT_NEEDS_TO_CONNECT) {
				CrossDataEvent<const CrossPack*, NetTransMethod>* newEvent = new CrossDataEvent<const CrossPack*, NetTransMethod>(inDataName);
				newEvent->AddObjectCallback<Class>(inFunction, object);
				newEvent->dataID = StaticDataID::UNKNOWN_PACK;
				dataEvents.push_back(newEvent);
				return true;
			}

			/* cannot add data handler at this time */
			return false;
		}

		/* Get the custom data ID from a given name */
		CrossPackDataID GetDataIDFromName(std::string inDataName) const
		{
			if (clientState == CrossClientState::CLIENT_CONNECTED) {
				if (inDataName.length() > CROSS_SOCK_MAX_DATA_NAME_LENGTH)
					inDataName.resize(CROSS_SOCK_MAX_DATA_NAME_LENGTH);
				auto dataEvent = dataEventsByName.find(inDataName);
				if (dataEvent != dataEventsByName.end())
					return dataEvent->second->dataID;
			}
			return StaticDataID::UNKNOWN_PACK;
		}

		/* Get the data name from its custom data ID*/
		std::string GetNameFromDataID(const CrossPackDataID& inDataID) const
		{
			if (clientState == CrossClientState::CLIENT_CONNECTED) {
				auto dataEvent = dataEventsByID.at(inDataID);
				if (dataEvent)
					return dataEvent->name;
			}
			return "";
		}

		/* Create an empty packet with the given data name - WARNING: this packet must be deleted */
		CrossPackPtr CreatePack(std::string inDataName) const
		{
			return std::make_shared<CrossPack>(GetDataIDFromName(inDataName));
		}

		/* Get the most recent ping to the server in ms */
		double GetPing() const
		{
			return ping;
		}

		/*
		 * CrossSock delegates are ugly - see CrossUtil.h for an explanation
		 */

		/* Set the connection succesful handler - this is the first point that we have a valid client ID */
		void SetConnectHandler(void(*inFunction)())
		{
			delete connectEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			connectEvent = newEvent;
		}

		/* Set the ready to transmit custom data handler */
		void SetReadyHandler(void(*inFunction)())
		{
			delete readyEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			readyEvent = newEvent;
		}

		/* Set the disconnect handler */
		void SetDisconnectHandler(void(*inFunction)())
		{
			delete disconnectEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			disconnectEvent = newEvent;
		}

		/* Set the trying to reconnect handler */
		void SetAttemptReconnectHandler(void(*inFunction)())
		{
			delete attemptReconnectEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			attemptReconnectEvent = newEvent;
		}

		/* Set the succesful reconnect handler */
		void SetReconnectHandler(void(*inFunction)())
		{
			delete reconnectEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			reconnectEvent = newEvent;
		}

		/* Set the failed reconnect handler */
		void SetReconnectFailedHandler(void(*inFunction)())
		{
			delete failedReconnectEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			failedReconnectEvent = newEvent;
		}

		/* Set the initial handshake handler */
		void SetHandshakeHandler(void(*inFunction)())
		{
			delete handshakeEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			handshakeEvent = newEvent;
		}

		/* Set the receive any data handler */
		void SetReceiveDataHandler(void(*inFunction)(const CrossPack*, NetTransMethod))
		{
			delete receiveEvent;
			CrossSingleEvent<void, const CrossPack*, NetTransMethod>* newEvent = new CrossSingleEvent<void, const CrossPack*, NetTransMethod>();
			newEvent->SetCallback(inFunction);
			receiveEvent = newEvent;
		}

		/* Set the transmit error handler - WARNING: depending on the error the packet may be null */
		void SetTransmitErrorHandler(void(*inFunction)(const CrossPack*, NetTransMethod, NetTransError))
		{
			delete transErrorEvent;
			CrossSingleEvent<void, const CrossPack*, NetTransMethod, NetTransError>* newEvent = new CrossSingleEvent<void, const CrossPack*, NetTransMethod, NetTransError>();
			newEvent->SetCallback(inFunction);
			transErrorEvent = newEvent;
		}

		/* Set the connection succesful handler - this is the first point that we have a valid client ID */
		template <class Class>
		void SetConnectHandler(void(Class::*inFunction)(), Class* object)
		{
			delete connectEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			connectEvent = newEvent;
		}

		/* Set the ready to transmit custom data handler */
		template <class Class>
		void SetReadyHandler(void(Class::*inFunction)(), Class* object)
		{
			delete readyEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			readyEvent = newEvent;
		}

		/* Set the disconnect handler */
		template <class Class>
		void SetDisconnectHandler(void(Class::*inFunction)(), Class* object)
		{
			delete disconnectEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			disconnectEvent = newEvent;
		}

		/* Set the trying to reconnect handler */
		template <class Class>
		void SetAttemptReconnectHandler(void(Class::*inFunction)(), Class* object)
		{
			delete attemptReconnectEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			attemptReconnectEvent = newEvent;
		}

		/* Set the succesful reconnect handler */
		template <class Class>
		void SetReconnectHandler(void(Class::*inFunction)(), Class* object)
		{
			delete reconnectEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			reconnectEvent = newEvent;
		}

		/* Set the failed reconnect handler */
		template <class Class>
		void SetReconnectFailedHandler(void(Class::*inFunction)(), Class* object)
		{
			delete failedReconnectEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			failedReconnectEvent = newEvent;
		}

		/* Set the initial handshake handler */
		template <class Class>
		void SetHandshakeHandler(void(Class::*inFunction)(), Class* object)
		{
			delete handshakeEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			handshakeEvent = newEvent;
		}

		/* Set the receive any data handler */
		template <class Class>
		void SetReceiveDataHandler(void(Class::*inFunction)(const CrossPack*, NetTransMethod), Class* object)
		{
			delete receiveEvent;
			CrossObjectEvent<Class, void, const CrossPack*, NetTransMethod>* newEvent = new CrossObjectEvent<Class, void, const CrossPack*, NetTransMethod>();
			newEvent->SetCallback(inFunction, object);
			receiveEvent = newEvent;
		}

		/* Set the transmit error handler - WARNING: depending on the error the packet may be null */
		template <class Class>
		void SetTransmitErrorHandler(void(Class::*inFunction)(const CrossPack*, NetTransMethod, NetTransError), Class* object)
		{
			delete transErrorEvent;
			CrossObjectEvent<Class, void, const CrossPack*, NetTransMethod, NetTransError>* newEvent = new CrossObjectEvent<Class, void, const CrossPack*, NetTransMethod, NetTransError>();
			newEvent->SetCallback(inFunction, object);
			transErrorEvent = newEvent;
		}

	private:
		TCPSocketPtr serverSocket;
		UDPSocketPtr streamSocket;
		CrossSockAddress serverAddress;
		CrossSockAddressFamily addressFamily;
		CrossClientProperties clientProperties;
		CrossClientState clientState;
		CrossClientID clientID;
		bool streamIsBound;
		CrossEvent<void>* connectEvent;
		CrossEvent<void>* readyEvent;
		CrossEvent<void>* disconnectEvent;
		CrossEvent<void>* attemptReconnectEvent;
		CrossEvent<void>* reconnectEvent;
		CrossEvent<void>* failedReconnectEvent;
		CrossEvent<void>* handshakeEvent;
		CrossEvent<void, const CrossPack*, NetTransMethod>* receiveEvent;
		CrossEvent<void, const CrossPack*, NetTransMethod, NetTransError>* transErrorEvent;
		std::vector<CrossDataEvent<const CrossPack*, NetTransMethod>* > dataEvents;
		std::unordered_map<CrossPackDataID, CrossDataEvent<const CrossPack*, NetTransMethod>* > dataEventsByID;
		std::unordered_map<std::string, CrossDataEvent<const CrossPack*, NetTransMethod>* > dataEventsByName;
		int connectionAttempts;
		CrossTimer connectionTimer;
		CrossPackData tcpBuffer[CROSS_SOCK_BUFFER_SIZE];
		CrossBufferLen tcpBufferLength;
		CrossPackData udpBuffer[CROSS_SOCK_BUFFER_SIZE];
		CrossBufferLen udpBufferLength;
		CrossTimer alivenessTestTimer;
		CrossTimer timeoutTimer;
		double timeoutDelay;
		double ping;

		/* 
		 * receive function for when raw data is received - this is where the majority of
		 * the connection process is implemented. In addition, this function is responsible
		 * for calling custom data events.
		 */
		CrossBufferLen OnReceiveNewData(CrossPackData* inData, CrossBufferLen inLength, NetTransMethod inMethod) {
			if (inLength >= CrossPack::GetHeaderSize()) {
				CrossPackHeader header = CrossPack::PeakHeader(inData);
				if (header.payloadSize > CrossPack::MAX_PAYLOAD_BYTES) {
					if (transErrorEvent && transErrorEvent->IsValid())
						transErrorEvent->Execute(nullptr, inMethod, NetTransError::INVALID_PAYLOAD_SIZE);
					return inLength;
				}
				if (header.payloadSize + CrossPack::GetHeaderSize() + CrossPack::GetFooterLength(header) <= inLength) {
					CrossPackFooter footer = CrossPack::PeakFooter(inData, header);
					CrossPack inPack(header, footer, inData);
					if (inPack.GetDataID() == StaticDataID::HANDSHAKE) {
						if (clientState == CrossClientState::CLIENT_RECEIVING_ID || clientID == 0)
						{
							CrossPack pack;
							pack.SetDataID(StaticDataID::INIT_CLIENT_ID);
							SendToServer(&pack);
						}
						else
						{
							CrossPack pack;
							pack.SetDataID(StaticDataID::RECONNECT_PACK);
							pack.AddToPayload<CrossClientID>(clientID);
							SendToServer(&pack);
						}
						if (handshakeEvent && handshakeEvent->IsValid())
							handshakeEvent->Execute();
					}
					else if (inPack.GetDataID() == StaticDataID::INIT_CLIENT_ID || inPack.GetDataID() == StaticDataID::RECONNECT_PACK) {

						// reset timeout
						timeoutTimer.SetToNow();
						timeoutDelay = CROSS_SOCK_MAX_TIMEOUT;
						ping = 0;

						// send aliveness test
						CrossPack alivenessTest;
						alivenessTest.SetDataID(StaticDataID::ALIVENESS_TEST);
						alivenessTest.AddToPayload<float>((float)((clientProperties.alivenessTestDelay + ping) * CROSS_SOCK_TIMEOUT_FACTOR));
						SendToServer(&alivenessTest);

						// check incoming ID
						CrossClientID newID = inPack.RemoveFromPayload<CrossClientID>();
						clientState = CrossClientState::CLIENT_RECEIVING_DATA_LIST;
						if (newID != 0) {
							clientID = newID;
							if (inPack.GetDataID() == StaticDataID::RECONNECT_PACK) {
								if (reconnectEvent && reconnectEvent->IsValid())
									reconnectEvent->Execute();
							}
							else {
								if (clientState == CrossClientState::CLIENT_REQUESTING_ID && failedReconnectEvent && failedReconnectEvent->IsValid())
									failedReconnectEvent->Execute();
								if (connectEvent && connectEvent->IsValid())
									connectEvent->Execute();
							}
							CrossPack pack;
							pack.SetDataID(StaticDataID::INIT_CUSTOM_DATA_LIST);
							SendToServer(&pack);
						}
						else { /* resend id request packet */
							CrossPack pack;
							if (clientState == CrossClientState::CLIENT_RECEIVING_ID) {
								pack.SetDataID(StaticDataID::INIT_CLIENT_ID);
							}
							else {
								pack.SetDataID(StaticDataID::RECONNECT_PACK);
								pack.AddToPayload<CrossClientID>(clientID);
							}
							SendToServer(&pack);
						}
					}
					else if (inPack.GetDataID() == StaticDataID::DISCONNECT_PACK) {
						Disconnect();
					}
					else if (inPack.GetDataID() == StaticDataID::INIT_CUSTOM_DATA_LIST) {

						// get data from packet
						CrossPackPayloadLen numCustomData = inPack.RemoveFromPayload<CrossPackPayloadLen>();
						CrossPackPayloadLen customDataIndex = inPack.RemoveFromPayload<CrossPackPayloadLen>();
						std::string dataName = inPack.RemoveStringFromPayload();
						CrossPackDataID dataID = inPack.RemoveFromPayload<CrossPackDataID>();

						// look for existing data event
						bool foundMatch = false;
						for (size_t x = 0; x < dataEvents.size(); x++) {
							auto dataEvent = dataEvents[x];
							if (dataEvent) {
								if (dataEvent->name == dataName) {
									foundMatch = true;
									dataEvent->dataID = dataID;
									break;
								}
							}
						}

						// add new data event if no match was found
						if (!foundMatch) {
							CrossDataEvent<const CrossPack*, NetTransMethod>* newEvent = new CrossDataEvent<const CrossPack*, NetTransMethod>(dataName);
							newEvent->dataID = dataID;
							dataEvents.push_back(newEvent);
						}

						// set state to connected if at the end of the custom data
						if (IsReady() || customDataIndex >= numCustomData - 1) {
							FillDataEventMaps();
							if (!IsReady()) {
								CrossPack outPack;
								outPack.SetDataID(StaticDataID::HANDSHAKE);
								SendToServer(&outPack);
								clientState = CrossClientState::CLIENT_CONNECTED;
								if (readyEvent && readyEvent->IsValid())
									readyEvent->Execute();
							}
						}
					}
					else if (inPack.GetDataID() == StaticDataID::ALIVENESS_TEST) {

						/* clock in ping */
						ping = timeoutTimer.GetElapsedTime() - timeoutDelay;
						if (ping < 0.0) {
							ping = 0.0;
						}

						/* reset timeout timer and update expected timeout delay */
						timeoutTimer.SetToNow();
						timeoutDelay = inPack.RemoveFromPayload<float>();
					}
					else { /* custom or unknown data */

						   /* call receive events if data ID is known and the packet is valid */
						if (inPack.GetDataID() != StaticDataID::UNKNOWN_PACK && (inMethod == NetTransMethod::TCP || inPack.IsValid())) {

							/* find custom event if it exists */
							auto dataEvent = dataEventsByID.find(inPack.GetDataID());

							/* call receive event if it is valid */
							if (receiveEvent && receiveEvent->IsValid()) {
								receiveEvent->Execute(&inPack, inMethod);
								inPack.Reset();
							}

							/* call each custom event callback until finished or disconnected */
							if (IsRunning() && dataEvent != dataEventsByID.end()) {
								for (int c = dataEvent->second->GetNumCallbacks() - 1; c >= 0; c--) {
									if (IsDisconnected()) {
										break;
									}
									dataEvent->second->Execute(c, &inPack, inMethod);
									inPack.Reset();
								}
							}
						}
						else { /* else data ID is unknown or checksum is invalid - call transmit error event */
							if (transErrorEvent && transErrorEvent->IsValid()) {
								NetTransError err;
								if (inPack.GetDataID() != StaticDataID::UNKNOWN_PACK)
									err = NetTransError::INVALID_DATA_ID;
								else
									err = NetTransError::INVALID_CHECKSUM;
								transErrorEvent->Execute(&inPack, inMethod, err);
							}
						}
					}

					/* return packet size */
					return inPack.GetPacketSize();
				}
			}

			/* return 0 - no buffer data used */
			return 0;
		}

		/* Fills the data event hash tables for ~O(k) access */
		void FillDataEventMaps()
		{
			for (size_t d = 0; d < dataEvents.size(); d++) {
				dataEventsByID[dataEvents[d]->dataID] = dataEvents[d];
				dataEventsByName[dataEvents[d]->name] = dataEvents[d];
			}
		}

		/* Resets data event hash tables */
		void ResetDataEventIDs()
		{
			dataEventsByID.clear();
			dataEventsByName.clear();
			for (size_t d = 0; d < dataEvents.size(); d++) {
				dataEvents[d]->dataID = StaticDataID::UNKNOWN_PACK;
			}
		}
	};

	typedef std::shared_ptr<CrossClient> CrossClientPtr;
}

#endif
