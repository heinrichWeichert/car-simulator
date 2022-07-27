#ifndef REQUEST_BYTE_TREE_NODE_H
#define REQUEST_BYTE_TREE_NODE_H

#include <map>
#include <memory>
#include <optional>

#include "selene.h"

using namespace std;
using namespace sel;

/**
 * In order to quickly find a request from the simulation that fits the current
 * request coming from the application, all possible requests are defined in
 * in a tree structure of bytes. Each element in the tree (i.e. each byte) contains
 * pointers to possible next bytes or the actual response
 * 
 * This class represents one element (byte) in this tree.
 * 
 * For example there are the following requests defined in a simulation file
 * * 22 F1 90 
 * * 22 30 98
 * * 11 01
 * * 36 XX *
 * * 31 XX 12
 * * 31 01 12
 * 
 * Then the tree will look like
 * - 22
 *   - F1
 *     - 90 -> response1
 *   - 30
 *     - 98 -> response2
 * - 11
 *   - 01 -> response3
 * - 36
 *   - XX
 *     - * -> response4
 * - 31
 *   - XX
 *     - 12 -> response5
 *   - 01
 *     - 12 -> response6
 * 
 * When the application sends request 31 01 12, the simulation can quickly move through
 * the tree to find the matching response "response5"
 *
 */
template<class T>
class RequestByteTreeNode {

private:	
	/**
	 * Contains all possible bytes at the next position in the request.
	 * For example, it there are requests available 22 F1 90 and 22 17 10 and
	 * this object represents byte at position 0 (i.e. 22), then the map contains
	 * entries for F1 and 17.  
	 */
	map<uint8_t, shared_ptr<RequestByteTreeNode<T>>> subsequentByte;

	/**
	 * Points to following placeholder or wildcard entry respectively.
	 * It works the same as the map but instead of putting entries with magic number
	 * into the map that represents placeholder and wildcard, we use separate
	 * fields for them. This avoids converting between byte and short.
	 */
	shared_ptr<RequestByteTreeNode<T>> subsequentPlaceholder;
	shared_ptr<RequestByteTreeNode<T>> subsequentWildcard;
	
	/**
	 * Stores the actual rvalue from the lua map in case there is any
	 * For example if there is a request ["22 F1 90"] = "62 F1 90", then
	 * if this object represents the position 2 of that request the response
	 * is stored in this object, if it represents position 0 or 1 the response
	 * is empty. 
	 */
	optional<T> luaResponse;
	
	/**
	 * Meta information to determine best matching request 
	 */
	uint32_t placeholderCount;
	uint32_t requestLength;
	bool wildcard;
	

public:	
	RequestByteTreeNode(uint32_t placeholderCount = 0, uint32_t requestLength = 0) :
        luaResponse(nullopt),
        placeholderCount(placeholderCount),
		requestLength(requestLength) {}
    
    RequestByteTreeNode(RequestByteTreeNode<T> &rbt) {
		cerr << "Copy Constructor of RequestByteTreeNode must not be called!" << endl;
        throw exception();
    }

	inline shared_ptr<RequestByteTreeNode<T>> getSubsequentByte(uint8_t requestByte) {
        typename map<uint8_t, shared_ptr<RequestByteTreeNode<T>>>::const_iterator iter = subsequentByte.find(requestByte);
		if(iter != subsequentByte.end()) {
			return iter->second;
		}
		return nullptr;
	}

	inline optional<T> &getLuaResponse() {
		return luaResponse;
	}
	
	inline shared_ptr<RequestByteTreeNode<T>> getSubsequentPlaceholder() {
		return subsequentPlaceholder;
	}

	inline shared_ptr<RequestByteTreeNode<T>> getSubsequentWildcard() {
		return subsequentWildcard;
	}

	inline int getPlaceholderCount() const {
		return placeholderCount;
	}

	inline int getRequestLength() const {
		return requestLength;
	}

	inline bool isWildcard() const {
		return wildcard;
	}	
	
	// Builder Methods
	inline shared_ptr<RequestByteTreeNode<T>> appendByte(uint8_t requestByte) {
        subsequentByte.emplace(requestByte, shared_ptr<RequestByteTreeNode<T>>(new RequestByteTreeNode<T>(placeholderCount, requestLength + 1)));
		return subsequentByte[requestByte];
	}
	
	inline shared_ptr<RequestByteTreeNode<T>> appendWildcard() {
		if(subsequentWildcard) {
			cerr << "Same request with Wildcard already exists" << endl;
			throw exception();
		}
		subsequentWildcard.reset(new RequestByteTreeNode<T>(placeholderCount, requestLength + 1));
		shared_ptr<RequestByteTreeNode<T>> nextElement = subsequentWildcard;
		nextElement->wildcard = true;
		return nextElement;
	}

	inline shared_ptr<RequestByteTreeNode<T>> appendPlaceholder() {
		if(!subsequentPlaceholder) {
			subsequentPlaceholder.reset(new RequestByteTreeNode<T>(placeholderCount + 1, requestLength + 1));
		}
		shared_ptr<RequestByteTreeNode<T>> nextElement = subsequentPlaceholder;
		nextElement->placeholderCount = this->placeholderCount + 1;
		return nextElement;
	}
	
	inline shared_ptr<RequestByteTreeNode<T>> setLuaResponse(T &luaResponse) {
		this->luaResponse.emplace(luaResponse);
		return shared_ptr<RequestByteTreeNode<T>>(this);
	}

};

#endif