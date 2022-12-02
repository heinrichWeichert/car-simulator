#ifndef DOIP_SIMULATOR_H
#define DOIP_SIMULATOR_H

#include "DoIPServer.h"
#include "ecu_lua_script.h"
#include <functional>
#include <thread>
#include <vector>

class DoIPSimulator
{
public:
    static bool hasSimulation(EcuLuaScript *pEcuScript);

public:
    DoIPSimulator(EcuLuaScript *pEcuScript);
    vector<unsigned char> proceedDoIPData(const unsigned char* buffer, const size_t num_bytes) noexcept;

    unsigned short getLogicalEcuAddress() { return logicalEcuAddress; };

private:
    EcuLuaScript *pEcuScript_;
    shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> requestByteTree;
    unsigned short logicalEcuAddress;

};

#endif /* DOIP_SIMULATOR_H */