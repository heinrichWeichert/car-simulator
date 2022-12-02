#include "doip_simulator.h"
#include "service_identifier.h"
#include <iostream>

using namespace std;

bool DoIPSimulator::hasSimulation(EcuLuaScript *pEcuScript)
{
    if(pEcuScript->hasDoIPLogicalEcuAddress()) {
        return true;
    }
    return false;
}
/**
 * Constructor. Creates a DoIPServer for this simulator
 */
DoIPSimulator::DoIPSimulator(EcuLuaScript *pEcuScript) :
        pEcuScript_(pEcuScript) {
    logicalEcuAddress = pEcuScript->getDoIPLogicalEcuAddress();
    requestByteTree = pEcuScript->buildRequestByteTreeFromRawTable();
}

/**
 * Proceed received DoIP data
 * @param buffer        received DoIP data
 * @param num_bytes     length of data
 * @return              answer from the ecu config file
 */
vector<unsigned char> DoIPSimulator::proceedDoIPData(const unsigned char* buffer, const size_t num_bytes) noexcept {
    const string request = pEcuScript_->intToHexString(buffer, num_bytes);

    const optional<string> response = pEcuScript_->getRawResponse(requestByteTree, buffer, num_bytes);
    if (response)
    {
        vector<unsigned char> raw = pEcuScript_->literalHexStrToBytes(*response);
        cout << "DoIP UDS sending: " << dec << raw.size() << " bytes." << endl;
        return raw;
    } else {
        vector<unsigned char> negResponse = {
            ERROR, uint8_t(num_bytes > 0 ? buffer[0] : 0x00), SERVICE_NOT_SUPPORTED
        };
        cout << "DoIP UDS sending negative response." << endl;
        return negResponse;
    }
    return vector<unsigned char>();
}
