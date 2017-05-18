
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
#include "CrossServer.h"

using namespace std;
using namespace CrossSock;

#define LISTEN_PORT 7425 // TODO: change if your server is using a different port

/* Global pointer to the server object */
CrossServer* myServer;

/* Custom data class which servers an an example of how to add custom data to clients */
class CustomClientData {
public:
	unsigned int numMessages;

	CustomClientData()
	{
		numMessages = 0;
	}
};

/* Handler for receiving the custom "message" data */
void HandleMessage(const CrossPack* pack, CrossClientEntryPtr client, NetTransMethod method)
{
	/* update custom data */
	client->GetCustomData<CustomClientData>()->numMessages++;

	/* print message to console */
	printf("New message #%u via %s: %s\n", 
		client->GetCustomData<CustomClientData>()->numMessages, 
		(method == NetTransMethod::TCP ? "TCP" : "UDP"), 
		pack->RemoveStringFromPayload().c_str());

	/* send response packet to client */
	CrossPackPtr outPack = myServer->CreatePack("message");
	outPack->AddStringToPayload("Ty for the message my dude");
	outPack->AddToPayload<unsigned int>(client->GetCustomData<CustomClientData>()->numMessages);
	outPack->Finalize(true, method == NetTransMethod::UDP, myServer->GetServerID());
	if(method == NetTransMethod::TCP)
		myServer->SendToClient(outPack, client);
	else
		myServer->StreamToClient(outPack, client);
}

/* On server bind event */
void HandleBind()
{
	printf("Server Ready!\n");
}

/* On client connected event */
void HandleNewClient(CrossClientEntryPtr client)
{
	printf("New client connected with ID: %d\n", client->GetClientID());
}

/* On client disconnected event */
void HandleDisconnect(CrossClientEntryPtr client)
{
	printf("Client disconnected with ID: %d\n", client->GetClientID()); 
}

/* On client reconnect event */
void HandleReconnect(CrossClientEntryPtr client)
{
	printf("Client reconnected with ID: %d\n", client->GetClientID());
}

/* On client failed reconnect event */
void HandleFailedReconnect(CrossClientEntryPtr client)
{
	printf("Client failed to reconnect with ID: %d, reinitializing..\n", client->GetClientID());
}

/* On initialize custom client data event */
void HandleInitializeClient(CrossClientEntryPtr client)
{
	client->SetCustomData(new CustomClientData);
}

/* On destroy custom client data event */
void HandleDestroyClient(CrossClientEntryPtr client)
{
	CustomClientData* clientData = client->GetCustomData<CustomClientData>();
	delete clientData;
}

/* On transmit error event */
void HandleTransmitError(const CrossPack* pack, CrossClientEntryPtr client, NetTransMethod method, NetTransError error)
{
	printf("Transfer error received via %s\n", (method == NetTransMethod::TCP ? "TCP" : "UDP"));
}

int main()
{
	/* Mandatory cross sock initialization */
	CrossSockUtil::Init();

	/* Set server events */
	CrossServer server;
	myServer = &server;
	server.SetServerBindHandler(&HandleBind);
	server.SetClientConnectedHandler(&HandleNewClient);
	server.SetClientDisconnectedHandler(&HandleDisconnect);
	server.SetClientReconnectedHandler(&HandleReconnect);
	server.SetClientReconnectFailedHandler(&HandleFailedReconnect);
	server.SetInitializeClientHandler(&HandleInitializeClient);
	server.SetDestroyClientHandler(&HandleDestroyClient);
	server.SetTransmitErrorHandler(&HandleTransmitError);

	/* Add data handlers */
	server.AddDataHandler("message", &HandleMessage);

	/* Start the server on listen port */
	server.Start(LISTEN_PORT);

	/* While the server is running */
	while (server.IsRunning()) {

		/* Update the server, which automatically receives incoming data and connects to new clients */
		server.Update();
	}

	/* Mandatory cross sock cleanup */
	CrossSockUtil::CleanUp();
	return 0;
}