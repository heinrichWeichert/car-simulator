
#include "j1939_simulator.h"
#include "can/j1939.h"
#include <linux/can.h>
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <libsocketcan.h>


#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>



using namespace std;

constexpr size_t MAX_BUFSIZE = 1788; // 255*7 Byte + 3 byte PGN

bool J1939Simulator::hasSimulation(EcuLuaScript *pEcuScript)
{
    if(pEcuScript->hasJ1939SourceAddress()) {
        return true;
    }
    return false;
}

J1939Simulator::J1939Simulator(const std::string& device,
                               EcuLuaScript *pEcuScript)
: device_(device)
, pEcuScript_(pEcuScript)
, isOnExit_(false)
{
    source_address_ = pEcuScript->getJ1939SourceAddress();

    int err = openReceiver();
    if (err != 0)
    {
        throw exception();
    }

    j1939ReceiverThread_ = new std::thread(&J1939Simulator::readDataThread, this);
    startPeriodicSenderThreads();
}

void J1939Simulator::stopSimulation()
{
    closeReceiver();
}

void J1939Simulator::waitForSimulationEnd()
{
    for (auto cyclicThread : cyclicMessageThreads) {
        cyclicThread->join();
        delete cyclicThread;
    }
    j1939ReceiverThread_->join();
    delete j1939ReceiverThread_;
}


J1939Simulator::~J1939Simulator()
{
    for (auto cyclicThread : cyclicMessageThreads) {
        delete cyclicThread;
    }
    cyclicMessageThreads.clear();
}

void J1939Simulator::startPeriodicSenderThreads()
{
    vector<string> pgnDefinitions = pEcuScript_->getJ1939PGNs();
    cout << "Found " << pgnDefinitions.size() << " PGN definitions in simulation" << endl;

    for (auto pgnDefinition : pgnDefinitions) {
        size_t separatorPos = pgnDefinition.find_first_of('#');
        // cyclic sent PGNs or PGNs requested via EA00
        if(separatorPos == string::npos) {
            cout << "Found PGN " << pgnDefinition << " as cyclic PGN or to be requested via EA00" << endl;
            std::thread *cyclicThread = new std::thread(&J1939Simulator::sendCyclicMessage, this, pgnDefinition);
            cyclicMessageThreads.push_back(cyclicThread);
        }
    }
    cout << "PGN threads started" << endl;
}

/**
 * Opens the socket for receiving J1939 PGNs
 *
 * @return 0 on success, otherwise a negative value
 * @see J1939Simulator::closeReceiver()
 */
int J1939Simulator::openReceiver() noexcept
{
    int skt = openJ1939Socket(source_address_);
    if (skt < 0)
    {
        cerr << __func__ << "() socket: " << strerror(errno) << '\n';
        return -1;
    }
    receive_skt_ = skt;
    return 0;
}

/**
 * Closes the socket for receiving data.
 * 
 * @see J1939Simulator::openReceiver()
 */
void J1939Simulator::closeReceiver() noexcept
{
    isOnExit_ = true;

    if (receive_skt_ < 0)
    {
        cerr << __func__ << "() Receiver socket is already closed!\n";
        return;
    }
    close(receive_skt_);
    receive_skt_ = -1;
}

/**
 * Reads the data from the opened receiver socket. The handling of the received
 * data is controlled by the inside nested `proceedReceivedData()` function.
 * 
 * @return 0 on success, otherwise a negative value
 */
int J1939Simulator::readDataThread() noexcept
{
    if (receive_skt_ < 0)
    {
        cerr << __func__ << "() Can not read data. J1939 receiver socket invalid!\n";
        return -1;
    }

    uint8_t msg[MAX_BUFSIZE];
    size_t num_bytes;
    struct sockaddr_can saddr = {};
    socklen_t addrlen = sizeof(saddr);

    do
    {
        cout << "Reading from socket" << endl;
        num_bytes = recvfrom(receive_skt_, msg, sizeof(msg), 0, (struct sockaddr *)&saddr, &addrlen);

        cout << "Message received from: " << hex << (unsigned short)saddr.can_addr.j1939.addr << dec << endl;
        cout << " -> PGN: " << hex << (unsigned short)saddr.can_addr.j1939.pgn << dec << endl;

        if (num_bytes >= 0 && num_bytes < MAX_BUFSIZE)
        {
            processReceivedData(msg, num_bytes, saddr.can_addr.j1939.addr, saddr.can_addr.j1939.pgn);
        }
    }
    while (!isOnExit_);

    return 0;
}

/**
 * Prints out the received data and should be called once per received message
 * 
 * @see J1939Simulator::readData()
 */
void J1939Simulator::processReceivedData(const uint8_t* buffer, const size_t num_bytes, const uint8_t sourceAddress, const uint32_t pgn) noexcept
{
    // Print out what we received
    cout << __func__ << "() Received " << dec << num_bytes << " bytes.\n";
    for (size_t i = 0; i < num_bytes; i++)
    {
        cout << " 0x" << hex << setw(2) << setfill('0') << static_cast<int> (buffer[i]);
    }
    cout << endl;

    string pgnString = "";
    
    uint8_t pgnBuffer[3];
    pgnBuffer[0] = (uint8_t)(pgn >> 0);
    pgnBuffer[1] = (uint8_t)(pgn >> 8);
    pgnBuffer[2] = (uint8_t)(pgn >> 16);
    pgnString += pEcuScript_->intToHexString(pgnBuffer, sizeof(pgnBuffer));
    pgnString += " #";
    string pgnRequestPayload = pEcuScript_->intToHexString(buffer, num_bytes);

    cout << "Looking for PGN " << pgnString << " - Payload: " << pgnRequestPayload << endl;

    string pgnResponse = pEcuScript_->getJ1939PGNData(pgnString, pgnRequestPayload).payload;
    cout << "-> Response: " << pgnResponse << endl;

    struct sockaddr_can saddr = {};
    saddr.can_family = AF_CAN;
    saddr.can_addr.j1939.name = J1939_NO_NAME;
    saddr.can_addr.j1939.addr = sourceAddress;

    vector<unsigned char> responsePayload;

    if(pgnResponse != "") {
        uint32_t respondingPgn;

        size_t separatorPos = pgnResponse.find_first_of('#');
        if(separatorPos != string::npos) {
            respondingPgn = parsePGN(pgnResponse.substr(0,separatorPos));
            pgnResponse = pgnResponse.substr(separatorPos + 1, string::npos);
            responsePayload = pEcuScript_->literalHexStrToBytes(pgnResponse);
        } else if(pgnResponse.substr(0,3) == "ACK") {
            respondingPgn = 0xE800;
            saddr.can_addr.j1939.addr = 0xff;
            responsePayload = assembleACK(pgnResponse.substr(3, string::npos), sourceAddress, pgn);
        } else {
            respondingPgn = pgn;
            responsePayload = pEcuScript_->literalHexStrToBytes(pgnResponse);
        }
        saddr.can_addr.j1939.pgn = respondingPgn;

        sendJ1939Message(receive_skt_, saddr, responsePayload);
    } else if(pgn == 0xEA00) {
        uint32_t requestedPgn = parsePGN(pgnRequestPayload);
        cout << "Requested PGN: " << requestedPgn << endl;
        saddr.can_addr.j1939.pgn = requestedPgn;
        string pgnResponse = pEcuScript_->getJ1939PGNData(pgnRequestPayload).payload;
        cout << "-> Response: " << pgnResponse << endl;

        responsePayload = pEcuScript_->literalHexStrToBytes(pgnResponse);
        sendJ1939Message(receive_skt_, saddr, responsePayload);
    }
}

ssize_t J1939Simulator::sendJ1939Message(int skt, struct sockaddr_can saddr, vector<unsigned char> payload) noexcept
{
    ssize_t sentBytes = sendto(skt, payload.data(), payload.size(), 0, (const struct sockaddr *)&saddr, sizeof(saddr));
    cout << "sentBytes: " << sentBytes << endl;
    if(sentBytes < 0) {
        cout << "Unable to send PGN " << saddr.can_addr.j1939.pgn << ": " << strerror(errno) << endl;
    }
    return sentBytes;
}

/**
 * @brief Put together ACK message according to J1939-21, 5.4.4 Acknowledgement
 * 
 * @param ackInfoByteString 
 * @param targetAddress 
 * @param pgn 
 * @return vector<unsigned char> 
 */
vector<unsigned char> J1939Simulator::assembleACK(const string ackInfoByteString, const uint8_t targetAddress, const uint32_t pgn) {
    vector<unsigned char> rawMessage;

    vector<unsigned char> ackInfo = 
        pEcuScript_->literalHexStrToBytes(ackInfoByteString);
    if(ackInfo.size() > 0) { // Control byte
        rawMessage.push_back(ackInfo[0]);
    } else {
        rawMessage.push_back(0x00);
    }
    if(ackInfo.size() > 1) { // Group Function Value
        rawMessage.push_back(ackInfo[1]);
    } else {
        rawMessage.push_back(0x00);
    }
    rawMessage.push_back(0xFF); // Reserved
    rawMessage.push_back(0xFF); // Reserved
    rawMessage.push_back(targetAddress); // Address Acknowledged
    rawMessage.push_back((uint8_t)(pgn >> 0)); // PGN of requested information
    rawMessage.push_back((uint8_t)(pgn >> 8)); // PGN of requested information
    rawMessage.push_back((uint8_t)(pgn >> 16)); // PGN of requested information
    return rawMessage;
}

void J1939Simulator::sendCyclicMessage(const string pgn) noexcept
{
    uint32_t pgnNum = parsePGN(pgn);
    cout << "Sending Cyclic PGN: " << pgn << " as " << pgnNum << endl;

    struct sockaddr_can saddr = {};
    saddr.can_family = AF_CAN;
    saddr.can_addr.j1939.name = J1939_NO_NAME;
    saddr.can_addr.j1939.pgn = pgnNum;
    saddr.can_addr.j1939.addr = 0xff;


    do {
        J1939PGNData pgnData = pEcuScript_->getJ1939PGNData(pgn);
        string pgnMessage = pgnData.payload;
        vector<unsigned char> rawMessage = pEcuScript_->literalHexStrToBytes(pgnMessage);
        unsigned int cycleTime = pgnData.cycleTime;
        if(cycleTime == 0) {
            return;
        }

        if(isBusActive()) {
            int sendSkt = openCyclicSendSocket();

            int retries = 5;
            while(retries > 0)
            {
                cout << "Trying to send PGN: " << dec << pgnNum << endl;

                int sendResult = sendto(sendSkt, rawMessage.data(), rawMessage.size(), MSG_DONTWAIT, (const struct sockaddr *)&saddr, sizeof(saddr));
                if(sendResult < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        retries--;
                        cout << "Sending PGN " << dec << pgnNum << " blocked - " << retries << " retries remaining." << endl;
                        usleep(1000 * 50); // wait for 50ms before retry
                    } else {
                        cout << "Error sending PGN: " << strerror(errno) << endl;
                        retries = 0;
                    }
                }
                else
                {
                    retries = 0;
                }
            }

            close(sendSkt);

            cout << "PGN sent: " << pgn << endl;

        }

        usleep(1000 * cycleTime); // takes microseconds

    } while (!isOnExit_);
}

bool J1939Simulator::isBusActive()
{
    int state;
    if(can_get_state(this->device_.c_str(), &state) < 0) {
        cerr << "Unable to get status for " << this->device_ << " assuming state OFF" << endl;
        return false;
    }

    if(state == CAN_STATE_ERROR_ACTIVE || state == CAN_STATE_ERROR_WARNING) {
        return true;
    }
    return false;
}

/**
 * @brief convert a PGN  specified in the lua file to an integer
 * This tries to determine which of the valid styles of defining a PGN
 * is used and tries to convert it into an integer.
 * Valid formats are: "65226" (decimal), "CA FE 00" (little endian hex) or even "CAFE00".
 * 
 * Note that this does not work properly for the last syntax.
 * Specifying "011100" is abiguous and would be interpreted as decimal number.
 * Numbers that have more than 5 digits are always in little endian hex
 * 
 * @param pgn 
 * @return * this 
 */
uint32_t J1939Simulator::parsePGN(string pgn) const noexcept
{
    uint32_t pgnNum = 0;
    // try to parse the number as decimal if the string is not more than 5 digits long
    if(strnlen(pgn.c_str(),6) < 6) {
        pgnNum = strtol(pgn.c_str(), NULL, 10);
    }

    // If number parsing fails or number is longer than 5 digits, try parsing a string instead
    if(pgnNum == 0 || pgnNum > 99999) {
        vector<uint8_t> pgnBytes = pEcuScript_->literalHexStrToBytes(pgn);
        pgnNum = 0;
        if(pgnBytes.size() <= 3) {
            // put byte together in reverse order (little endian)
            for(int i = pgnBytes.size()-1; i >= 0; i--) {
                pgnNum = pgnNum << 8;
                pgnNum |= pgnBytes[i] & 0xffu;
            }
        }
    }

    return pgnNum;
}

/**
 * Opens a socket to send a J1939 PGN
 *
 * @return socket
 */
int J1939Simulator::openCyclicSendSocket() const noexcept
{
    return openJ1939Socket(source_address_);
}

/**
 * Opens a socket for sending/receiving J1939 PGNs
 *
 * @return 0 on success, otherwise a negative value
 */
int J1939Simulator::openJ1939Socket(const uint8_t node_address) const noexcept
{
    // see also: https://www.kernel.org/doc/html/latest/networking/j1939.html
    struct sockaddr_can addr = {};
    addr.can_family = AF_CAN;
    addr.can_addr.j1939.pgn = J1939_NO_PGN; // Receive all PGNs
    addr.can_addr.j1939.name = J1939_NO_NAME; // Don't do name lookups
    addr.can_addr.j1939.addr = node_address; // source address

    int skt = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
    if (skt < 0)
    {
        cerr << __func__ << "() socket: " << strerror(errno) << endl;
        return -1;
    }

    struct ifreq ifr = {};
    strncpy(ifr.ifr_name, device_.c_str(), device_.length() + 1);
    ioctl(skt, SIOCGIFINDEX, &ifr);
    addr.can_ifindex = ifr.ifr_ifindex;
    int value = 1;
    setsockopt(skt, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));

    auto bind_res = bind(skt,
                         reinterpret_cast<struct sockaddr*> (&addr),
                         sizeof(addr));
    if (bind_res < 0)
    {
        cerr << __func__ << "() bind: " << strerror(errno) << '\n';
        close(skt);
        return -2;
    }

    return skt;
}