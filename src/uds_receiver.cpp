/** 
 * @file uds_receiver.cpp
 * 
 * Implementation of an UDS server, which receives requests and sends response
 * messages via ISO_TP.
 */

#include "uds_receiver.h"
#include "service_identifier.h"
#include <vector>
#include <array>
#include <iostream>
#include <random>
#include <limits>
#include <cassert>

using namespace std;

static random_device RANDOM_DEVICE; ///< necessary for `generateSeed()`

/**
 * Constructor.
 * 
 * @param source
 * @param dest
 * @param device
 * @param ecuScript
 * @param pSender
 * @param pSesCtrl
 */
UdsReceiver::UdsReceiver(canid_t source,
                         canid_t dest,
                         const string& device,
                         EcuLuaScript *pEcuScript,
                         IsoTpSender* pSender,
                         SessionController* pSesCtrl)
: IsoTpReceiver(source, dest, device)
, pEcuScript_(pEcuScript)
, pIsoTpSender_(pSender)
, pSessionCtrl_(pSesCtrl)
{
    assert(pIsoTpSender_ != nullptr);
    assert(pSessionCtrl_ != nullptr);
    pEcuScript_->registerIsoTpSender(pSender);
    pEcuScript_->registerSessionController(pSesCtrl);
    requestByteTree = pEcuScript->buildRequestByteTreeFromRawTable();
}

/**
 * Move constructor.
 * 
 * @param orig: the originating instance
 */
UdsReceiver::UdsReceiver(UdsReceiver&& orig) noexcept
: IsoTpReceiver(move(orig))
, pEcuScript_(move(orig.pEcuScript_))
, pIsoTpSender_(orig.pIsoTpSender_)
, pSessionCtrl_(orig.pSessionCtrl_)
, securityAccessType_(orig.securityAccessType_)
{
    orig.pIsoTpSender_ = nullptr;
    orig.pSessionCtrl_ = nullptr;
}

/**
 * Move assignment operator.
 * 
 * @param orig: the originating instance
 * @return reference to the moved instance
 */
UdsReceiver& UdsReceiver::operator=(UdsReceiver&& orig) noexcept
{
    assert(this != &orig);
    pEcuScript_ = move(orig.pEcuScript_);
    pIsoTpSender_ = orig.pIsoTpSender_;
    pSessionCtrl_ = orig.pSessionCtrl_;
    securityAccessType_ = orig.securityAccessType_;
    orig.pIsoTpSender_ = nullptr;
    orig.pSessionCtrl_ = nullptr;
    return *this;
}

/**
 * Handles the received UDS messages and sends back the response like defined in
 * the according Lua script.
 *
 * @param buffer: the buffer containing the received data
 * @param num_bytes: the number of received bytes.
 * @see IsoTpSender::sendData()
 * @see EcuLuaScript::getRequestId()
 * @see EcuLuaScript::getResponseId()
 */
void UdsReceiver::proceedReceivedData(const uint8_t* buffer, const size_t num_bytes) noexcept
{
    IsoTpReceiver::proceedReceivedData(buffer, num_bytes);

    const uint8_t udsServiceIdentifier = buffer[0];
    const optional<string> response = pEcuScript_->getRawResponse(requestByteTree, buffer, num_bytes);

    if (response)
    {
        vector<unsigned char> raw = pEcuScript_->literalHexStrToBytes(*response);
        cout << "UDS sending: " << dec << raw.size() << " bytes." << endl;
        pIsoTpSender_->sendData(raw.data(), raw.size());
        pSessionCtrl_->reset();
    }
    else
    {
        switch (udsServiceIdentifier)
        {
            case READ_DATA_BY_IDENTIFIER_REQ:
            {
                readDataByIdentifier(buffer, num_bytes);
                pSessionCtrl_->reset();
                break;
            }
            case DIAGNOSTIC_SESSION_CONTROL_REQ:
                diagnosticSessionControl(buffer, num_bytes);
                break;
            case SECURITY_ACCESS_REQ:
                //                securityAccess(buffer, num_bytes);
                break;
                // TODO: implement all other requests ...
        default:
            array<uint8_t, 3> resp = {
                ERROR,
                udsServiceIdentifier,
                SERVICE_NOT_SUPPORTED
            };
            pIsoTpSender_->sendData(resp.data(), resp.size());
        }
    }
}

/**
 * Handles the UDS `readDataByIdentifier` request. The ISO-TP layer already
 * ensures the min. length of 3 bytes by filling up the request with zero bytes
 * if necessary.
 *
 * @param buffer: the buffer containing the UDS message
 * @param num_bytes: the length of the message in bytes (min. 3 bytes)
 */
void UdsReceiver::readDataByIdentifier(const uint8_t* buffer, const size_t num_bytes) noexcept
{
    assert(pSessionCtrl_ != nullptr);
    assert(pIsoTpSender_ != nullptr);

    const uint16_t dataIdentifier = (buffer[1] << 8) + buffer[2];
    string data;

    if (pSessionCtrl_->getCurrentUdsSession() == UdsSession::PROGRAMMING)
    {
        data = pEcuScript_->getDataByIdentifier(EcuLuaScript::toByteResponse(dataIdentifier, sizeof(dataIdentifier)), "Programming");
    }
    else if (pSessionCtrl_->getCurrentUdsSession() == UdsSession::EXTENDED)
    {
        data = pEcuScript_->getDataByIdentifier(EcuLuaScript::toByteResponse(dataIdentifier, sizeof(dataIdentifier)), "Extended");
    }
    else // default session
    {
        const string resp = EcuLuaScript::toByteResponse(dataIdentifier, sizeof(dataIdentifier));
        data = pEcuScript_->getDataByIdentifier(resp);
    }


    if (!data.empty())
    {
        // send positive response
        vector<uint8_t> resp(data.length() + 3); // data + UDS header
        resp = {
            READ_DATA_BY_IDENTIFIER_RES,
            buffer[1],
            buffer[2]
        };
        resp.insert(resp.cend(), data.cbegin(), data.cend()); // insert payload
        pIsoTpSender_->sendData(resp.data(), resp.size());
    }
    else
    {
        array<uint8_t, 3> nrc = {
            ERROR,
            0x22,
            SERVICE_NOT_SUPPORTED
        };
        pIsoTpSender_->sendData(nrc.data(), nrc.size());
    }
}

/**
 * Starts a session and sends back the corresponding response message.
 *
 * @param buffer: the buffer containing the UDS message
 * @param num_bytes: the length of the message in bytes
 */
void UdsReceiver::diagnosticSessionControl(const uint8_t* buffer, const size_t num_bytes)
{
    assert(pSessionCtrl_ != nullptr);

    const uint8_t sessionId = buffer[1];
    switch (sessionId)
    {
        case 0x01: // UdsSession::DEFAULT
            pSessionCtrl_->setCurrentUdsSession(UdsSession::DEFAULT);
            break;
        case 0x02: // UdsSession::PROGRAMMING
            pSessionCtrl_->setCurrentUdsSession(UdsSession::PROGRAMMING);
            pSessionCtrl_->start(SESSION_TIME);
            break;
        case 0x03: // UdsSession::EXTENDED
            pSessionCtrl_->setCurrentUdsSession(UdsSession::EXTENDED);
            pSessionCtrl_->start(SESSION_TIME);
            break;
        default:
            cerr << "Invalid session ID!\n";
            break;
    }

    const array<uint8_t, 2> resp = {
        DIAGNOSTIC_SESSION_CONTROL_RES,
        sessionId
    };
    pIsoTpSender_->sendData(resp.data(), resp.size());
}

/**
 *
 * @param buffer: the buffer containing the UDS message
 * @param num_bytes: the length of the message in bytes
 */
void UdsReceiver::securityAccess(const uint8_t* buffer, const size_t num_bytes) noexcept
{
    const uint8_t seedId = buffer[1];
    const string seed = pEcuScript_->getSeed(seedId);
    if (!seed.empty())
    {
        // send seed
        vector<uint8_t> resp(seed.length() + 2); // data + UDS header
        resp = {
            SECURITY_ACCESS_REQ,
            seedId
        };
        resp.insert(resp.cend(), seed.cbegin(), seed.cend() - 1); // insert payload - nullbyte
        pIsoTpSender_->sendData(resp.data(), resp.size());
        securityAccessType_ = seedId + 0x01;
    }
    else
    {
        if (securityAccessType_ == seedId)
        {
            // second request
            constexpr array<uint8_t, 1> resp = {SECURITY_ACCESS_RES};
            // Lua seed function
            pIsoTpSender_->sendData(resp.data(), resp.size());
            securityAccessType_ = 0x00;
        }
        else
        {
            array<uint8_t, 3> resp = {
                ERROR,
                0x27,
                SERVICE_NOT_SUPPORTED
            };
            pIsoTpSender_->sendData(resp.data(), resp.size());
        }
    }
}

/**
 * Generates a random 2 byte large unsigned number.
 *
 * @return a random `uint16_t` value
 */
uint16_t UdsReceiver::generateSeed()
{
    default_random_engine gen(RANDOM_DEVICE());
    uniform_int_distribution<uint16_t> dist(0, numeric_limits<uint16_t>::max());
    return dist(gen);
}
