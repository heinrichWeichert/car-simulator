/**
 * @file j1939_simulator.h
 *
 */

#ifndef J1939_SIMULATOR_H
#define J1939_SIMULATOR_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <thread>

#include "ecu_lua_script.h"

constexpr uint32_t J1939_PGN_REQUESTPGN = 0xEA00;
constexpr uint32_t J1939_PGN_ACKPGN = 0xE800;
constexpr uint8_t J1939_BROADCAST_ID = 0xFF;

class J1939Simulator
{
public:
    static bool hasSimulation(EcuLuaScript *pEcuScript);

public:
    J1939Simulator() = delete;
    J1939Simulator(const std::string& device,
                   EcuLuaScript* pEcuScript);
    virtual ~J1939Simulator();
    int openReceiver() noexcept;
    void closeReceiver() noexcept;
    int readDataThread() noexcept;
    void startPeriodicSenderThreads();
    void processReceivedData(const uint8_t* buffer, const size_t num_bytes, const uint8_t sourceAddress, const uint32_t pgn) noexcept;
    std::vector<unsigned char> assembleACK(const std::string ackInfoByteString, const uint8_t targetAddress, const uint32_t pgn);
    void sendCyclicMessage(const std::string pgn) noexcept;

    void stopSimulation();
    void waitForSimulationEnd();


private:
    uint8_t source_address_;
    std::string device_;
    EcuLuaScript* pEcuScript_;
    int receive_skt_ = -1;
    bool isOnExit_ = false;
    std::thread *j1939ReceiverThread_;
    std::vector<std::thread*> cyclicMessageThreads;

    sel::State lua_state_;
    uint16_t *pgns_;

    int openCyclicSendSocket() const noexcept;
    int openJ1939Socket(const uint8_t node_address) const noexcept;
    ssize_t sendJ1939Message(int skt, struct sockaddr_can saddr, std::vector<unsigned char> payload) noexcept;

    uint32_t parsePGN(std::string pgn) const noexcept;
    bool isBusActive();

};

#endif // J1939_SIMULATOR_H