

/**********************************************************************************************************
*  AUTHOR: Brandon Wilson  ********************************************************************************
*  A type-safe cross-platform header-only lightweight socket library developed on top of berkely sockets  *
**********************************************************************************************************/


#ifndef CROSS_SOCK_SERVER
#define CROSS_SOCK_SERVER


/*
 * The high-level server implementation.  See CrossServerDemo.cpp for
 * general usage, and CrossServerProperties for configuration. Note
 * that this class is implemented with the RAII standard, and so
 * objects should be created in the highest scope or initialized
 * dynamically using smart pointers (CrossServerPtr can be used for
 * convenience).
 */

#include "CrossSock.h"
#include "CrossPack.h"
#include <unordered_map>


namespace CrossSock {

	enum CrossClientEntryState
	{
		/* Client has not yet assigned a client ID */
		CLIENT_ENTRY_INIT = 0,

		/* Client is exchanging custom data lists with the server */
		CLIENT_ENTRY_DATA_LIST_EXCHANGE = 1,

		/* Client is connected */
		CLIENT_ENTRY_CONNECTED = 2,

		/* Client has disconnected */
		CLIENT_ENTRY_DISCONNECTED = 3
	};

	/* 
	 * List entry for each connected client - AKA a client as seen
	 * by the server. Smart pointers are used, as the server could
	 * delete a client during any given update step due to timeout,
	 * by request of the client, or through custom server logic.
	 */
	class CrossClientEntry {
	public:

		CrossClientEntry()
		{
			state = CrossClientEntryState::CLIENT_ENTRY_INIT;
			tcpBufferLength = 0;
			clientID = 0;
			customData = nullptr;
			timeoutTimer.SetToNow();
			timeoutDelay = CROSS_SOCK_MAX_TIMEOUT;
		}

		bool operator==(const CrossClientEntry& inOther) const
		{
			return (clientID == inOther.clientID);
		}

		/* Get this client's address */
		CrossSockAddress GetClientAddress() const
		{
			return address;
		}

		/* Get the TCP socket */
		TCPSocketPtr GetTCPSocket() const
		{
			return socket;
		}

		/* Get this client's unique ID */
		CrossClientID GetClientID() const
		{
			return clientID;
		}

		/* Get this client's state */
		CrossClientEntryState GetClientEntryState() const
		{
			return state;
		}

		/* Returns true if this client is ready to transmit custom data - false otherwise */
		bool IsReady() const
		{
			return state == CrossClientEntryState::CLIENT_ENTRY_CONNECTED;
		}

		/* Returns true if this client is not disconnected - false otherwise */
		bool IsRunning() const
		{
			return state != CrossClientEntryState::CLIENT_ENTRY_DISCONNECTED;
		}

		/* Returns true if this client is not disconnected - false otherwise */
		void ResetTimeout(double ExpectedTimeoutDelay)
		{
			// calculate ping
			ping = timeoutTimer.GetElapsedTime() - timeoutDelay;
			if (ping < 0.0) {
				ping = 0.0;
			}

			// reset timeout
			timeoutTimer.SetToNow();
			timeoutDelay = ExpectedTimeoutDelay;
		}

		/* Returns true if this client is not disconnected - false otherwise */
		bool HasTimedOut() const
		{
			return timeoutTimer.GetElapsedTime() >= timeoutDelay;
		}

		/* Gets the most recent ping for this client in ms */
		double GetPing() const
		{
			return ping;
		}

		/* Sets the custom data pointer to the given object */
		template <class T>
		void SetCustomData(T* inData)
		{
			customData = inData;
		}

		/* Gets the custom data object of a given type */
		template <class T>
		T* GetCustomData() const
		{
			T* ptr = static_cast<T*>(customData);
			return ptr;
		}

		/* Gets the key for this event */
		size_t Hash() const
		{
			size_t key = clientID;
			key = (key ^ (key >> 30)) * size_t(0xbf58476d1ce4e5b9);
			key = (key ^ (key >> 27)) * size_t(0x94d049bb133111eb);
			key = key ^ (key >> 31);
			return key;
		}

	private:
		friend class CrossServer;

		CrossSockAddress address;
		TCPSocketPtr socket;
		CrossClientID clientID;
		CrossClientEntryState state;
		CrossPackData tcpBuffer[CROSS_SOCK_BUFFER_SIZE];
		CrossBufferLen tcpBufferLength;
		double timeoutDelay;
		CrossTimer timeoutTimer;
		void* customData;
		double ping;
	};

	typedef std::shared_ptr<CrossSock::CrossClientEntry> CrossClientEntryPtr;
}

namespace std
{
	/* declare hash functions */
	template<> struct hash< CrossSock::CrossClientEntryPtr >
	{
		size_t operator()(const CrossSock::CrossClientEntryPtr inClientEntry) const
		{
			return inClientEntry->Hash();
		}
	};

	template<> struct hash< CrossSock::CrossDataEvent<const CrossSock::CrossPack*, CrossSock::CrossClientEntryPtr, CrossSock::NetTransMethod>* >
	{
		size_t operator()(const CrossSock::CrossDataEvent<const CrossSock::CrossPack*, CrossSock::CrossClientEntryPtr, CrossSock::NetTransMethod>* inEvent) const
		{
			return inEvent->Hash();
		}
	};
}

namespace CrossSock {

	/* Server properties list */
	struct CrossServerProperties {

		/*
		 * The size of the connection backlog - if more than this number of clients
		 * have been handles in a given update step, the reset are rejected and must
		 * try again.
		 */
		int newConnectionBacklog;

		/*
		 * The maximum number of UDP transmits per update step
		 */
		int maxUDPTransmitsPerUpdate;

		/*
		 * The maximum number of TCP transmits per update step
		 */
		int maxTCPTransmitsPerUpdate;

		/*
		 * If this server should accept and be able to send connectionless packets
		 */
		bool allowUDPPackets;

		/*
		 * If this server uses and address blacklist
		 */
		bool useBlacklist;

		/*
		 * If this server uses and address whitelist
		 */
		bool useWhitelist;

		/*
		 * How often (in ms) we should send aliveness tests to clients - the larger
		 * this is the more forgiveness towards network hickups. Likewise, the lower
		 * this is the more responsive a timeout will occur.
		 */
		double alivenessTestDelay;

		/*
		 * Whether or not the delete event will be called on destroyed clients -
		 * this should ALWAYS be true for implementations that utilize custom
		 * data on client entries! (and a delete event should be provided)
		 */
		bool shouldFlushDisconnectedClientData;

		/*
		 * How long (in ms) we should wait for reconnect attempts before we
		 * flush (delete) a given client's custom data. Only relevant if
		 * shouldFlushDisconnectedClientData is on.
		 */
		double disconnectedClientFlushDelay;

		CrossServerProperties()
		{
			newConnectionBacklog = 32;
			maxUDPTransmitsPerUpdate = 256;
			maxTCPTransmitsPerUpdate = 4;
			allowUDPPackets = true;
			useBlacklist = true;
			useWhitelist = false;
			alivenessTestDelay = 1000.0;
			shouldFlushDisconnectedClientData = true;
			disconnectedClientFlushDelay = CROSS_SOCK_MAX_TIMEOUT;
		}
	};

	enum CrossServerState
	{
		/* Has yet to start */
		SERVER_NEEDS_STARTUP = 0,

		/* Attempting to bind the listen socket */
		SERVER_BINDING = 1,

		/* Servicing connected clients */
		SERVER_LOOP = 2
	};

	/* A high level server object */
	class CrossServer {
	private:

		void Init()
		{
			listenSocket = nullptr;
			streamSocket = nullptr;
			serverProperties = CrossServerProperties();
			streamIsBound = false;
			udpBufferLength = 0;
			nextAvailableClientID = 1;
			nextAvailableDataID = StaticDataID::CUSTOM_DATA_START;
			serverState = CrossServerState::SERVER_NEEDS_STARTUP;
			connectEvent = nullptr;
			disconnectEvent = nullptr;
			reconnectEvent = nullptr;
			failedReconnectEvent = nullptr;
			destroyClientEvent = nullptr;
			initializeClientEvent = nullptr;
			readyEvent = nullptr;
			rejectEvent = nullptr;
			bindEvent = nullptr;
			validateEvent = nullptr;
			receiveEvent = nullptr;
			transErrorEvent = nullptr;
		}

	public:

		CrossServer()
		{
			Init();
		}

		CrossServer(CrossServerProperties inProperties)
		{
			Init();
			SetServerProperties(inProperties);
		}

		~CrossServer()
		{
			Stop();
			for (auto it = dataEvents.begin(); it != dataEvents.end(); ++it) {
				delete *it;
			}
			delete connectEvent;
			delete disconnectEvent;
			delete reconnectEvent;
			delete failedReconnectEvent;
			delete destroyClientEvent;
			delete initializeClientEvent;
			delete readyEvent;
			delete rejectEvent;
			delete bindEvent;
			delete validateEvent;
			delete receiveEvent;
			delete transErrorEvent;
		}

		/* Start listening with the given port and address family */
		void Start(const uint16_t& inPort, const CrossSockAddressFamily& inFamily = CrossSockAddressFamily::INET)
		{
			port = inPort;
			addressFamily = inFamily;
			FillDataEventMaps();
			serverState = CrossServerState::SERVER_BINDING;
			Update();
		}

		/* Stop this server - disconnects all connected clients */
		void Stop()
		{
			if (serverState != CrossServerState::SERVER_NEEDS_STARTUP) {
				auto it = connectedClients.begin();
				while (it != connectedClients.end()) {
					CrossClientEntryPtr clientToDisconnect = it->second;
					DisconnectClient(clientToDisconnect, false);
					it++;
					clientToDisconnect.reset();
				}
				connectedClients.clear();
				auto it2 = disconnectedClients.begin();
				while (it2 != disconnectedClients.end()) {
					CrossClientEntryPtr disconnectedClient = it2->second;
					if (destroyClientEvent && destroyClientEvent->IsValid())
						destroyClientEvent->Execute(disconnectedClient);
					it2++;
					disconnectedClient.reset();
				}
				disconnectedClients.clear();
				if (listenSocket) {
					listenSocket->Close();
					listenSocket.reset();
				}
				if (streamSocket) {
					streamSocket->Close();
					streamSocket.reset();
				}
				streamIsBound = false;
				udpBufferLength = 0;
				serverState = CrossServerState::SERVER_NEEDS_STARTUP;
			}
		}

		/* Automatically connects clients and receives data */
		void Update()
		{
			/* if the listen socket still needs to be bound */
			if (serverState == CrossServerState::SERVER_BINDING) {

				/* if the listen socket is invalid */
				if (!listenSocket) {
					listenSocket = CrossSockUtil::CreateTCPSocket(addressFamily);
					if (listenSocket)
						listenSocket->SetNonBlockingMode(true);
				}

				/* if the listen socket is valid */
				if (listenSocket) {

					/* attempt to bind the address */
					CrossSockAddress address(CrossSockAddress::ANY_ADDRESS, port, addressFamily);
					int result = listenSocket->Bind(address);
					int err = CrossSockUtil::GetLastError();

					/* if succesfully bound: update state and execute bind event */
					if (result >= 0 || (err == CrossSockError::SUCCESS || err == CrossSockError::ALREADY)) {

						serverState = CrossServerState::SERVER_LOOP;
						alivenessTestTimer.SetToNow();
						if (bindEvent && bindEvent->IsValid())
							bindEvent->Execute();
					}
				}
			}
			else if (serverState == CrossServerState::SERVER_LOOP) { /* else if running */

				/* hey! listen! */
				listenSocket->Listen(serverProperties.newConnectionBacklog);

				/* accept and validate new connections*/
				int newConnections = 0;
				int err;
				do {

					/* increment connections counter */
					newConnections++;

					/* accept new clients */
					CrossSockAddress newClientAddress;
					TCPSocketPtr newSocket = listenSocket->Accept(newClientAddress);
					err = CrossSockUtil::GetLastError();
					if (err == CrossSockError::SUCCESS) {

						/* assemble new client entry */
						newSocket->SetNonBlockingMode(true);
						CrossClientEntryPtr newEntry = std::make_shared<CrossClientEntry>();
						newEntry->address = newClientAddress;
						newEntry->socket = newSocket;
						newEntry->clientID = nextAvailableClientID;

						/* get connection list entry */
						bool canConnect = false;
						bool onList = false;
						auto itr = canConnectList.find(newClientAddress);
						if (itr != canConnectList.end()) {
							canConnect = itr->second;
							onList = true;
						}

						/* validate against blacklist */
						if (!serverProperties.useBlacklist || !onList || canConnect) {
							/* validate against whitelist */
							if (!serverProperties.useWhitelist || canConnect) {
								/* do custom client connection validation */
								if (!validateEvent || !validateEvent->IsValid() || validateEvent->Execute(newEntry))
								{
									/* accept connection */
									nextAvailableClientID++;
									connectedClients[newEntry->clientID] = newEntry;

									/* send init packet */
									CrossPack pack;
									pack.SetDataID(StaticDataID::HANDSHAKE);
									SendToClient(&pack, newEntry);
								}
								else {
									/* execute callback */
									if (rejectEvent && rejectEvent->IsValid())
										rejectEvent->Execute(newEntry);

									/* reject connection */
									DisconnectClient(newEntry);
								}
							}
							else {
								/* execute callback */
								if (rejectEvent && rejectEvent->IsValid())
									rejectEvent->Execute(newEntry);

								/* reject connection */
								DisconnectClient(newEntry);
							}
						}
						else {
							/* execute callback */
							if (rejectEvent && rejectEvent->IsValid())
								rejectEvent->Execute(newEntry);

							/* reject connection */
							DisconnectClient(newEntry);
						}
					}
					else { /* else failed to accept new connection - stop trying to accept new clients */
						break;
					}
				} while (newConnections < serverProperties.newConnectionBacklog);

				/* do aliveness test if possible */
				if (alivenessTestTimer.GetElapsedTime() >= serverProperties.alivenessTestDelay) {

					// reset timer and assemble packet
					alivenessTestTimer.SetToNow();
					CrossPack alivenessTest;
					alivenessTest.SetDataID(StaticDataID::ALIVENESS_TEST);

					// iterate through each client
					auto it = connectedClients.begin();
					while (it != connectedClients.end()) {

						// set aliveness test delay
						alivenessTest.ClearPayload();
						alivenessTest.AddToPayload<float>((float)((serverProperties.alivenessTestDelay + it->second->GetPing()) * CROSS_SOCK_TIMEOUT_FACTOR));

						// do aliveness test
						int result = SendToClient(&alivenessTest, it->second);
						if (result < 0 || it->second->HasTimedOut()) {
							DisconnectClient(it->second);
						}
						it++;
					}

					// delete disconnect client data if possible
					if (serverProperties.shouldFlushDisconnectedClientData) {
						auto it2 = disconnectedClients.begin();
						while (it2 != disconnectedClients.end()) {

							if (it2->second->HasTimedOut()) {
								CrossClientEntryPtr oldClientEntry = it2->second;
								if (destroyClientEvent && destroyClientEvent->IsValid())
									destroyClientEvent->Execute(oldClientEntry);
								it2 = disconnectedClients.erase(it2);
								oldClientEntry.reset();
							}
							else
								it2++;
						}
					}
				}

				/* handle TCP data */
				auto it = connectedClients.begin();
				while (it != connectedClients.end()) {
					if (it->second && it->second->IsRunning()) {
						CrossClientEntryPtr client = it->second;
						it++;
						CrossBufferLen bytesReceived;
						int tcpTransmits = 0;
						do {
							tcpTransmits++;
							bytesReceived = client->socket->Receive(client->tcpBuffer + client->tcpBufferLength, CROSS_SOCK_BUFFER_SIZE - client->tcpBufferLength);
							if (bytesReceived > 0) {
								client->tcpBufferLength += bytesReceived;
								CrossBufferLen dataUsed;
								CrossBufferLen bufferPos = 0;
								do {
									dataUsed = OnReceiveNewData(client->tcpBuffer + bufferPos, client->tcpBufferLength - bufferPos, client, NetTransMethod::TCP);
									bufferPos += dataUsed;
								} while (IsRunning() && client && client->IsRunning() && dataUsed > 0 && bufferPos < client->tcpBufferLength);

								/* reset buffer to front */
								client->tcpBufferLength -= bufferPos;
								memcpy(client->tcpBuffer, client->tcpBuffer + bufferPos, client->tcpBufferLength);
							}
							else if (CrossSockUtil::GetLastError() == CrossSockError::CONNRESET) {
								DisconnectClient(client);
							}
						} while (IsRunning() && client && client->IsRunning() && bytesReceived > 0 && tcpTransmits < serverProperties.maxTCPTransmitsPerUpdate);
					}
					else {
						it++;
					}
				}

				/* handle UDP data */
				if (serverProperties.allowUDPPackets) {
					if (streamIsBound) {
						CrossBufferLen bytesReceived;
						int udpTransmits = 0;
						do {
							udpTransmits++;
							CrossSockAddress fromAddress;
							bytesReceived = streamSocket->ReceiveFrom(udpBuffer + udpBufferLength, CROSS_SOCK_BUFFER_SIZE - udpBufferLength, fromAddress);
							if (bytesReceived > 0) {
								udpBufferLength += bytesReceived;
								CrossBufferLen dataUsed;
								CrossBufferLen bufferPos = 0;
								do {
									dataUsed = OnReceiveNewData(udpBuffer + bufferPos, udpBufferLength - bufferPos, nullptr, NetTransMethod::UDP);
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
						} while (IsRunning() && streamIsBound && bytesReceived > 0 && udpTransmits < serverProperties.maxUDPTransmitsPerUpdate);
					}
					else { /* bind UDP socket if needed */
						if (!streamSocket) {
							streamSocket = CrossSockUtil::CreateUDPSocket(addressFamily);
							if (streamSocket)
								streamSocket->SetNonBlockingMode(true);
						}
						if (streamSocket) {
							CrossSockAddress address(CrossSockAddress::ANY_ADDRESS, port, addressFamily);
							int result = streamSocket->Bind(address);
							int err = CrossSockUtil::GetLastError();
							if (result >= 0 || (err == CrossSockError::SUCCESS || err == CrossSockError::ISCONN))
								streamIsBound = true;
						}
					}
				}

				/* consider disconnecting clients */
				it = connectedClients.begin();
				while (it != connectedClients.end()) {

					/* destroy and remove client if no longer running */
					if (it->second == nullptr || !(it->second->IsRunning())) {
						it = connectedClients.erase(it);
					}
					else {
						it++;
					}
				}
			}
		}

		void SetServerProperties(CrossServerProperties inProperties)
		{
			// consider sending aliveness test if the aliveness test delay is changing
			if (IsRunning() && serverProperties.alivenessTestDelay != inProperties.alivenessTestDelay) {
				CrossPack alivenessTest;
				alivenessTest.SetDataID(StaticDataID::ALIVENESS_TEST);
				alivenessTest.AddToPayload<float>((float)(inProperties.alivenessTestDelay * CROSS_SOCK_TIMEOUT_FACTOR));
				SendToAll(&alivenessTest);
			}

			serverProperties = inProperties;
		}

		CrossServerProperties GetServerProperties() const
		{
			return serverProperties;
		}

		/* Get the client entry using its unique ID */
		CrossClientEntryPtr GetClientEntry(const CrossClientID& inClientID) const
		{
			auto client = connectedClients.find(inClientID);
			if (client != connectedClients.end()) {
				return client->second;
			}
			return nullptr;
		}

		/* Get the iterator to the first node in the client list */
		std::unordered_map<CrossClientID, CrossClientEntryPtr>::iterator Clients_Begin()
		{
			return connectedClients.begin();
		}

		/* Get the iterator to the last node in the client list */
		std::unordered_map<CrossClientID, CrossClientEntryPtr>::iterator Clients_End()
		{

			return connectedClients.end();
		}

		/* Get the constant iterator to the first node in the client list */
		std::unordered_map<CrossClientID, CrossClientEntryPtr>::const_iterator Clients_CBegin() const
		{
			return connectedClients.begin();
		}

		/* Get the constant iterator to the last node in the client list */
		std::unordered_map<CrossClientID, CrossClientEntryPtr>::const_iterator Clients_CEnd() const
		{
			return connectedClients.end();
		}

		/* Get the number of clients connected to the server */
		size_t GetNumConnectedClients() const
		{
			return connectedClients.size();
		}

		/* Send a packet reliably to a client - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful */
		int SendToClient(const CrossPackPtr inPack, const CrossClientEntryPtr inClient, const bool inShouldBlockUntilSent = true) const
		{
			return SendToClient(inPack.get(), inClient, inShouldBlockUntilSent);
		}

		/* Send a packet reliably to a client - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful */
		int SendToClient(const CrossPack* inPack, const CrossClientEntryPtr inClient, const bool inShouldBlockUntilSent = true) const
		{
			if (!inClient || inClient->state == CrossClientEntryState::CLIENT_ENTRY_DISCONNECTED)
				return NetTransError::CLIENT_NOT_CONNECTED;
			else {
				/* send until succesful */
				int result;
				do {
					result = inClient->socket->Send(inPack->Serialize(), inPack->GetPacketSize());
				} while (inShouldBlockUntilSent && result < 0 && CrossSockUtil::GetLastError() == CrossSockError::WOULDBLOCK);
				return result;
			}
		}

		/* Send a packet reliably to all connected clients */
		void SendToAll(const CrossPackPtr inPack, const bool inShouldBlockUntilSent = true) const
		{
			return SendToAll(inPack.get(), inShouldBlockUntilSent);
		}

		/* Send a packet reliably to all connected clients */
		void SendToAll(const CrossPack* inPack, const bool inShouldBlockUntilSent = true) const
		{
			auto it = connectedClients.begin();
			while (it != connectedClients.end()) {
				if (it->second && it->second->state != CrossClientEntryState::CLIENT_ENTRY_DISCONNECTED) {
					/* send until succesful */
					int result;
					do {
						result = it->second->socket->Send(inPack->Serialize(), inPack->GetPacketSize());
					} while (inShouldBlockUntilSent && result < 0 && CrossSockUtil::GetLastError() == CrossSockError::WOULDBLOCK);
				}
				it++;
			}
		}

		/* Send a packet unreliably to a client - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful. WARNING: Will finalize the packet automatically */
		int StreamToClient(const CrossPackPtr inPack, const CrossClientEntryPtr inClient, const bool inShouldBlockUntilSent = true) const
		{
			return StreamToClient(inPack.get(), inClient, inShouldBlockUntilSent);
		}

		/* Send a packet unreliably to a client - returns the number of bytes sent if succesful, or the NetTransError if unsuccesful. WARNING: Will finalize the packet automatically */
		int StreamToClient(const CrossPack* inPack, const CrossClientEntryPtr inClient, const bool inShouldBlockUntilSent = true) const
		{
			if (!inClient || inClient->state == CrossClientEntryState::CLIENT_ENTRY_DISCONNECTED)
				return NetTransError::CLIENT_NOT_CONNECTED;
			else if (!streamIsBound)
				return NetTransError::STREAM_NOT_BOUND;
			else {

				/* finalize if necessary */
				if (!inPack->IsFinalized()) {
					inPack->Finalize(false, true, GetServerID());
				}

				/* send until succesful */
				int result;
				do {
					result = streamSocket->SendTo(inPack->Serialize(), inPack->GetPacketSize(), inClient->address);
				} while (inShouldBlockUntilSent && result < 0 && CrossSockUtil::GetLastError() == CrossSockError::WOULDBLOCK);
				return result;
			}
		}

		/* Send a packet unreliably to all connected clients. WARNING: Will finalize the packet automatically */
		void StreamToAll(const CrossPackPtr inPack, const bool inShouldBlockUntilSent = true) const
		{
			return StreamToAll(inPack.get(), inShouldBlockUntilSent);
		}

		/* Send a packet unreliably to all connected clients. WARNING: Will finalize the packet automatically */
		void StreamToAll(const CrossPack* inPack, const bool inShouldBlockUntilSent = true) const
		{
			if (streamIsBound) {

				/* finalize if necessary */
				if (!inPack->IsFinalized()) {
					inPack->Finalize(false, true, GetServerID());
				}

				auto it = connectedClients.begin();
				while (it != connectedClients.end()) {
					if (it->second && it->second->state != CrossClientEntryState::CLIENT_ENTRY_DISCONNECTED) {
						/* send until succesful */
						int result;
						do {
							streamSocket->SendTo(inPack->Serialize(), inPack->GetPacketSize(), it->second->address);
						} while (inShouldBlockUntilSent && result < 0 && CrossSockUtil::GetLastError() == CrossSockError::WOULDBLOCK);
					}
					it++;
				}
			}
		}

		/* Get the server's listen socket used to connect new clients */
		TCPSocketPtr GetListenSocket() const
		{
			return listenSocket;
		}

		/* Get the server's stream socket used to transmit and receive UDP packets */
		UDPSocketPtr GetStreamSocket() const
		{
			return streamSocket;
		}

		uint16_t GetPort() const
		{
			return port;
		}

		CrossSockAddressFamily GetAddressFamily() const
		{
			return addressFamily;
		}

		/* Get the server's state */
		CrossServerState GetServerState() const
		{
			return serverState;
		}

		/* Add custom data to this server's custom data list without a handler - useful for send-only data */
		bool AddDataType(std::string inDataName)
		{
			/* truncate name if too long */
			if (inDataName.length() > CROSS_SOCK_MAX_DATA_NAME_LENGTH)
				inDataName.resize(CROSS_SOCK_MAX_DATA_NAME_LENGTH);

			/* if this data exists, add this callback to the event */
			for (size_t d = 0; d < dataEvents.size(); d++) {
				if (dataEvents[d]->name == inDataName) {
					return true;
				}
			}

			/* a new event is needed - add if possible  */
			if (serverState == CrossServerState::SERVER_NEEDS_STARTUP) {
				CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>* newEvent = new CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>(inDataName);
				newEvent->dataID = nextAvailableDataID;
				nextAvailableDataID++;
				dataEvents.push_back(newEvent);
				return true;
			}

			/* cannot add data handler at this time */
			return false;
		}

		/* Add custom data to this server's custom data list - the event will fire when this data's type is received from a client */
		bool AddDataHandler(std::string inDataName, void(*inFunction)(const CrossPack*, CrossClientEntryPtr, NetTransMethod))
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
			if (serverState == CrossServerState::SERVER_NEEDS_STARTUP) {
				CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>* newEvent = new CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>(inDataName);
				newEvent->AddCallback(inFunction);
				newEvent->dataID = nextAvailableDataID;
				nextAvailableDataID++;
				dataEvents.push_back(newEvent);
				return true;
			}

			/* cannot add data handler at this time */
			return false;
		}

		/* Add custom data to this server's custom data list - the event will fire on the given object (of type Class) when this data's type is received from a client */
		template <class Class>
		bool AddDataHandler(std::string inDataName, void(Class::*inFunction)(const CrossPack*, CrossClientEntryPtr, NetTransMethod), Class* object)
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
			if (serverState == CrossServerState::SERVER_NEEDS_STARTUP) {
				CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>* newEvent = new CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>(inDataName);
				newEvent->AddObjectCallback<Class>(inFunction, object);
				newEvent->dataID = nextAvailableDataID;
				nextAvailableDataID++;
				dataEvents.push_back(newEvent);
				return true;
			}

			/* cannot add data handler at this time */
			return false;
		}

		/* Add an address to the blacklist so that it can no longer connect to the server */
		void AddAddressToBlacklist(const CrossSockAddress& inAddress)
		{
			canConnectList[inAddress] = false;
			DisconnectAddress(inAddress);
		}

		/* Remove an address from the blacklist so that it can succesfully connect to the server */
		void RemoveAddressFromBlacklist(const CrossSockAddress& inAddress)
		{
			auto canConnect = canConnectList.find(inAddress);
			if (canConnect != canConnectList.end() && !canConnect->second)
				canConnectList.erase(inAddress);
		}

		/* Add an address to the whitelist so that it can succesfully connect to the server */
		void AddAddressToWhitelist(const CrossSockAddress& inAddress)
		{
			canConnectList[inAddress] = true;
		}

		/* Remove an address from the whitelist so that it can no longer connect to the server */
		void RemoveAddressFromWhitelist(const CrossSockAddress& inAddress)
		{
			auto canConnect = canConnectList.find(inAddress);
			if (canConnect != canConnectList.end() && canConnect->second)
				canConnectList.erase(inAddress);
		}

		/* Disconnects and deletes a client using its client entry */
		void DisconnectClient(CrossClientEntryPtr inClient, bool ShouldSaveDisconnectedClientData = true)
		{
			if (inClient && inClient->state != CrossClientEntryState::CLIENT_ENTRY_DISCONNECTED) {

				// add to disconnect list if desired
				if (ShouldSaveDisconnectedClientData) {
					disconnectedClients[inClient->clientID] = inClient;
					if (serverProperties.shouldFlushDisconnectedClientData) {
						inClient->ResetTimeout(serverProperties.disconnectedClientFlushDelay);
					}
				}

				// disconnect this client
				CrossPack pack;
				pack.SetDataID(StaticDataID::DISCONNECT_PACK);
				SendToClient(&pack, inClient);
				inClient->state = CrossClientEntryState::CLIENT_ENTRY_DISCONNECTED;
				if (disconnectEvent && disconnectEvent->IsValid())
					disconnectEvent->Execute(inClient);
				if (inClient->socket) {
					inClient->socket->Close();
					inClient->socket.reset();
				}
			}
		}

		/* Disconnect all clients with the given address - returns the number of clients removed */
		size_t DisconnectAddress(const CrossSockAddress& inAddress)
		{
			size_t clientsRemoved = 0;
			auto it = connectedClients.begin();
			while (it != connectedClients.end()) {
				if (inAddress == it->second->address) {
					CrossClientEntryPtr clientToDisconnect = it->second;
					DisconnectClient(clientToDisconnect);
					clientsRemoved++;
				}
				it++;
			}
			return clientsRemoved;
		}

		/* Get the local address of this server */
		CrossSockAddress GetLocalAddress() const
		{
			if (listenSocket)
				return listenSocket->GetLocalAddress();
			else {
				CrossSockAddress outAddr;
				return outAddr;
			}
		}

		/* Get the custom data ID from a given name */
		CrossPackDataID GetDataIDFromName(std::string inDataName) const
		{
			if (inDataName.length() > CROSS_SOCK_MAX_DATA_NAME_LENGTH)
				inDataName.resize(CROSS_SOCK_MAX_DATA_NAME_LENGTH);
			if (IsRunning()) {
				auto dataEvent = dataEventsByName.find(inDataName);
				if (dataEvent != dataEventsByName.end()) {
					return dataEvent->second->dataID;
				}
			}
			else {
				for (size_t d = 0; d < dataEvents.size(); d++) {
					if (dataEvents[d]->name == inDataName)
						return dataEvents[d]->dataID;
				}
			}
			return StaticDataID::UNKNOWN_PACK;
		}

		/* Get the data name from its custom data ID*/
		std::string GetNameFromDataID(const CrossPackDataID& inDataID) const
		{
			if (IsRunning()) {
				auto dataEvent = dataEventsByID.find(inDataID);
				if (dataEvent != dataEventsByID.end()) {
					return dataEvent->second->name;
				}
			}
			else {
				for (size_t d = 0; d < dataEvents.size(); d++) {
					if (dataEvents[d]->dataID == inDataID)
						return dataEvents[d]->name;
				}
			}
			return "";
		}

		/* Create an empty packet with the given data name */
		CrossPackPtr CreatePack(std::string inDataName) const
		{
			return std::make_shared<CrossPack>(GetDataIDFromName(inDataName));
		}

		/* Returns true if the server is not disconnected - false otherwise */
		bool IsRunning() const
		{
			return serverState != CrossServerState::SERVER_NEEDS_STARTUP;
		}

		/* Returns true if the server is ready to service clients - false otherwise */
		bool IsReady() const
		{
			return serverState == CrossServerState::SERVER_LOOP;
		}

		/* Returns true if the UDP socket has been bound - false otherwise */
		bool IsStreamBound() const
		{
			return streamIsBound;
		}

		/* Returns the server's ID - this will always be 0 */
		CrossClientID GetServerID() const
		{
			return 0;
		}

		/*
		* CrossSock delegates are ugly - see CrossUtil.h for an explanation
		*/

		/* Set the client connected handler - the clients have their ID at this point */
		void SetClientConnectedHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete connectEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			connectEvent = newEvent;
		}

		/* Set the client disconnected handler - any custom client data MUST be deleted with this handler */
		void SetClientDisconnectedHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete disconnectEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			disconnectEvent = newEvent;
		}

		/* Set the client reconnected succesfully handler*/
		void SetClientReconnectedHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete reconnectEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			reconnectEvent = newEvent;
		}

		/* Set the client reconnect failed handler*/
		void SetClientReconnectFailedHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete failedReconnectEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			failedReconnectEvent = newEvent;
		}

		/* Set the destroy client handler - this is necessary and should be used to delete any custom client data */
		void SetDestroyClientHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete destroyClientEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			destroyClientEvent = newEvent;
		}

		/* Set the initialize client handler - preferably this is where custom data should be created and assigned to this client
		
		   if additonal data is necessary before the client can be initialized, then a custom event should be used.
		   NOTE: if this is the case, please consider failed disconnects and client destroyed events (due to timeouts on reconnection) */
		void SetInitializeClientHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete initializeClientEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			initializeClientEvent = newEvent;
		}

		/* Set the client ready to transmit custom data handler */
		void SetClientReadyHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete readyEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			readyEvent = newEvent;
		}

		/* Set the client rejected from client handler*/
		void SetClientRejectedHandler(void(*inFunction)(CrossClientEntryPtr))
		{
			delete rejectEvent;
			CrossSingleEvent<void, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			rejectEvent = newEvent;
		}

		/* Set the server listen socket bound handler */
		void SetServerBindHandler(void(*inFunction)())
		{
			delete bindEvent;
			CrossSingleEvent<void>* newEvent = new CrossSingleEvent<void>();
			newEvent->SetCallback(inFunction);
			bindEvent = newEvent;
		}

		/* Set the client validation handler - clients are only connected if this handler returns true.  By default all clients are connected */
		void SetClientValidationHandler(bool(*inFunction)(CrossClientEntryPtr))
		{
			delete validateEvent;
			CrossSingleEvent<bool, CrossClientEntryPtr>* newEvent = new CrossSingleEvent<bool, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction);
			validateEvent = newEvent;
		}

		/* Set the receive any valid (known data ID and sender) handler */
		void SetReceiveDataHandler(void(*inFunction)(const CrossPack*, CrossClientEntryPtr, NetTransMethod))
		{
			delete receiveEvent;
			CrossSingleEvent<void, const CrossPack*, CrossClientEntryPtr, NetTransMethod>* newEvent = new CrossSingleEvent<void, const CrossPack*, CrossClientEntryPtr, NetTransMethod>();
			newEvent->SetCallback(inFunction);
			receiveEvent = newEvent;
		}

		/* Set the transmit error handler - WARNING: depending on the error the packet or client may be null */
		void SetTransmitErrorHandler(void(*inFunction)(const CrossPack*, CrossClientEntryPtr, NetTransMethod, NetTransError))
		{
			delete transErrorEvent;
			CrossSingleEvent<void, const CrossPack*, CrossClientEntryPtr, NetTransMethod, NetTransError>* newEvent = new CrossSingleEvent<void, const CrossPack*, CrossClientEntryPtr, NetTransMethod, NetTransError>();
			newEvent->SetCallback(inFunction);
			transErrorEvent = newEvent;
		}

		/* Set the client connected handler */
		template <class Class>
		void SetClientConnectedHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete connectEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			connectEvent = newEvent;
		}

		/* Set the client disconnected handler - any custom client data MUST be deleted with this handler */
		template <class Class>
		void SetClientDisconnectedHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete disconnectEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			disconnectEvent = newEvent;
		}

		/* Set the client reconnected succesfully handler*/
		template <class Class>
		void SetClientReconnectedHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete reconnectEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			reconnectEvent = newEvent;
		}

		/* Set the client reconnect failed handler*/
		template <class Class>
		void SetClientReconnectFailedHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete failedReconnectEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			failedReconnectEvent = newEvent;
		}

		/* Set the destroy client handler - this is necessary and should be used to delete any custom client data */
		template <class Class>
		void SetDestroyClientHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete destroyClientEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			destroyClientEvent = newEvent;
		}

		/* Set the initialize client handler - preferably this is where custom data should be created and assigned to this client

		if additonal data is necessary before the client can be initialized, then a custom event should be used.
		NOTE: if this is the case, please consider failed disconnects and client destroyed events (due to timeouts on reconnection) */
		template <class Class>
		void SetInitializeClientHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete initializeClientEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			initializeClientEvent = newEvent;
		}

		/* Set the client ready to transmit custom data handler */
		template <class Class>
		void SetClientReadyHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete readyEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			readyEvent = newEvent;
		}

		/* Set the client rejected from client handler*/
		template <class Class>
		void SetClientRejectedHandler(void(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete rejectEvent;
			CrossObjectEvent<Class, void, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, void, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			rejectEvent = newEvent;
		}

		/* Set the server listen socket bound handler */
		template <class Class>
		void SetServerBindHandler(void(Class::*inFunction)(), Class* object)
		{
			delete bindEvent;
			CrossObjectEvent<Class, void>* newEvent = new CrossObjectEvent<Class, void>();
			newEvent->SetCallback(inFunction, object);
			bindEvent = newEvent;
		}

		/* Set the client validation handler - clients are only connected if this handler returns true.  By default all clients are connected */
		template <class Class>
		void SetClientValidationHandler(bool(Class::*inFunction)(CrossClientEntryPtr), Class* object)
		{
			delete validateEvent;
			CrossObjectEvent<Class, bool, CrossClientEntryPtr>* newEvent = new CrossObjectEvent<Class, bool, CrossClientEntryPtr>();
			newEvent->SetCallback(inFunction, object);
			validateEvent = newEvent;
		}

		/* Set the receive any valid (known data ID and sender) handler */
		template <class Class>
		void SetReceiveDataHandler(void(Class::*inFunction)(CrossPack*, CrossClientEntryPtr, NetTransMethod), Class* object)
		{
			delete receiveEvent;
			CrossObjectEvent<Class, void, CrossPack*, CrossClientEntryPtr, NetTransMethod>* newEvent = new CrossObjectEvent<Class, void, CrossPack*, CrossClientEntryPtr, NetTransMethod>();
			newEvent->SetCallback(inFunction, object);
			receiveEvent = newEvent;
		}

		/* Set the transmit error handler - WARNING: depending on the error the packet or client may be null */
		template <class Class>
		void SetTransmitErrorHandler(void(Class::*inFunction)(CrossPack*, CrossClientEntryPtr, NetTransMethod, NetTransError), Class* object)
		{
			delete transErrorEvent;
			CrossObjectEvent<Class, void, CrossPack*, CrossClientEntryPtr, NetTransMethod, NetTransError>* newEvent = new CrossObjectEvent<Class, void, CrossPack*, CrossClientEntryPtr, NetTransMethod, NetTransError>();
			newEvent->SetCallback(inFunction, object);
			transErrorEvent = newEvent;
		}

	private:
		TCPSocketPtr listenSocket;
		UDPSocketPtr streamSocket;
		uint16_t port;
		CrossSockAddressFamily addressFamily;
		std::unordered_map<CrossClientID, CrossClientEntryPtr> connectedClients;
		std::unordered_map<CrossClientID, CrossClientEntryPtr> disconnectedClients;
		CrossClientID nextAvailableClientID;
		CrossClientID nextAvailableDataID;
		std::vector<CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>*> dataEvents;
		std::unordered_map<CrossPackDataID, CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>*> dataEventsByID;
		std::unordered_map<std::string, CrossDataEvent<const CrossPack*, CrossClientEntryPtr, NetTransMethod>*> dataEventsByName;
		CrossServerProperties serverProperties;
		CrossServerState serverState;
		CrossEvent<void, CrossClientEntryPtr>* connectEvent;
		CrossEvent<void, CrossClientEntryPtr>* disconnectEvent;
		CrossEvent<void, CrossClientEntryPtr>* reconnectEvent;
		CrossEvent<void, CrossClientEntryPtr>* readyEvent;
		CrossEvent<void, CrossClientEntryPtr>* rejectEvent;
		CrossEvent<void, CrossClientEntryPtr>* failedReconnectEvent;
		CrossEvent<void, CrossClientEntryPtr>* destroyClientEvent;
		CrossEvent<void, CrossClientEntryPtr>* initializeClientEvent;
		CrossEvent<void>* bindEvent;
		CrossEvent<bool, CrossClientEntryPtr>* validateEvent;
		CrossEvent<void, const CrossPack*, CrossClientEntryPtr, NetTransMethod>* receiveEvent;
		CrossEvent<void, const CrossPack*, CrossClientEntryPtr, NetTransMethod, NetTransError>* transErrorEvent;
		std::unordered_map<CrossSockAddress, bool> canConnectList;
		bool streamIsBound;
		CrossPackData udpBuffer[CROSS_SOCK_BUFFER_SIZE];
		CrossBufferLen udpBufferLength;
		CrossTimer alivenessTestTimer;

		/*
		 * receive function for when raw data is received - this is where the majority of
		 * the connection process is implemented. In addition, this function is responsible
		 * for calling custom data events.
		 */
		CrossBufferLen OnReceiveNewData(CrossPackData* inData, CrossBufferLen inLength, CrossClientEntryPtr inClient, NetTransMethod inMethod) {
			if (inLength >= CrossPack::GetHeaderSize()) {
				CrossPackHeader header = CrossPack::PeakHeader(inData);
				if (header.payloadSize > CrossPack::MAX_PAYLOAD_BYTES) {
					if (transErrorEvent && transErrorEvent->IsValid())
						transErrorEvent->Execute(nullptr, inClient, inMethod, NetTransError::INVALID_PAYLOAD_SIZE);
					return inLength;
				}
				if (header.payloadSize + CrossPack::GetHeaderSize() + CrossPack::GetFooterLength(header) <= inLength) {
					CrossPackFooter footer = CrossPack::PeakFooter(inData, header);
					CrossPack inPack(header, footer, inData);
					if (inMethod == NetTransMethod::UDP && inPack.GetPacketFlag(CrossPackFlagBit::UDP_SUPPORT_FLAG)) {
						inClient = GetClientEntry(inPack.GetSenderID());
					}
					if (inPack.GetDataID() == StaticDataID::HANDSHAKE && inClient) {
						if (inClient->state == CrossClientEntryState::CLIENT_ENTRY_DATA_LIST_EXCHANGE) {
							inClient->state = CrossClientEntryState::CLIENT_ENTRY_CONNECTED;
							if (readyEvent && readyEvent->IsValid())
								readyEvent->Execute(inClient);
						}
					}
					else if (inPack.GetDataID() == StaticDataID::INIT_CLIENT_ID && inClient) {

						// reset timeout
						inClient->ResetTimeout(CROSS_SOCK_MAX_TIMEOUT);

						// send aliveness test
						CrossPack alivenessTest;
						alivenessTest.SetDataID(StaticDataID::ALIVENESS_TEST);
						alivenessTest.AddToPayload<float>((float)((serverProperties.alivenessTestDelay + inClient->GetPing()) * CROSS_SOCK_TIMEOUT_FACTOR));
						SendToClient(&alivenessTest, inClient);

						CrossPack pack;
						pack.SetDataID(StaticDataID::INIT_CLIENT_ID);
						pack.AddToPayload<CrossClientID>(inClient->clientID);
						SendToClient(&pack, inClient);
						if (inClient->state == CrossClientEntryState::CLIENT_ENTRY_INIT)
							inClient->state = CrossClientEntryState::CLIENT_ENTRY_DATA_LIST_EXCHANGE;

						/* execute callbacks */
						if (connectEvent && connectEvent->IsValid())
							connectEvent->Execute(inClient);
						if (initializeClientEvent && initializeClientEvent->IsValid())
							initializeClientEvent->Execute(inClient);
					}
					else if (inPack.GetDataID() == StaticDataID::DISCONNECT_PACK && inClient) {
						DisconnectClient(inClient);
					}
					else if (inPack.GetDataID() == StaticDataID::RECONNECT_PACK && inClient) {

						// reset timeout
						inClient->ResetTimeout(CROSS_SOCK_MAX_TIMEOUT);

						// send aliveness test
						CrossPack alivenessTest;
						alivenessTest.SetDataID(StaticDataID::ALIVENESS_TEST);
						alivenessTest.AddToPayload<float>((float)(serverProperties.alivenessTestDelay * CROSS_SOCK_TIMEOUT_FACTOR));
						SendToClient(&alivenessTest, inClient);

						/* check if we can reconnect */
						CrossClientID oldID = inPack.RemoveFromPayload<CrossClientID>();
						auto client = connectedClients.find(oldID);
						if (oldID == 0 || client != connectedClients.end()) {
							CrossPack pack;
							pack.SetDataID(StaticDataID::INIT_CLIENT_ID);
							pack.AddToPayload<CrossClientID>(inClient->clientID);
							SendToClient(&pack, inClient);
							if (inClient->state == CrossClientEntryState::CLIENT_ENTRY_INIT)
								inClient->state = CrossClientEntryState::CLIENT_ENTRY_DATA_LIST_EXCHANGE;

							/* execute callbacks */
							if (failedReconnectEvent && failedReconnectEvent->IsValid())
								failedReconnectEvent->Execute(inClient);
							if (connectEvent && connectEvent->IsValid())
								connectEvent->Execute(inClient);
							if (initializeClientEvent && initializeClientEvent->IsValid())
								initializeClientEvent->Execute(inClient);
						}
						else { // else we can reconnect..

							// erase the client to update its id
							connectedClients.erase(inClient->clientID);

							// update this clients id and add back to the list
							inClient->clientID = oldID;
							connectedClients[oldID] = inClient;

							// try and find the old client in the disconnect list
							auto oldClient = disconnectedClients.find(oldID);
							if (oldClient != disconnectedClients.end() && oldClient->second) {

								// copy custom data and delete / remove from the disconnected clients list
								CrossClientEntryPtr oldClientEntry = oldClient->second;
								inClient->SetCustomData(oldClientEntry->GetCustomData<void>());
								disconnectedClients.erase(oldClient);
								oldClientEntry.reset();
							}
							else { // otherwise re-initialize this client
								if (initializeClientEvent && initializeClientEvent->IsValid())
									initializeClientEvent->Execute(inClient);
							}

							// finish reconnect and execute callback
							CrossPack pack;
							pack.SetDataID(StaticDataID::RECONNECT_PACK);
							pack.AddToPayload<CrossClientID>(oldID);
							SendToClient(&pack, inClient);
							if (inClient->state == CrossClientEntryState::CLIENT_ENTRY_INIT)
								inClient->state = CrossClientEntryState::CLIENT_ENTRY_DATA_LIST_EXCHANGE;
							if (reconnectEvent && reconnectEvent->IsValid())
								reconnectEvent->Execute(inClient);
						}
					}
					else if (inPack.GetDataID() == StaticDataID::INIT_CUSTOM_DATA_LIST && inClient) {
						CrossPackPayloadLen numTotalCustomData = (CrossPackPayloadLen)dataEvents.size();
						CrossPack outPack;
						outPack.SetDataID(StaticDataID::INIT_CUSTOM_DATA_LIST);
						for (CrossPackPayloadLen x = 0; x < numTotalCustomData; x++) {
							auto dataEvent = dataEvents[x];
							if (dataEvent) {
								outPack.ClearPayload();
								outPack.AddToPayload<CrossPackPayloadLen>(numTotalCustomData);
								outPack.AddToPayload<CrossPackPayloadLen>(x);
								outPack.AddStringToPayload(dataEvent->name);
								outPack.AddToPayload<CrossPackDataID>(dataEvent->dataID);
								SendToClient(&outPack, inClient);
							}
						}
					}
					else if (inPack.GetDataID() == StaticDataID::ALIVENESS_TEST && inClient) {

						/* reset timeout timer and update expected timeout delay */
						float timeoutDelay = inPack.RemoveFromPayload<float>();
						inClient->ResetTimeout(timeoutDelay);
					}
					else { /* custom or unknown data or unknown client */

						   /* call receive events if data ID / client is known and the packet is valid */
						if (inClient && inPack.GetDataID() != StaticDataID::UNKNOWN_PACK && (inMethod == NetTransMethod::TCP || inPack.IsValid())) {

							/* find the data event if it exists */
							auto dataEvent = dataEventsByID.find(inPack.GetDataID());

							/* call receive event if it is valid */
							if (receiveEvent && receiveEvent->IsValid()) {
								receiveEvent->Execute(&inPack, inClient, inMethod);
								inPack.Reset();
							}

							/* call custom event callbacks if any exists until finished or the client is disconnected */
							if (inClient && dataEvent != dataEventsByID.end()) {
								for (int c = (int)dataEvent->second->GetNumCallbacks() - 1; c >= 0; c--) {
									if (!IsRunning() || !inClient || !inClient->IsRunning()) {
										break;
									}
									dataEvent->second->Execute(c, &inPack, inClient, inMethod);
									inPack.Reset();
								}
							}
						}
						else { /* else data ID / client is unknown or checksum is invalid - call transmit error event */

							if (transErrorEvent && transErrorEvent->IsValid()) {
								NetTransError err;
								if (!inClient)
									err = NetTransError::CLIENT_NOT_FOUND;
								else if (inPack.GetDataID() == StaticDataID::UNKNOWN_PACK)
									err = NetTransError::INVALID_DATA_ID;
								else
									err = NetTransError::INVALID_CHECKSUM;
								transErrorEvent->Execute(&inPack, inClient, inMethod, err);
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
			dataEventsByID.clear();
			dataEventsByName.clear();
			for (size_t d = 0; d < dataEvents.size(); d++) {
				dataEventsByID[dataEvents[d]->dataID] = dataEvents[d];
				dataEventsByName[dataEvents[d]->name] = dataEvents[d];
			}
		}
	};

	typedef std::shared_ptr<CrossServer> CrossServerPtr;
}

#endif