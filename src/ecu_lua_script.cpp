/**
 * @file ecu_lua_script.cpp
 *
 * This file contains the class which represents the corresponding Lua script.
 */

#include "ecu_lua_script.h"
#include "libcrc/crcccitt.c"
#include "utilities.h"
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <stdexcept>
#include <unistd.h>
#include <cassert>

using namespace std;
using namespace sel;

/// Look-up table for (uppercase) hexadecimal digits [0..F].
static constexpr char HEX_LUT[] = "0123456789ABCDEF";

/// Defines the maximum size of an UDS message in bytes.
static constexpr int MAX_UDS_SIZE = 4096;

static string receivedDataBytes = "";
/**
 * Constructor. Loads a Lua script and injects common used functions.
 *
 * @param ecuIdent: the identifier name for the ECU (e.g. "PCM")
 * @param luaScript: the path to the Lua script
 */
EcuLuaScript::EcuLuaScript(const string& ecuIdent, const string& luaScript)
{
    const std::lock_guard<std::mutex> lock(luaLock_);

    if (utils::existsFile(luaScript))
    {
        // inject the C++ functions into the Lua script
        // static functions
        lua_state_["ascii"] = [](const string& utf8_str) -> string { return ascii(utf8_str); };
        lua_state_["getCounterByte"] = [](const string& msg) -> string { return getCounterByte(msg); };
        lua_state_["getDataBytes"] = [](const string& msg) { return getDataBytes(msg); };
        lua_state_["createHash"] = []() -> string { return createHash(); };
        lua_state_["toByteResponse"] = [](uint32_t value, uint32_t len = sizeof(uint32_t)) -> string { return toByteResponse(value, len); };
        lua_state_["sleep"] = [](unsigned int ms) { return sleep(ms); };
        // member functions
        lua_state_["getCurrentSession"] = [this]() -> uint32_t { return this->getCurrentSession(); }; 
        lua_state_["switchToSession"] = [this](uint32_t ses) { this->switchToSession(ses); };
        lua_state_["disconnectDoip"] = [this]() { this->disconnectDoip(); }; 
        lua_state_["sendDoipVehicleAnnouncements"] = [this]() { this->sendDoipVehicleAnnouncements(); }; 
        lua_state_["sendRaw"] = [this](const string& msg) { this->sendRaw(msg); };

        lua_state_.Load(luaScript);
        if (lua_state_[ecuIdent.c_str()].exists())
        {
            ecu_ident_ = ecuIdent;

            auto requId = lua_state_[ecu_ident_.c_str()][REQ_ID_FIELD];
            if (requId.exists())
            {
                hasRequestId_ = true;
                requestId_ = uint32_t(requId);
            }

            auto respId = lua_state_[ecu_ident_.c_str()][RES_ID_FIELD];
            if (respId.exists())
            {
                hasResponseId_ = true;
                responseId_ = uint32_t(respId);
            }

            auto broadcastId = lua_state_[ecu_ident_.c_str()][BROADCAST_ID_FIELD];
            if (broadcastId.exists())
            {
                hasBroadcastId_ = true;
                broadcastId_ = uint32_t(broadcastId);
            }

            auto j1939SourceAddress = lua_state_[ecu_ident_.c_str()][J1939_SOURCE_ADDRESS_FIELD];
            if (j1939SourceAddress.exists())
            {
                hasJ1939SourceAddress_ = true;
                j1939SourceAddress_ = uint32_t(j1939SourceAddress);
            }

            auto doipLogicalEcuAddress = lua_state_[ecu_ident_.c_str()][DOIP_LOGICAL_ECU_ADDRESS_FIELD];
            if (doipLogicalEcuAddress.exists())
            {
                hasDoIPLogicalEcuAddress_ = true;
                doipLogicalEcuAddress_ = uint32_t(doipLogicalEcuAddress);
            }

            return;
        }
    }
}

/**
 * Move constructor.
 * 
 * @param orig: the originating instance
 */
EcuLuaScript::EcuLuaScript(EcuLuaScript&& orig) noexcept
: lua_state_(move(orig.lua_state_))
, ecu_ident_(move(orig.ecu_ident_))
, pSessionCtrl_(orig.pSessionCtrl_)
, pIsoTpSender_(orig.pIsoTpSender_)
, requestId_(orig.requestId_)
, responseId_(orig.responseId_)
, broadcastId_(orig.broadcastId_)
, j1939SourceAddress_(orig.j1939SourceAddress_)
{
    orig.pSessionCtrl_ = nullptr;
    orig.pIsoTpSender_ = nullptr;
}

/**
 * Move-assignment operator.
 * 
 * @param orig: the originating instance
 * @return reference to the moved instance
 */
EcuLuaScript& EcuLuaScript::operator=(EcuLuaScript&& orig) noexcept
{
    assert(this != &orig);
    lua_state_ = move(orig.lua_state_);
    ecu_ident_ = move(orig.ecu_ident_);
    pSessionCtrl_ = orig.pSessionCtrl_;
    pIsoTpSender_ = orig.pIsoTpSender_;
    requestId_ = orig.requestId_;
    responseId_ = orig.responseId_;
    broadcastId_ = orig.broadcastId_;
    j1939SourceAddress_ = orig.j1939SourceAddress_;
    orig.pIsoTpSender_ = nullptr;
    orig.pSessionCtrl_ = nullptr;
    return *this;
};

/**
 * Gets the UDS request ID according to the loaded Lua script. Since this call
 * is very common, the value is cached at the instantiation to avoid expensive
 * access operations on the Lua file.
 *
 * @return the request ID or 0 on error
 */
uint32_t EcuLuaScript::getRequestId() const
{
    return requestId_;
}

/**
 * Gets the UDS response ID according to the loaded Lua script. Since this call
 * is very common, the value is cached at the instantiation to avoid expensive
 * access operations on the Lua file.
 *
 * @return the response ID or 0 on error
 */
uint32_t EcuLuaScript::getResponseId() const
{
    return responseId_;
}

/**
 * Gets the UDS broadcast address, which is `0x7DF` on default.
 *  
 * @return the specific broadcast address according to the Lua file or `0x7DF`
 *         on default
 */
uint32_t EcuLuaScript::getBroadcastId() const
{
    return broadcastId_;
}

/**
 * Gets the J1939SourceAddress
 *  
 * @return the specific J1939 address according to the Lua file
 */
uint8_t EcuLuaScript::getJ1939SourceAddress() const
{
    return j1939SourceAddress_;
}

/**
 * Reads the data according to `ReadDataByIdentifier`-table in the Lua script.
 *
 * @param identifier: the identifier to access the field in the Lua table
 * @return the identifier field on success, otherwise an empty string
 */
string EcuLuaScript::getDataByIdentifier(const string& identifier)
{
    const std::lock_guard<std::mutex> lock(luaLock_);

    auto val = lua_state_[ecu_ident_.c_str()][READ_DATA_BY_IDENTIFIER_TABLE][identifier];

    if (val.isFunction())
    {
        return val(identifier);
    }
    else
    {
        return val;
    }
}

/**
 * Overload with additional sessions handling.
 *
 * @param identifier: the identifier to access the field in the Lua table
 * @param session: the session as string (e.g. "Programming")
 * @return the identifier field on success, otherwise an empty string
 */
string EcuLuaScript::getDataByIdentifier(const string& identifier, const string& session)
{
    const std::lock_guard<std::mutex> lock(luaLock_);

    auto val = lua_state_[ecu_ident_.c_str()][session][READ_DATA_BY_IDENTIFIER_TABLE][identifier];

    if (val.isFunction())
    {
        return val(identifier);
    }
    else
    {
        return val;
    }
}

string EcuLuaScript::getSeed(uint8_t seed_level)
{
    const std::lock_guard<std::mutex> lock(luaLock_);

    auto val = lua_state_[ecu_ident_.c_str()][READ_SEED][seed_level];
    if (val.exists())
    {
        return val;
    }
    return "";
}

/**
 * Converts a literal hex string into a value vector.
 *
 * @param hexString: the literal hex string (e.g. "41 6f 54")
 * @return a vector with the byte values
 */
vector<uint8_t> EcuLuaScript::literalHexStrToBytes(const string& hexString)
{
    // make a working copy
    string tmpStr = hexString;
    // remove white spaces from string
    tmpStr.erase(remove(tmpStr.begin(), tmpStr.end(), ' '), tmpStr.end());
    vector<uint8_t> data;
    // plus `% 2` just in case of a "odd" byte number
    data.reserve(tmpStr.length() / 2 + (tmpStr.length() % 2));
    string byteString;
    uint8_t byte;
    for (size_t i = 0; i < tmpStr.length(); i += 2)
    {
        byteString = tmpStr.substr(i, 2);
        byte = static_cast<uint8_t> (strtol(byteString.c_str(), NULL, 16));
        data.push_back(byte);
    }
    cout << endl;
    return data;
}

/**
 * Convert the given string into another string that represents the hex bytes of
 * the input string. This is a convenience function to use ascii strings in
 * responses.
 *
 * Example:
 *     `ascii("Hello")` -> `" 48 65 6C 6C 6F "`
 *
 * @param utf8_str: the input string to convert
 * @return a literal string of hex bytes (e.g. " 12 4A FF ") or an empty string
 * on error
 *
 * @note To allow a seamless string concatenation, the returned string always
 * begins and ends with an whitespace.
 */
string EcuLuaScript::ascii(const string& utf8_str) noexcept
{
    const size_t len = utf8_str.length();
    if (len == 0)
    {
        return "";
    }

    string output;
    // str length * (1 whitespace + 2 characters per byte) + last whitespace
    output.reserve(len * 3 + 1);
    unsigned char c;
    for (size_t i = 0; i < len; ++i)
    {
        c = utf8_str[i];
        output.push_back(' ');
        output.push_back(HEX_LUT[c >> 4]);
        output.push_back(HEX_LUT[c & 0x0F]);
    }
    output.push_back(' ');
    return output;
}
/**
*Substrings the counter value of the message which is the second byte
**/
string EcuLuaScript::getCounterByte(const string& msg) noexcept
{
    // make a working copy
    string tmpStr = msg;
    // remove white spaces from string
    tmpStr.erase(remove(tmpStr.begin(), tmpStr.end(), ' '), tmpStr.end());
    string answer = tmpStr;
    answer = answer.substr(2,2);
    return answer;
}
/**
*Substrings the data bytes value of the message which is the second byte
**/
void EcuLuaScript::getDataBytes(const string& msg) noexcept
{
    // make a working copy
    string tmpStr = msg;
    // remove white spaces from string
    tmpStr.erase(remove(tmpStr.begin(), tmpStr.end(), ' '), tmpStr.end());
    string answer = tmpStr;
    //cut the first two bytes which indicate the request 
    answer = answer.substr(4,answer.length());
    //save the answer in global var, which is also available for createHash()
    receivedDataBytes = receivedDataBytes + answer;
}

string EcuLuaScript::createHash() noexcept
{
    vector <uint8_t> resp = literalHexStrToBytes(receivedDataBytes);
    uint16_t crc2 = crc_ccitt_ffff(resp.data(), resp.size());
    char hash[5];
    snprintf(hash, 5, "%X", crc2);
    string answer(hash);
    if(answer.length() % 2 != 0){
        answer = "0" + answer;
    }
    //reset the received Data variable
    receivedDataBytes = "";
    cout << answer << endl;
    return answer;
}

/**
 * Convert the given unsigned value into a hex byte string as used in requests
 * and responses. The parameter `len` [0..4096] gives the number of bytes that
 * gets returned. In case `len` equals 0, a empty string is returned.
 *
 * Examples:
 *     `toByteResponse(13248, 2)` -> `"33 C0"`
 *     `toByteResponse(13248, 3)` -> `"00 33 C0"`
 *     `toByteResponse(13248, 1)` -> `"C0"`
 *     `toByteResponse(13248)` -> `"00 00 00 00 00 00 33 C0"`
 *
 * @param value: the numeric value to send (e.g. `123`, `0xff`)
 * @param len: the length in bytes [default = 4]
 */
string EcuLuaScript::toByteResponse(uint32_t value,
                                    uint32_t len /* = 4 */) noexcept
{
    if (len > MAX_UDS_SIZE)
    {
        len = MAX_UDS_SIZE;
    }

    static constexpr int CHAR_SP = 3; // character space for 2 hex digits + 1 whitespace
    const int zeroFill = len - sizeof(value);

    if (zeroFill < 0)
    {
        // truncated value
        const size_t space = len * CHAR_SP;
        char str[space];
        for (size_t i = 0, j = (len * 2 - 1) * 4; i < space; i += CHAR_SP, j -= 8)
        {
            str[i] = HEX_LUT[(value >> j) & 0x0F];
            str[i + 1] = HEX_LUT[(value >> (j - 4)) & 0x0F];
            str[i + 2] = ' ';
        }
        str[space - 1] = '\0';
        return str;
    }
    else
    {
        // fill up wit zeros
        const size_t space = ((sizeof(value) + zeroFill) * CHAR_SP);
        const size_t valSpace = zeroFill * CHAR_SP;
        char str[space];
        for (size_t i = 0; i < valSpace; i += CHAR_SP)
        {
            str[i] = '0';
            str[i + 1] = '0';
            str[i + 2] = ' ';
        }

        // insert hex values
        for (size_t i = valSpace, j = (sizeof(value) * 2 - 1) * 4; i < space; i += CHAR_SP, j -= 8)
        {
            str[i] = HEX_LUT[(value >> j) & 0x0F];
            str[i + 1] = HEX_LUT[(value >> (j - 4)) & 0x0F];
            str[i + 2] = ' ';
        }
        str[space - 1] = '\0';
        return str;
    }
    return "";
}

/**
 * Sends the given response (string of hex bytes) immediately.
 *
 * @param response: the raw response message to send (e.g. "DE AD C0 DE")
 */
void EcuLuaScript::sendRaw(const string& response) const
{
    vector<uint8_t> resp = literalHexStrToBytes(response);
    if(pIsoTpSender_) {
        pIsoTpSender_->sendData(resp.data(), resp.size());
    }
    if(pDoipSimServer_) {
        pDoipSimServer_->sendDiagnosticResponse(resp, doipLogicalEcuAddress_);
    }

}

/**
 * Suspend the script for the given number of milliseconds.
 *
 * @param ms: time to sleep in milliseconds
 */
void EcuLuaScript::sleep(unsigned int ms) noexcept
{
    usleep(ms * 1000);
}

/**
 * Returns the currently active diagnostic session to be used in custom
 * functions.
 */
uint8_t EcuLuaScript::getCurrentSession() const
{
    assert(pSessionCtrl_ != nullptr);

    return pSessionCtrl_->getCurrentUdsSession();
}

/**
 * Switch to the given (numeric) diagnostic session.
 *
 * @param ses: the session ID (e.g. `0x01` = DEFAULT, `0x02` = PROGRAMMING)
 */
void EcuLuaScript::switchToSession(int ses)
{
    assert(pSessionCtrl_ != nullptr);

    pSessionCtrl_->setCurrentUdsSession(UdsSession(ses));
}

/**
 * Disconnect the currently active DoIP TCP connection
 */
void EcuLuaScript::disconnectDoip()
{
    if(pDoipSimServer_) pDoipSimServer_->triggerDisconnection();
}

void EcuLuaScript::sendDoipVehicleAnnouncements()
{
    if(pDoipSimServer_) pDoipSimServer_->sendVehicleAnnouncements();
}


/**
 * Gets all keys from the given Lua table
 *
 * @return vector of keys or an empty vector if not table given
 */
vector<string> EcuLuaScript::getLuaTableKeys(Selector luaTable)
{
    if(luaTable.exists()) {
        return luaTable.getKeys();
    } else {
        return vector<string>();
    }
}

/**
 * @brief Remove all separator characters from given string
 * 
 * @param rawString 
 * @return string rawString with all separator characters removed
 */
string EcuLuaScript::cleanupString(string rawString)
{
    rawString.erase(remove_if(rawString.begin(), rawString.end(),
        [](char &x){return string("_.,; #\t").find(x) != string::npos;}
    ), rawString.end());
    return rawString;
}

/**
 * Gets J1939 PGNS from the Lua PGN-Table.
 *
 * @return vector of raw message data as they are configured in lua
 */
vector<string> EcuLuaScript::getJ1939PGNs()
{
    const std::lock_guard<std::mutex> lock(luaLock_);

    cout << "Get PGNs from ident: " << ecu_ident_ << endl;
    return getLuaTableKeys(lua_state_[ecu_ident_.c_str()][J1939_PGN_TABLE]);
}

/**
 * Build a RequestByteTree from given keys with given response mapping function
 */
shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> EcuLuaScript::buildRequestByteTree(
    vector<string> requestKeys, std::function<shared_ptr<Selector>(string &key)> mappingFunction) {

    shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> requestByteTree(new RequestByteTreeNode<shared_ptr<Selector>>());
    for(string requestStringRaw : requestKeys)
    {
        string requestString = cleanupString(requestStringRaw);
        
        try {
            shared_ptr<Selector> response = mappingFunction(requestStringRaw);

            auto requestByteLeaf = addRequestToTree(requestByteTree, requestString);
            requestByteLeaf->setLuaResponse(response);
        } catch(exception &e) {
            cerr << "Ignoring invalid request '" << requestStringRaw << "': " << e.what();
        }
    }
		
	return requestByteTree;
}
/**
 * Build a RequestByteTree from the 'Raw' table in the current simulation
 */
shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> EcuLuaScript::buildRequestByteTreeFromRawTable() {
    const std::lock_guard<std::mutex> lock(luaLock_);

    cout << "Get 'Raw' request tree from ident: " << ecu_ident_ << endl;
    auto rawTable = lua_state_[ecu_ident_.c_str()][RAW_TABLE];
    vector<string> requestKeys = getLuaTableKeys(rawTable);

    return buildRequestByteTree(requestKeys, [&rawTable](string &x){ return shared_ptr<Selector>(new Selector(rawTable[x]));});
}


/**
 * Build a RequestByteTree from the 'PGN' table in the current simulation
 */
shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> EcuLuaScript::buildRequestByteTreeFromPGNTable() {
    const std::lock_guard<std::mutex> lock(luaLock_);

    cout << "Get 'PGN' request tree from ident: " << ecu_ident_ << endl;
    auto pgnTable = lua_state_[ecu_ident_.c_str()][J1939_PGN_TABLE];
    vector<string> requestKeys = getLuaTableKeys(pgnTable);

    requestKeys.erase(remove_if(requestKeys.begin(), requestKeys.end(),
        [](string &x){return x.find('#') == string::npos;}
    ), requestKeys.end());

    return buildRequestByteTree(requestKeys, [&pgnTable](string &x){ return shared_ptr<Selector>(new Selector(pgnTable[x]));});
}

/**
 * Fetch list of PGNs that do not contain the '#' character and map them to their Lua response
 */
map<string,shared_ptr<Selector>> EcuLuaScript::buildRequestPGNMap() {
    const std::lock_guard<std::mutex> lock(luaLock_);

    map<string,shared_ptr<Selector>> pgnMap = map<string,shared_ptr<Selector>>();

    auto pgnTable = lua_state_[ecu_ident_.c_str()][J1939_PGN_TABLE];
    vector<string> pgnKeys = getLuaTableKeys(pgnTable);

    pgnKeys.erase(remove_if(pgnKeys.begin(), pgnKeys.end(),
        [](string &x){return x.find('#') != string::npos;}
    ), pgnKeys.end());

    for( string pgnKey : pgnKeys) {
        if(pgnKey.find('#') == string::npos) {
            string pgnNormalized = cleanupString(pgnKey);
            pgnMap.insert(pair<string,shared_ptr<Selector>>(pgnNormalized, shared_ptr<Selector>(new Selector(pgnTable[pgnKey]))));
        }
    }

    return pgnMap;
}


J1939PGNData EcuLuaScript::getJ1939RequestPGNData(const map<string,shared_ptr<Selector>> pgnMap, const std::string& pgn)
{
    cout << "Looking for requested PGN: " << pgn << endl;
    J1939PGNData pgnData;
    pgnData.cycleTime = 0;

    auto pgnItem = pgnMap.find(cleanupString(pgn));
    if(pgnItem != pgnMap.end()) {
        auto val = *(pgnItem->second);
        cout << "Found PGN: " << pgn << endl;
        if (val.isFunction())
        {
            pgnData.payload = val().toString();
        }
        else if(val.isTable())
        {
            auto pgnPayload = val[J1939_PGN_PAYLOAD];
            auto pgnCycleTime = val[J1939_PGN_CYCLETIME];
            if(pgnCycleTime.exists() == true) {
                pgnData.cycleTime = pgnCycleTime;
            }
            if(pgnPayload.exists() == true) {
                if(pgnPayload.isFunction())
                {
                    pgnData.payload = pgnPayload().toString();
                }
                else
                {
                    pgnData.payload = pgnPayload.toString();
                }
            }
        }
        else
        {
            pgnData.payload = val.toString(); // will be cast into string
        }
    }
    return pgnData;

}

string EcuLuaScript::getJ1939Response(const shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> requestByteTree, const uint32_t pgn, const uint8_t *payload, const uint32_t payloadLength)
{
    const std::lock_guard<std::mutex> lock(luaLock_);
    string response;

    vector<uint8_t> lookupPayload(3 + payloadLength);
    lookupPayload[0] = (uint8_t)(pgn >> 0);
    lookupPayload[1] = (uint8_t)(pgn >> 8);
    lookupPayload[2] = (uint8_t)(pgn >> 16);

    for(uint32_t i = 0; i < payloadLength; i++) {
        lookupPayload[i+3] = payload[i];
    }

    auto val = getValueFromTree(requestByteTree, lookupPayload);

    if(val.has_value() == true) {
        shared_ptr<Selector> luaResp = *val;
        if (luaResp->isFunction())
        {
            response = (*luaResp)(intToHexString(payload, payloadLength)).toString();
        }
        else
        {
            response = luaResp->toString();
        } 
    }

    return response;
}

/**
 * Gets the response from the given requestByteTree that matches the given payload
 * The entries in the table are either strings or functions that will
 * to be called, with the payload string as the default parameter.
 *
 * @param requestByteTree: The prepared tree-representation of the request table
 * @param payload: Request payload to be matched with the table
 * @param payloadLength Length of the payload
 * @return the response to be sent as literal hex byte string, an empty string when no response should be sent
 *          or an empty optional when no table entry matches the request
 */
optional<string> EcuLuaScript::getRawResponse(const shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> requestByteTree, const uint8_t *payload, const uint32_t payloadLength)
{ 
    const std::lock_guard<std::mutex> scopelock(luaLock_);

    vector<uint8_t> lookupPayload(payload, payload + payloadLength);

    auto val = getValueFromTree(requestByteTree, lookupPayload);

    optional<string> response = {};
    if(val.has_value() == true) {
        shared_ptr<Selector> luaResp = *val;
        if (luaResp->isFunction())
        {
            response = (*luaResp)(intToHexString(payload, payloadLength)).toString();
        }
        else
        {
            response = luaResp->toString();
        } 
    }
    return response;
}


/**
 * Sets the SessionController required for session handling.
 *
 * @param pSesCtrl: pointer to the orchestrating `SessionController`
 */
void EcuLuaScript::registerSessionController(SessionController* pSesCtrl) noexcept
{
    pSessionCtrl_ = pSesCtrl;
}

void EcuLuaScript::registerIsoTpSender(IsoTpSender* pSender) noexcept
{
    pIsoTpSender_ = pSender;
}

void EcuLuaScript::registerDoipSimServer(DoIPSimServer *pDoipSimServer) noexcept
{
    pDoipSimServer_ = pDoipSimServer;
}

string EcuLuaScript::intToHexString(const uint8_t* buffer, const size_t num_bytes)
{
    string a = "";

    for (uint32_t i = 0; i < num_bytes; i++)
    {
        a.push_back(HEX_LUT[buffer[i] / 16]);
        a.push_back(HEX_LUT[buffer[i] % 16]);
        if (!(i == num_bytes - 1))
        {
            a.push_back(' ');
        }
    }
    return a;
}

/**
 * Tries to get the Value by using the request tree, including wildcard and placeholder keys
 * See {@link RequestByteTreeNode} to see how the tree works
 * 
 */
template<class T>
optional<T> EcuLuaScript::getValueFromTree(const shared_ptr<RequestByteTreeNode<T>> requestByteTree, const vector<uint8_t> payload) {
    set<shared_ptr<RequestByteTreeNode<T>>> potentiallyMatchingRequests;
    potentiallyMatchingRequests.insert(requestByteTree);
    
    for(auto nextByte : payload) {
        if(potentiallyMatchingRequests.empty()) {
            break;
        }
        set<shared_ptr<RequestByteTreeNode<T>>> matchingNodes;
        for (typename set<shared_ptr<RequestByteTreeNode<T>>>::iterator reqIter = potentiallyMatchingRequests.begin();
                reqIter != potentiallyMatchingRequests.end(); reqIter++) {
            auto crntRequestByte = *reqIter;
            if(crntRequestByte->isWildcard()) {
                matchingNodes.insert(crntRequestByte);
                continue;
            }
            findAndAddMatchesForNextByte(matchingNodes, crntRequestByte, nextByte);				
        }
        potentiallyMatchingRequests = matchingNodes;
    }
    
    auto bestMatchingRequest = findBestMatchingRequest(potentiallyMatchingRequests);
    if(bestMatchingRequest) {
        return bestMatchingRequest->getLuaResponse();
    }
    return {};
}

template<class T>
void EcuLuaScript::findAndAddMatchesForNextByte(set<shared_ptr<RequestByteTreeNode<T>>> &matchingNodes, shared_ptr<RequestByteTreeNode<T>> currentByte, uint8_t nextByte) {
    shared_ptr<RequestByteTreeNode<T>> crntByteTreeOpt;
    crntByteTreeOpt = currentByte->getSubsequentByte(nextByte);
    if(crntByteTreeOpt) {
        matchingNodes.insert(crntByteTreeOpt);
    }
    crntByteTreeOpt = currentByte->getSubsequentPlaceholder();
    if(crntByteTreeOpt) {
        matchingNodes.insert(crntByteTreeOpt);
    }
    crntByteTreeOpt = currentByte->getSubsequentWildcard();
    if(crntByteTreeOpt) {
        matchingNodes.insert(crntByteTreeOpt);
    }
}

/**
 * From the list of potentially matching requests find the one that matches best
 * according these rules in the specified order:
 * * Prefer requests without wildcard (*)
 * * Prefer requests with fewer placeholders (XX)
 * * If there are only requests with wildcard, prefer longer ones (no matter how many placeholders)
 *   (All requests without wildcard have per definition the same length as the received request)
 * * If there are still multiple requests, it is undefined which one is chosen
 * @param potentiallyMatchingRequests
 * @return
 */
template<class T>
shared_ptr<RequestByteTreeNode<T>> EcuLuaScript::findBestMatchingRequest(set<shared_ptr<RequestByteTreeNode<T>>> &potentiallyMatchingRequests) {
    shared_ptr<RequestByteTreeNode<T>> bestMatchingRequest;
    for (auto matchingRequestItem : potentiallyMatchingRequests) {
        auto matchingRequest = getThisOrNextWildcardWithResponse(matchingRequestItem);
        if(!matchingRequest->getLuaResponse()) {
            continue;
        }
        if(!bestMatchingRequest) {
            bestMatchingRequest = matchingRequest;
        } else {				
            if(bestMatchingRequest->isWildcard() && !matchingRequest->isWildcard()) {
                bestMatchingRequest = matchingRequest;
            } else if(matchingRequest->isWildcard() && matchingRequest->getRequestLength() > bestMatchingRequest->getRequestLength()) {
                bestMatchingRequest = matchingRequest;
            } else if(matchingRequest->getPlaceholderCount() < bestMatchingRequest->getPlaceholderCount()) {
                bestMatchingRequest = matchingRequest;
            }
        }
    }
    return bestMatchingRequest;
}

/**
 * Determine if the given node has a response, if not return the subsequent wildcard if there is one.
 * Background: A Wildcard also matches for 0 bytes. 
 * @param requestByteNode
 * @return
 */
template<class T>
shared_ptr<RequestByteTreeNode<T>> EcuLuaScript::getThisOrNextWildcardWithResponse(shared_ptr<RequestByteTreeNode<T>> requestByteNode) {
    if(!requestByteNode->getLuaResponse()) {
        return (requestByteNode->getSubsequentWildcard() ? requestByteNode->getSubsequentWildcard() : requestByteNode);
    }
    return requestByteNode;
}

/**
 * Add the given request to the given request byte tree and return the leaf node
 * @param requestByteTree The root of the tree this request should be added to
 * @param requestStringRaw
 * @param requestString The normalized (i.e. without spaces and other separators) string representation of the request 
 * @return tree node that represents the leaf ready for the response to be added
 */
template<class T>
shared_ptr<RequestByteTreeNode<T>> EcuLuaScript::addRequestToTree(shared_ptr<RequestByteTreeNode<T>> requestByteTree, string &requestString) {
    auto currentRequestByteTreePosition = requestByteTree;
    for(uint32_t i = 0; i + 1 < requestString.length(); i+=2) {
        string requestByteString = requestString.substr(i,2);
        if(strncasecmp(requestByteString.c_str(), REQUEST_PLACEHOLDER.c_str(), REQUEST_PLACEHOLDER.length()) == 0) {
            currentRequestByteTreePosition = currentRequestByteTreePosition->appendPlaceholder();
        } else {
            try {
                uint8_t requestByte = literalHexStrToBytes(requestByteString).at(0);
                currentRequestByteTreePosition = currentRequestByteTreePosition->appendByte(requestByte);
            } catch(out_of_range &e) {
                cerr << requestByteString << " is not a hex number." << endl;
                throw exception();
            }
        }
    }
    
    if(requestString.length() % 2 != 0) {
        if(requestString.compare(requestString.length() - REQUEST_WILDCARD.length(), REQUEST_WILDCARD.length(), REQUEST_WILDCARD) == 0) {
            currentRequestByteTreePosition = currentRequestByteTreePosition->appendWildcard();
        } else {
            cerr << requestString << " has odd number of digits." << endl;
            throw exception();
        }
    }
    return currentRequestByteTreePosition;
}

