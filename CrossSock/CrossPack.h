

/**********************************************************************************************************
*  AUTHOR: Brandon Wilson  ********************************************************************************
*  A type-safe cross-platform header-only lightweight socket library developed on top of berkely sockets  *
**********************************************************************************************************/


#ifndef CROSS_SOCK_PACK
#define CROSS_SOCK_PACK

/*
 * The packet API - used by the client-server impelentations for easy data serialization and transmition
 *
 * Data is always added in little endian format, and when removed is converted automatically (by default)
 * translated to the native endianness of the machine.
 *
 * This class uses RAII standards - as such, it is wise to use smart pointers to avoid deleting the data
 * when a packet is created in a lower scope.
 *
 * Packets have a unique data ID to denote which data handler should be used to access the data. In addition,
 * They have an integer (1 byte) flag to denote the state of the packet, such as any data contained in its
 * footer. Packets are serialized only once unless they are altered for efficiency. In addition packets can
 * be 'finalized' to prevent alterations. This step can also include a client ID and custom checksum. Adding
 * an client ID is mandatory for UDP transmitions, as the server needs some way to correlate a packet to its
 * sender!
 *
 * Also, it should be noted that removing data from the payload (buffer) doesn't actually remove it.  Instead,
 * data is simply 'popped' from an array in a stack fashion.  This data can be reset using the Reset() function
 * to begin reading from the front again. Likewise, the Clear() function will drop the payload size to 0 and
 * effectively remove all data from the payload, but the actual memory allocation doesn't change until the
 * destructor!
 */

#include <memory>
#include "CrossUtil.h"

namespace CrossSock {

	enum CrossPackError
	{
		/* The operation yielded a valid payload size */
		VALID_DATA = 0,

		/* The operation failed due to the payload size being too big/small */
		INVALID_DATA_SIZE = -1,

		/* The operation failed due to the packet being finalized */
		HAS_BEEN_FINALIZED = -2
	};

	enum CrossPackFlagBit
	{
		/* Flag indicating that this packet has a checksum in its footer */
		CHECKSUM_FLAG = 0,

		/* Flag indicating that this packet has a client ID in its footer */
		UDP_SUPPORT_FLAG = 1,

		/* Custom user flag - WARNING: support for custom header flags may change in future updates, as support for additional features may change */
		CUSTOM_FLAG_1 = 2,

		/* Custom user flag - WARNING: support for custom header flags may change in future updates, as support for additional features may change */
		CUSTOM_FLAG_2 = 3,

		/* Custom user flag - WARNING: support for custom header flags may change in future updates, as support for additional features may change */
		CUSTOM_FLAG_3 = 4,

		/* Custom user flag - WARNING: support for custom header flags may change in future updates, as support for additional features may change */
		CUSTOM_FLAG_4 = 5,

		/* Custom user flag - WARNING: support for custom header flags may change in future updates, as support for additional features may change */
		CUSTOM_FLAG_5 = 6,

		/* Custom user flag - WARNING: support for custom header flags may change in future updates, as support for additional features may change */
		CUSTOM_FLAG_6 = 7
	};

	typedef unsigned int CrossClientID;
	typedef char CrossPackData;
	typedef unsigned short CrossPackPayloadLen;
	typedef char CrossPackFlag;
	typedef int CrossPackChecksum;

	struct CrossPackHeader
	{
	public:
		CrossPackDataID dataID;
		CrossPackPayloadLen payloadSize;
		CrossPackFlag packFlags;

		CrossPackHeader()
		{
			dataID = StaticDataID::UNKNOWN_PACK;
			payloadSize = 0;
			packFlags = 0;
		}
	};

	struct CrossPackFooter
	{
	public:
		CrossPackChecksum checksum;
		CrossClientID senderID;

		CrossPackFooter()
		{
			checksum = 0;
			senderID = 0;
		}
	};

	/* High level packet class - WARNING: this class automatically handles deletion of its internal buffer and must be used with caution when passed to different scopes */
	class CrossPack {
	public:

		/* Empty constructor */
		CrossPack()
		{
			payload = new CrossPackData[GetMaxPacketSize()];
			removeIdx = 0;
			shouldDeleteData = true;
			hasBeenFinalized = false;
			hasBeenSerialized = false;
		}

		/* Copy constructor */
		CrossPack(const CrossPack& inPack)
		{
			payload = new CrossPackData[GetMaxPacketSize()];
			removeIdx = 0;
			shouldDeleteData = true;
			Copy(inPack);
		}

		/* Deserialization constructor */
		CrossPack(CrossPackData* data)
		{
			header = CrossPack::PeakHeader(data);
			footer = CrossPack::PeakFooter(data, header);
			payload = data;
			removeIdx = 0;
			shouldDeleteData = false;
			hasBeenFinalized = false;
			hasBeenSerialized = false;
		}

		/* Component constructor */
		CrossPack(CrossPackDataID inDataID)
		{
			header.dataID = inDataID;
			payload = new CrossPackData[GetMaxPacketSize()];
			removeIdx = 0;
			shouldDeleteData = true;
			hasBeenFinalized = false;
			hasBeenSerialized = false;
		}

		/* Component constructor */
		CrossPack(CrossPackHeader& inHeader, CrossPackFooter& inFooter, CrossPackData* inPayload)
		{
			header = inHeader;
			footer = inFooter;
			payload = inPayload;
			removeIdx = 0;
			shouldDeleteData = false;
			hasBeenFinalized = false;
			hasBeenSerialized = false;
		}

		/* Destructor */
		~CrossPack()
		{
			if (shouldDeleteData) {
				shouldDeleteData = false;
				delete[] payload;
			}
		}

		/* Copy an existing packet */
		void Copy(const CrossPack& inPack)
		{
			header.dataID = inPack.header.dataID;
			header.payloadSize = inPack.header.payloadSize;
			header.packFlags = inPack.header.packFlags;
			footer.checksum = inPack.footer.checksum;
			footer.senderID = inPack.footer.senderID;
			memcpy(payload + GetHeaderSize(), inPack.payload + GetHeaderSize(), header.payloadSize);
			removeIdx = inPack.removeIdx;
			hasBeenFinalized = inPack.hasBeenFinalized;
			hasBeenSerialized = false;
		}

		/* Copy assignment operator */
		CrossPack& operator= (const CrossPack& inPack)
		{
			/* allocate memory if necessary */
			if (!shouldDeleteData) {
				payload = new CrossPackData[GetMaxPacketSize()];
				shouldDeleteData = true;
			}

			/* do the copy */
			removeIdx = 0;
			Copy(inPack);
		}

		/* Utility function to peak at the packet header from raw data */
		static CrossPackHeader PeakHeader(const CrossPackData* inData)
		{
			CrossPackHeader outHeader;
			CrossPackData headerData[sizeof(CrossPackHeader)];
			memcpy(headerData, inData, sizeof(CrossPackHeader));
			if (!CrossSysUtil::IsLittleEndian()) {
				CrossSysUtil::SwapEndian(headerData, 0, sizeof(CrossPackDataID));
				CrossSysUtil::SwapEndian(headerData, sizeof(CrossPackDataID), sizeof(CrossPackPayloadLen));
				CrossSysUtil::SwapEndian(headerData, sizeof(CrossPackDataID) + sizeof(CrossPackPayloadLen), sizeof(CrossPackFlag));
			}
			memcpy(&outHeader.dataID, headerData, sizeof(CrossPackDataID));
			memcpy(&outHeader.payloadSize, headerData + sizeof(CrossPackDataID), sizeof(CrossPackPayloadLen));
			memcpy(&outHeader.packFlags, headerData + sizeof(CrossPackDataID) + sizeof(CrossPackPayloadLen), sizeof(CrossPackFlag));
			return outHeader;
		}

		/* Utility function that returns the packet's footer length from its header flags */
		static CrossPackPayloadLen GetFooterLength(const CrossPackHeader& inHeader)
		{
			return (CrossSysUtil::CheckBit(inHeader.packFlags, CrossPackFlagBit::CHECKSUM_FLAG) ? sizeof(CrossPackChecksum) : 0)
				+ (CrossSysUtil::CheckBit(inHeader.packFlags, CrossPackFlagBit::UDP_SUPPORT_FLAG) ? sizeof(CrossClientID) : 0);
		}

		/* Utility function to peak at the packet footer from raw data given the packet's header  */
		static CrossPackFooter PeakFooter(const CrossPackData* inData, const CrossPackHeader& inHeader)
		{
			CrossPackFooter outFooter;
			CrossPackData footerData[sizeof(CrossPackFooter)];
			CrossPackPayloadLen footerStart = sizeof(CrossPackHeader) + inHeader.payloadSize;
			CrossPackPayloadLen footerSize = (CrossSysUtil::CheckBit(inHeader.packFlags, CrossPackFlagBit::CHECKSUM_FLAG) ? sizeof(CrossPackChecksum) : 0)
				+ (CrossSysUtil::CheckBit(inHeader.packFlags, CrossPackFlagBit::UDP_SUPPORT_FLAG) ? sizeof(CrossClientID) : 0);
			memcpy(footerData, inData + footerStart, footerSize);
			CrossPackPayloadLen offset = 0;
			if (CrossSysUtil::CheckBit(inHeader.packFlags, CrossPackFlagBit::CHECKSUM_FLAG)) {
				if (!CrossSysUtil::IsLittleEndian())
					CrossSysUtil::SwapEndian(footerData, 0, sizeof(CrossPackChecksum));
				memcpy(&outFooter.checksum, footerData, sizeof(CrossPackChecksum));
				offset += sizeof(CrossPackChecksum);
			}
			if (CrossSysUtil::CheckBit(inHeader.packFlags, CrossPackFlagBit::UDP_SUPPORT_FLAG)) {
				if (!CrossSysUtil::IsLittleEndian())
					CrossSysUtil::SwapEndian(footerData, offset, sizeof(CrossClientID));
				memcpy(&outFooter.senderID, footerData + offset, sizeof(CrossClientID));
			}
			return outFooter;
		}

		/* Add data type to payload */
		template<class T>
		CrossPackError AddToPayload(T data, bool autoEndianSwap = true)
		{
			if (header.payloadSize + sizeof(T) > MAX_PAYLOAD_BYTES)
				return CrossPackError::INVALID_DATA_SIZE;
			memcpy(payload + header.payloadSize + GetHeaderSize(), &data, sizeof(T));
			if (autoEndianSwap && !CrossSysUtil::IsLittleEndian()) {
				CrossSysUtil::SwapEndian(payload, header.payloadSize + GetHeaderSize(), sizeof(T));
			}
			header.payloadSize += sizeof(T);
			hasBeenSerialized = false;
			return CrossPackError::VALID_DATA;
		}

		/* Add raw data to payload */
		CrossPackError AddDataToPayload(const CrossPackData* inData, CrossPackPayloadLen length, bool autoEndianSwap = false)
		{
			if (header.payloadSize + length > MAX_PAYLOAD_BYTES)
				return CrossPackError::INVALID_DATA_SIZE;
			memcpy(payload + header.payloadSize + GetHeaderSize(), inData, length);
			if (autoEndianSwap && !CrossSysUtil::IsLittleEndian()) {
				CrossSysUtil::SwapEndian(payload, header.payloadSize + GetHeaderSize(), length);
			}
			header.payloadSize += length;
			hasBeenSerialized = false;
			return CrossPackError::VALID_DATA;
		}

		/* Add string (CrossPackPayloadLen{string length} + char*{string}) to payload */
		CrossPackError AddStringToPayload(std::string inStr)
		{
			if (header.payloadSize + inStr.length() + sizeof(CrossPackPayloadLen) > MAX_PAYLOAD_BYTES) {
				return CrossPackError::INVALID_DATA_SIZE;
			}
			if (AddToPayload<CrossPackPayloadLen>((CrossPackPayloadLen)inStr.length()) != VALID_DATA) {
				return CrossPackError::INVALID_DATA_SIZE;
			}
			if (AddDataToPayload((CrossPackData*)inStr.c_str(), (CrossPackPayloadLen)inStr.length()) != VALID_DATA) {
				header.payloadSize -= sizeof(CrossPackPayloadLen);
				return CrossPackError::INVALID_DATA_SIZE;
			}
			return CrossPackError::VALID_DATA;
		}

		/* Remove data type from payload */
		template<class T>
		T RemoveFromPayload(bool autoEndianSwap = true) const
		{
			if (header.payloadSize < sizeof(T) + removeIdx)
				return 0;
			if (autoEndianSwap && !CrossSysUtil::IsLittleEndian()) {
				CrossSysUtil::SwapEndian(payload, GetHeaderSize() + removeIdx, sizeof(T));
			}
			T Output;
			memcpy(&Output, payload + (GetHeaderSize() + removeIdx), sizeof(T));
			removeIdx += sizeof(T);
			return Output;
		}


		/* Remove raw data from payload */
		CrossPackError RemoveDataFromPayload(CrossPackData* outData, CrossPackPayloadLen length, bool autoEndianSwap = false) const
		{
			if (header.payloadSize < length + removeIdx)
				return CrossPackError::INVALID_DATA_SIZE;
			if (autoEndianSwap && !CrossSysUtil::IsLittleEndian()) {
				CrossSysUtil::SwapEndian(payload, GetHeaderSize() + removeIdx, length);
			}
			memcpy(outData, payload + (GetHeaderSize() + removeIdx), length);
			removeIdx += length;
			return CrossPackError::VALID_DATA;
		}

		/* Remove string (CrossPackPayloadLen{string length} + char*{string}) from payload */
		std::string RemoveStringFromPayload() const
		{
			if (header.payloadSize < sizeof(CrossPackPayloadLen)) {
				return "";
			}
			CrossPackPayloadLen length = RemoveFromPayload<CrossPackPayloadLen>();
			if (header.payloadSize < length) {
				return "";
			}
			CrossPackData* outBuf = new CrossPackData[((int)length)+1];
			if (!outBuf) {
				return "";
			}
			if (RemoveDataFromPayload(outBuf, length) == CrossPackError::VALID_DATA) {
				outBuf[length] = '\0';
				std::string outStr(outBuf);
				delete outBuf;
				return outStr;
			}
			if (outBuf) {
				delete outBuf;
			}
			return "";
		}

		/* Returns this packet's payload size in bytes */
		CrossPackPayloadLen GetPayloadSize() const
		{
			return header.payloadSize;
		}

		/* Returns the static packet header size in bytes */
		static CrossPackPayloadLen GetHeaderSize()
		{
			return sizeof(CrossPackHeader);
		}

		/* Returns the maximum possible packet size in bytes - including the header, payload, and footer */
		static CrossPackPayloadLen GetMaxPacketSize()
		{
			return (sizeof(CrossPackHeader) + MAX_PAYLOAD_BYTES + sizeof(CrossPackFooter));
		}

		/* Returns this packet's size in bytes */
		CrossPackPayloadLen GetPacketSize() const
		{
			return (GetPayloadSize() + GetHeaderSize() + GetFooterSize());
		}

		/* Returns the size of the remaining data in this packet's payload in bytes*/
		CrossPackPayloadLen GetRemainingPayloadSize() const
		{
			return (GetPayloadSize() - removeIdx);
		}

		/* Returns the size of this packet's footer in bytes*/
		CrossPackPayloadLen GetFooterSize() const
		{
			return CrossPack::GetFooterLength(header);
		}

		/* Serialization for transfer across the network */
		const CrossPackData* Serialize() const
		{
			if (!hasBeenSerialized) {
				memcpy(payload, &header.dataID, sizeof(CrossPackDataID));
				memcpy(payload + sizeof(CrossPackDataID), &header.payloadSize, sizeof(CrossPackPayloadLen));
				memcpy(payload + sizeof(CrossPackDataID) + sizeof(CrossPackPayloadLen), &header.packFlags, sizeof(CrossPackFlag));
				if (GetPacketFlag(CrossPackFlagBit::CHECKSUM_FLAG)) {
					memcpy(payload + GetHeaderSize() + header.payloadSize, &footer.checksum, sizeof(CrossPackChecksum));
					if(GetPacketFlag(CrossPackFlagBit::UDP_SUPPORT_FLAG))
						memcpy(payload + GetHeaderSize() + header.payloadSize + sizeof(CrossPackChecksum), &footer.senderID, sizeof(CrossClientID));
				}
				else if (GetPacketFlag(CrossPackFlagBit::UDP_SUPPORT_FLAG)) {
					memcpy(payload + GetHeaderSize() + header.payloadSize, &footer.senderID, sizeof(CrossClientID));
				}
				if (!CrossSysUtil::IsLittleEndian()) {
					CrossSysUtil::SwapEndian(payload, 0, sizeof(CrossPackDataID));
					CrossSysUtil::SwapEndian(payload, sizeof(CrossPackDataID), sizeof(CrossPackPayloadLen));
					CrossSysUtil::SwapEndian(payload, sizeof(CrossPackDataID) + sizeof(CrossPackPayloadLen), sizeof(CrossPackFlag));
					if (GetPacketFlag(CrossPackFlagBit::CHECKSUM_FLAG)) {
						CrossSysUtil::SwapEndian(payload, GetHeaderSize() + header.payloadSize, sizeof(CrossPackChecksum));
						if (GetPacketFlag(CrossPackFlagBit::UDP_SUPPORT_FLAG))
							CrossSysUtil::SwapEndian(payload, GetHeaderSize() + header.payloadSize + sizeof(CrossPackChecksum), sizeof(CrossClientID));
					}
					else if (GetPacketFlag(CrossPackFlagBit::UDP_SUPPORT_FLAG)) {
						CrossSysUtil::SwapEndian(payload, GetHeaderSize() + header.payloadSize, sizeof(CrossClientID));
					}
				}
				hasBeenSerialized = true;
			};
			return payload;
		}

		/* Set this packet's data ID */
		void SetDataID(CrossPackDataID inDataID)
		{
			header.dataID = inDataID;
		}

		/* Get this packet's data ID */
		CrossPackDataID GetDataID() const
		{
			return header.dataID;
		}

		/* Clear this packet's payload data */
		void ClearPayload()
		{
			Reset();
			header.payloadSize = 0;
			hasBeenFinalized = false;
			hasBeenSerialized = false;
		}

		/* Get if this packet has been finalized */
		bool IsFinalized() const
		{
			return hasBeenFinalized;
		}

		/* Finalize this packet
		 *
		 *	 AddChecksum:   if true - a checksum will be added to this packet's footer
		 *	 AddUDPSupport: if true - the given client ID will be added to this packet's footer
		 */
		void Finalize(bool AddChecksum = true, bool AddUDPSupport = false, CrossClientID inSenderID = 0) const
		{
			// lower finalized flag to allow for setting packet flags
			hasBeenFinalized = false;

			// set packet flags
			SetPacketFlag(CrossPackFlagBit::CHECKSUM_FLAG, AddChecksum);
			SetPacketFlag(CrossPackFlagBit::UDP_SUPPORT_FLAG, AddUDPSupport);

			// set footer data
			if (AddUDPSupport)
				footer.senderID = inSenderID;
			if (AddChecksum)
				footer.checksum = CalculateChecksum();

			// set packet states
			hasBeenFinalized = true;
			hasBeenSerialized = false;
		}

		/* Calculate this packet's checksum */
		CrossPackChecksum CalculateChecksum() const
		{
			CrossPackChecksum outChecksum = 0;
			for (int x = 0; x < header.payloadSize; x++) {
				outChecksum += payload[x + GetHeaderSize()];
			}
			outChecksum += header.dataID;
			outChecksum += header.payloadSize;
			outChecksum += header.packFlags;
			outChecksum += footer.senderID;
			return outChecksum;
		}

		/* Set one of this packet's given flag's to the given value. NOTE: non-custom flags should not be set manually, and might be unavailable after packet finalization */
		CrossPackError SetPacketFlag(const CrossPackFlagBit& inFlag, bool inValue) const
		{
			if (hasBeenFinalized && inFlag < CrossPackFlagBit::CUSTOM_FLAG_1) 
				return CrossPackError::HAS_BEEN_FINALIZED;
			if (inValue)
				header.packFlags = CrossSysUtil::SetBit(header.packFlags, inFlag);
			else
				header.packFlags = CrossSysUtil::ClearBit(header.packFlags, inFlag);
			hasBeenSerialized = false;
			return CrossPackError::VALID_DATA;
		}

		/* Check one of this packet's flag values */
		bool GetPacketFlag(const CrossPackFlagBit& inFlag) const
		{
			return CrossSysUtil::CheckBit(header.packFlags, inFlag);
		}

		/* Set one of this packet's states to the given value. NOTE: an error will be returned if used on non-custom packet flag bits after packet finalization */
		CrossPackError SetPacketState(const CrossPackFlagBit& inStart, const CrossPackFlagBit& inEnd, CrossPackFlag inValue)
		{
			if (hasBeenFinalized && inStart < CrossPackFlagBit::CUSTOM_FLAG_1)
				return CrossPackError::HAS_BEEN_FINALIZED;
			header.packFlags = CrossSysUtil::SetStateToFlag(header.packFlags, inValue, inStart, inEnd);
			hasBeenSerialized = false;
			return CrossPackError::VALID_DATA;
		}

		/* Check one of this packet's states */
		CrossPackFlag GetPacketState(const CrossPackFlagBit& inStart, const CrossPackFlagBit& inEnd) const
		{
			return CrossSysUtil::GetStateFromFlag<CrossPackFlag>(header.packFlags, inStart, inEnd);
		}

		/* Returns trure if this packet's checksum matches the calculated checksum or if there is no checksum - false otherwise */
		bool IsValid() const
		{
			if (!GetPacketFlag(CrossPackFlagBit::CHECKSUM_FLAG))
				return true;
			return footer.checksum == CalculateChecksum();
		}

		/* Gets this packet's client ID - WARNING: users should also check the UDP_SUPPORT_FLAG to see if this client ID is valid */
		CrossClientID GetSenderID() const
		{
			return footer.senderID;
		}

		/* Gets this packet's transmitted checksum - WARNING: users should also check the CHECKSUM_FLAG to see if this checksum is valid */
		CrossPackChecksum GetChecksum() const
		{
			return footer.checksum;
		}

		/* Reset this packet so that it removes data from it's beginning - if you would like to clear the packet, use ClearPayload */
		void Reset() const
		{
			removeIdx = 0;
		}

		/* The maximum payload size */
		static const unsigned int MAX_PAYLOAD_BYTES = 1500 - sizeof(CrossPackHeader) - sizeof(CrossPackFooter);

	private:
		mutable CrossPackHeader header;
		mutable CrossPackFooter footer;
		CrossPackData* payload;
		mutable CrossPackPayloadLen removeIdx;
		bool shouldDeleteData;
		mutable bool hasBeenFinalized;
		mutable bool hasBeenSerialized;
	};

	typedef std::shared_ptr<CrossPack> CrossPackPtr;
}

#endif