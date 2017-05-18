

/**********************************************************************************************************
*  AUTHOR: Brandon Wilson  ********************************************************************************
*  A type-safe cross-platform header-only lightweight socket library developed on top of berkely sockets  *
**********************************************************************************************************/


#ifndef CROSS_SOCK_UTIL
#define CROSS_SOCK_UTIL


/*
 * This file includes all utilities needed by the socket, packet, and server-client API's.
 * Included is bit operations, a timer and sleep function with millisecond precision, 
 * endianness checking and conversion, and an ugly delegation implementation (see 
 * CrossEvent below for more information).
 */

/* Includes for system functions */
#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include "Windows.h" // for Sleep
#elif _POSIX_C_SOURCE >= 199309L
	#include <time.h>   // for nanosleep
#else
	#include <unistd.h> // for usleep
#endif

#include <string>
#include <vector>
#include <chrono>

namespace CrossSock {

	/* The buffer size for the cross server and cross client - 124 KB */
#define CROSS_SOCK_BUFFER_SIZE 65536

	/* The maximum size for each custom data name */
#define CROSS_SOCK_MAX_DATA_NAME_LENGTH 1024

	/* The amount of aliveness test failed before a timeout occurs */
#define CROSS_SOCK_TIMEOUT_FACTOR 3.1

	/* The default timeout duration that shouldn't be exceeded */
#define CROSS_SOCK_MAX_TIMEOUT 999999.0

	typedef int CrossBufferLen;
	typedef unsigned short CrossPackDataID;

	enum NetTransMethod
	{
		/* The reliable TCP protocol was used */
		TCP = 0,

		/* The unreliable UDP protocol was used */
		UDP = 1
	};

	enum NetTransError
	{
		/* No data to send / receive */
		NO_TRANSMIT = -1,

		/* No matching client was found */
		CLIENT_NOT_FOUND = -2,

		/* This client's streaming (UDP) socket has not been bound */
		STREAM_NOT_BOUND = -3,

		/* This client is disconnected */
		CLIENT_NOT_CONNECTED = -4,

		/* The packet's checksum does not match its payload */
		INVALID_CHECKSUM = -5,

		/* The packet's data ID is not handled or was sent as unknown */
		INVALID_DATA_ID = -6,

		/* The packet's payload size is greater than the maximum */
		INVALID_PAYLOAD_SIZE = -7
	};

	enum StaticDataID
	{
		/* Server to Client: rerquest that ends the connection state and begins the initialization process */
		HANDSHAKE = 0,

		/* Client to Server: Requests a new client ID
		* Server to Client: Send the requested client ID */
		INIT_CLIENT_ID = 1,

		/* Client to Server: Request a reconnect using the old client ID
		* Server to Client: Send the old client ID if still available, or a new client ID if unavailable */
		RECONNECT_PACK = 2,

		/* Client to Server: Notification that this client is disconnecting
		* Server to Client: Notification that the server is disconnecting this client */
		DISCONNECT_PACK = 3,

		/* Client to Server: Array of strings representing all available data in order
		* Server to Client: Array of data ID's given in the order they were sent */
		INIT_CUSTOM_DATA_LIST = 4,

		/* Unknown data ID - with fire the transmit error event and the receive event */
		UNKNOWN_PACK = 5,

		/* Aliveness test packet */
		ALIVENESS_TEST = 6,

		/* Starting data ID of the custom data list */
		CUSTOM_DATA_START = 7
	};

	/* Simple single event delegation */
	class CrossTimer {
	public:
		CrossTimer()
		{
			SetToNow();
		}

		/* Sets this timer to the current time */
		void SetToNow()
		{
			start = std::chrono::high_resolution_clock::now();
		}

		/* Gets the elapsed time since the previous 'SetToNow' in milliseconds */
		double GetElapsedTime() const
		{
			std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
			return (std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1000000.0);
		}

	private:
		std::chrono::high_resolution_clock::time_point start;
	};

	/* 
	 * Cross-platform delegation - this implementation is ugly but is capable of surviving
	 * garbage collection (i.e. useable in Unreal Engine 4). It isn't recommended to use
	 * this outside of the CrossClient and CrossServer. If CrossEvent is the desired
	 * delegation, see those two classes for general use. 
	 *
	 * Since CrossSock was designed with the VETO architecture in mind, it must be supported
	 * by game engines and other environments that often use garbage collection. As such,
	 * bare pointers are the expected use, as they will be ignored by garbage collection. This
	 * is also why it isn't recommended for CrossEvent's to be used outside of the CrossClient
	 * and CrossServer implementation, as bare pointers are ugly and risky if precautions aren't
	 * taken!
	 *
	 * NOTE: this implementation doesn't support handles to the delegates, as they aren't 
	 * necessary in the CrossClient and CrossServer implementations, and it isn't recommended
	 * for this delegation to be used elsewhere.
	 */
	template<class Return, class... Arguments>
	class CrossEvent {
	public:

		/* Virtual destructor */
		virtual ~CrossEvent() { }

		/* Execute this event - IsValid() must be checked before its use */
		virtual Return operator()(Arguments... args)
		{
			return Execute(args...);
		}

		/* Execute this event - IsValid() must be checked before its use */
		virtual Return Execute(Arguments... args) = 0;

		/* Checks for a valid callback event */
		virtual bool IsValid() const = 0;

	private:
		friend class CrossServer;
		friend class CrossClient;
	};

	/* Simple single event delegation */
	template<class Return, class... Arguments>
	class CrossSingleEvent : CrossEvent<Return, Arguments...> {
	public:

		CrossSingleEvent()
		{
			callback = nullptr;
		}

		~CrossSingleEvent() { }

		/* Execute this event - IsValid() must be checked before its use */
		Return operator()(Arguments... args)
		{
			return Execute(args...);
		}

		/* Execute this event - IsValid() must be checked before its use, explicit override */
		Return Execute(Arguments... args)
		{
			return (*callback)(args...);
		}

		/* Set this event to a function pointer */
		void SetCallback(Return(*function)(Arguments...))
		{
			callback = function;
		}

		/* Checks for a valid callback event, explicit override */
		bool IsValid() const
		{
			return (callback != nullptr);
		}

	private:
		friend class CrossServer;
		friend class CrossClient;

		Return(*callback)(Arguments...);
	};

	/* Single event delegation of a member function for a given object of type Class  */
	template<class Class, class Return, class... Arguments>
	class CrossObjectEvent : CrossEvent<Return, Arguments...> {
	public:

		CrossObjectEvent()
		{
			callback = nullptr;
			object = nullptr;
		}

		~CrossObjectEvent() { }

		/* Execute this event - IsValid() must be checked before its use */
		Return operator()(Arguments... args)
		{
			return Execute(args...);
		}

		/* Execute this event - IsValid() must be checked before its use, explicit override */
		Return Execute(Arguments... args)
		{
			return (object->*callback)(args...);
		}

		/* Set this event to a function pointer */
		void SetCallback(Return(Class::*function)(Arguments...), Class* objPtr)
		{
			callback = function;
			object = objPtr;
		}

		/* Checks for a valid callback event, explicit override */
		bool IsValid() const
		{
			return (callback != nullptr && object != nullptr);
		}

	private:
		friend class CrossServer;
		friend class CrossClient;

		Class* object;
		Return(Class::*callback)(Arguments...);
	};

	/* Multiple event delegation */
	template<class... Arguments>
	class CrossDataEvent {
	public:
		
		CrossDataEvent()
		{
			name = "";
		}

		CrossDataEvent(std::string inName)
		{
			name = inName;
		}

		~CrossDataEvent()
		{
			for (int x = (int)callbacks.size() - 1; x >= 0; x--) {
				CrossEvent<void, Arguments...>* event = callbacks[x];
				callbacks.erase(callbacks.begin() + x);
				delete event;
			}
		}

		bool operator==(const CrossDataEvent& inOther) const
		{
			if (dataID != StaticDataID::UNKNOWN_PACK)
				return (name == inOther.name);
			return (dataID == inOther.dataID && dataID);
		}

		bool operator==(const std::string& inStr) const
		{
			return (name == inStr);
		}

		bool operator==(const CrossPackDataID& inDataID) const
		{
			return (dataID == inDataID);
		}

		/* Execute this event - {GetNumCallbacks() > 0} be checked before its use */
		void operator()(Arguments... args)
		{
			Execute(args...);
		}

		/* Execute this event - {GetNumCallbacks() > 0} be checked before its use */
		void Execute(int callback, Arguments... args)
		{
			if(callback < (int)callbacks.size()) {
				(*callbacks[callback])(args...);
			}
		}

		/* Add a function pointer to this event's callback list */
		void AddCallback(void(*function)(Arguments...))
		{
			if (function != nullptr) {
				CrossSingleEvent<void, Arguments...>* newEvent = new CrossSingleEvent<void, Arguments...>();
				newEvent->SetCallback(function);
				callbacks.push_back((CrossEvent<void, Arguments...>*)newEvent);
			}
		}

		/* Add a member function pointer to this event's callback list using the given object of type Class */
		template <class Class>
		void AddObjectCallback(void(Class::*function)(Arguments...), Class* object)
		{
			if (function != nullptr && object != nullptr) {
				CrossObjectEvent<Class, void, Arguments...>* newEvent = new CrossObjectEvent<Class, void, Arguments...>();
				newEvent->SetCallback(function, object);
				callbacks.push_back((CrossEvent<void, Arguments...>*)newEvent);
			}
		}

		/* Set this event's name */
		void SetName(const std::string& inName)
		{
			name = inName;
		}

		/* Get this event's name */
		void GetName() const
		{
			return name;
		}

		/* Get this event's unique data ID*/
		CrossPackDataID GetDataID() const
		{
			return dataID;
		}

		/* Set this event's unique data ID */
		void SetDataID(CrossPackDataID inDataID)
		{
			dataID = inDataID;
		}

		/* Get the number of callbacks for this event */
		size_t GetNumCallbacks() const
		{
			return callbacks.size();
		}

		/* Gets the key for this event */
		size_t Hash() const 
		{
			size_t key = dataID;
			key = (key ^ (key >> 30)) * size_t(0xbf58476d1ce4e5b9);
			key = (key ^ (key >> 27)) * size_t(0x94d049bb133111eb);
			key = key ^ (key >> 31);
			return key;
		}

	private:
		friend class CrossServer;
		friend class CrossClient;

		std::string name;
		CrossPackDataID dataID;
		std::vector<CrossEvent<void, Arguments...>*> callbacks;
	};

	class CrossSysUtil
	{
	public:

		/* Returns true if on a little endian system; false otherwise */
		static bool IsLittleEndian()
		{
			int n = 1;
			if (*(char *)&n == 1)
				return true;
			return false;
		}

		/* Utility function that flips packed data - useful when the endianness is opposite on a given system */
		template <class T>
		static void SwapEndian(T* inData, const int& inStart, const int& inLength)
		{
			for (int i = inStart, j = i + inLength - 1; i < j; ++i, --j)
			{
				T temp = inData[i];
				inData[i] = inData[j];
				inData[j] = temp;
			}
		}

		/* Utility function that returns the given number with the xth bit set */
		template <class T>
		static T SetBit(T inNumber, const int& inX)
		{
			return (inNumber |= 1 << inX);
		}

		/* Utility function that returns the given number with the xth bit cleared */
		template <class T>
		static T ClearBit(T inNumber, const int& inX)
		{
			return (inNumber &= ~(1 << inX));
		}

		/* Utility function that returns the given number with the xth bit toggled */
		template <class T>
		static T ToggleBit(T inNumber, const int& inX)
		{
			return (inNumber ^= 1 << inX);
		}

		/* Utility function that returns true if the xth bit is set - false otherwise */
		template <class T>
		static bool CheckBit(T inNumber, const int& inX)
		{
			return (((inNumber >> inX) & 1) != 0);
		}

		/* Utility function that returns the value of the state extracted from a flag starting from the start-th bit and ending on the end-th bit */
		template <class T>
		static T GetStateFromFlag(T inFlag, const int& inStart, const int& inEnd)
		{
			T out = 0;
			for (int x = 0; x <= inEnd - inStart; x++) {
				if (CheckBit(inFlag, x + inStart))
					out = SetBit(out, x);
			}
			return out;
		}

		/* Utility function that returns the value of the state extracted from a flag starting from the start-th bit and ending on the end-th bit */
		template <class T>
		static T SetStateToFlag(T inFlag, T inState, const int& inStart, const int& inEnd)
		{
			for (int x = 0; x <= inEnd - inStart; x++) {
				if (CheckBit(inState, x))
					inFlag = SetBit(inFlag, x + inStart);
				else
					inFlag = ClearBit(inFlag, x + inStart);
			}
			return inFlag;
		}

		/* Utility function that deactivates this thread for a given number of milliseconds */
		static void SleepMS(int milliseconds)
		{
#ifdef WIN32
		    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
		    struct timespec ts;
		    ts.tv_sec = milliseconds / 1000;
		    ts.tv_nsec = (milliseconds % 1000) * 1000000;
		    nanosleep(&ts, NULL);
#else
		    usleep(milliseconds * 1000);
#endif
		}
	};
}

#endif
