/**
 * @file ecu_lua_script.h
 *
 */

#ifndef ECU_LUA_SCRIPT_H
#define ECU_LUA_SCRIPT_H

#include "selene.h"
#include "isotp_sender.h"
#include "session_controller.h"
#include "request_byte_tree_node.h"
#include <string>
#include <cstdint>
#include <vector>
#include <mutex>
#include <set>
#include <optional>
#include <functional>

constexpr char REQ_ID_FIELD[] = "RequestId";
constexpr char RES_ID_FIELD[] = "ResponseId";
constexpr char BROADCAST_ID_FIELD[] = "BroadcastId";
constexpr char READ_DATA_BY_IDENTIFIER_TABLE[] = "ReadDataByIdentifier";
constexpr char READ_SEED[] = "Seed";
constexpr char RAW_TABLE[] = "Raw";
constexpr char J1939_SOURCE_ADDRESS_FIELD[] = "J1939SourceAddress";
constexpr char J1939_PGN_TABLE[] = "PGNs";
constexpr char J1939_PGN_PAYLOAD[] = "payload";
constexpr char J1939_PGN_CYCLETIME[] = "cycleTime";
constexpr uint32_t DEFAULT_BROADCAST_ADDR = 0x7DF;

const string REQUEST_PLACEHOLDER("XX");
const string REQUEST_WILDCARD("*");

struct J1939PGNData
{
    unsigned int cycleTime;
    std::string payload;
};

class EcuLuaScript
{
public:
    EcuLuaScript() = delete;
    EcuLuaScript(const std::string& ecuIdent, const std::string& luaScript);
    EcuLuaScript(const EcuLuaScript& orig) = delete;
    EcuLuaScript& operator =(const EcuLuaScript& orig) = delete;
    EcuLuaScript(EcuLuaScript&& orig) noexcept;
    EcuLuaScript& operator =(EcuLuaScript&& orig) noexcept;
    virtual ~EcuLuaScript() = default;

    bool hasRequestId() const { return hasRequestId_; };
    std::uint32_t getRequestId() const;
    bool hasResponseId() const { return hasResponseId_; };
    std::uint32_t getResponseId() const;
    bool hasBroadcastId() const { return hasBroadcastId_; };
    std::uint32_t getBroadcastId() const;
    bool hasJ1939SourceAddress() const { return hasJ1939SourceAddress_; };
    std::uint8_t getJ1939SourceAddress() const;

    std::string getSeed(std::uint8_t identifier);
    std::string getDataByIdentifier(const std::string& identifier);
    std::string getDataByIdentifier(const std::string& identifier, const std::string& session);
    std::vector<std::string> getJ1939PGNs();
    J1939PGNData getJ1939RequestPGNData(const std::string& pgn);
    std::string getJ1939Response(const shared_ptr<RequestByteTreeNode<Selector*>> requestByteTree, const uint32_t pgn, const uint8_t *payload, const uint32_t payloadLength);

    optional<string> getRawResponse(const shared_ptr<RequestByteTreeNode<Selector*>> requestByteTree, const uint8_t *payload, const uint32_t payloadLength);
    static std::vector<std::uint8_t> literalHexStrToBytes(const std::string& hexString);

    static std::string ascii(const std::string& utf8_str) noexcept;
    static std::string getCounterByte(const std::string& msg) noexcept;
    static void getDataBytes(const std::string& msg) noexcept;
    static std::string createHash() noexcept;
    static std::string toByteResponse(std::uint32_t value, std::uint32_t len = sizeof(std::uint32_t)) noexcept;
    static void sleep(unsigned int ms) noexcept;
    void sendRaw(const std::string& response) const;
    std::uint8_t getCurrentSession() const;
    void switchToSession(int ses);

    void registerSessionController(SessionController* pSesCtrl) noexcept;
    void registerIsoTpSender(IsoTpSender* pSender) noexcept;

    std::string intToHexString(const uint8_t* buffer, const std::size_t num_bytes);

    template<class T>
	optional<T> getValueFromTree(const shared_ptr<RequestByteTreeNode<T>> requestByteTree, const vector<uint8_t> payload);
	shared_ptr<RequestByteTreeNode<sel::Selector*>> buildRequestByteTreeFromPGNTable();
    shared_ptr<RequestByteTreeNode<Selector*>> buildRequestByteTreeFromRawTable();

private:
    sel::State lua_state_{true};
    std::string ecu_ident_;
    SessionController* pSessionCtrl_ = nullptr;
    IsoTpSender* pIsoTpSender_ = nullptr;
    bool hasRequestId_ = false;
    std::uint32_t requestId_;
    bool hasResponseId_ = false;
    std::uint32_t responseId_;
    bool hasBroadcastId_ = false;
    std::uint32_t broadcastId_ = DEFAULT_BROADCAST_ADDR;
    bool hasJ1939SourceAddress_ = false;
    std::uint8_t j1939SourceAddress_;
    std::mutex luaLock_;

    vector<string> getLuaTableKeys(Selector luaTable);
    string cleanupString(string rawString);
    shared_ptr<RequestByteTreeNode<Selector*>> buildRequestByteTree(
        vector<string> requestKeys, std::function<Selector*(string &key)> mappingFunction);

    template<class T>
    void findAndAddMatchesForNextByte(set<shared_ptr<RequestByteTreeNode<T>>> &matchingNodes, shared_ptr<RequestByteTreeNode<T>> currentByte, uint8_t nextByte);
	template<class T>
    shared_ptr<RequestByteTreeNode<T>> findBestMatchingRequest(set<shared_ptr<RequestByteTreeNode<T>>> &potentiallyMatchingRequests);
	template<class T>
    shared_ptr<RequestByteTreeNode<T>> getThisOrNextWildcardWithResponse(shared_ptr<RequestByteTreeNode<T>> requestByteNode);
	template<class T>
    shared_ptr<RequestByteTreeNode<T>> addRequestToTree(shared_ptr<RequestByteTreeNode<T>> requestByteTree, string &requestString);

};

#endif /* ECU_LUA_SCRIPT_H */
