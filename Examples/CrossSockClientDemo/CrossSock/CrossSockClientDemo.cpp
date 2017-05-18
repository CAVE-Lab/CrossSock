
/**********************************************************************************************************
*  AUTHOR: Brandon Wilson  ********************************************************************************
*  A type-safe cross-platform header-only lightweight socket library developed on top of berkely sockets  *
**********************************************************************************************************/

/*
 * This file is meant to be used as an example and/or starting point for all CrossSock projects.
 * In this example, a simple clilent is shown.  The client contains a single data handler, that  
 * passes data ("messages") between this server and all clients. These messages originate on each
 * client and are sent to the server.  The server responds to each message with a follow-up   
 * message, using the same protocol as the original message.
 */

#include <stdio.h>
#include <iostream>
#include "CrossClient.h"

using namespace std;
using namespace CrossSock;

#define SERVER_ADDRESS "192.168.1.100:7425" // TODO: change to your servers address

/* Global client pointer */
CrossClient* myClient;

/* Handler for receiving the custom "message" data */
void HandleMessage(const CrossPack* pack, NetTransMethod method)
{
	/* read from the packet */
	std::string message = pack->RemoveStringFromPayload();
	unsigned int messageNum = pack->RemoveFromPayload<unsigned int>();

	/* print message to console */
	printf("Response message #%u via %s: %s\n",
		messageNum,
		(method == NetTransMethod::TCP ? "TCP" : "UDP"),
		message.c_str());
}

/* On ready event */
void HandleReady()
{
	printf("Ready to transmit!\n");
}

/* On disconnect event */
void HandleDisconnect()
{
	printf("Failed to connect/reconnect. Exiting..\n");
}

/* On trying to reconnect event */
void HandleAttemptReconnect()
{
	printf("Disconnected from server. Attempting to reconnect..\n");
}

/* On succesful reconnect event */
void HandleSuccesfulReconnect()
{
	printf("Reconnected to server! Re-initializing..\n");
}

/* On failed reconnect event */
void HandleFailedReconnect()
{
	printf("Failed to reconnect! Reconnecting and initializing..\n");
}

/* On connect to server event */
void HandleConnect()
{
	printf("Connected to server with ID: %d! Initializing..\n", myClient->GetClientID());
}

/* On receive initial handshake request event */
void HandleHandshake()
{
	if (myClient->GetClientState() == CrossClientState::CLIENT_REQUESTING_ID)
		printf("Requesting old ID..\n");
	else
		printf("Requesting new ID..\n");
}

void HandleTransmitError(const CrossPack* pack, NetTransMethod method, NetTransError error)
{
	printf("Transfer error received via %s\n", (method == NetTransMethod::TCP ? "TCP" : "UDP"));
}

int main()
{
	/* Mandatory cross sock initialization */
	CrossSockUtil::Init();

	/* Set client properties */
	CrossClientProperties props;
	props.maxConnectionAttempts = 10;
	props.maxReconnectionAttempts = 999;

	/* Set client events*/
	CrossClient client(props);
	myClient = &client;
	client.SetReadyHandler(&HandleReady);
	client.SetDisconnectHandler(&HandleDisconnect);
	client.SetAttemptReconnectHandler(&HandleAttemptReconnect);
	client.SetReconnectHandler(&HandleSuccesfulReconnect);
	client.SetReconnectFailedHandler(&HandleFailedReconnect);
	client.SetConnectHandler(&HandleConnect);
	client.SetHandshakeHandler(&HandleHandshake);
	client.SetTransmitErrorHandler(&HandleTransmitError);

	/* Add data events */
	client.AddDataHandler("message", &HandleMessage);

	/* Connect to the server using its string address in the format "b1.b2.b3.b4:port" */
	client.Connect(SERVER_ADDRESS);

	/* While the client is running */
	while (client.IsRunning())
	{
		/* Update the client, which automatically receives incoming data and reconnects to the server */
		client.Update();

		/* If the client is connected - send and stream a message */
		if (client.IsReady()) {
			CrossPackPtr pack = client.CreatePack("message");
			pack->AddStringToPayload("Wassup?");
			client.SendToServer(pack);
			if (client.IsStreamBound()) {
				pack->Finalize(false, true, client.GetClientID());
				client.StreamToServer(pack);
			}
		}
	}

	/* Mandatory cross sock deinitialization */
	CrossSockUtil::CleanUp();
	return 0;
}
