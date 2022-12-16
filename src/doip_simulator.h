#ifndef DOIP_SIMULATOR_H
#define DOIP_SIMULATOR_H

#include "DoIPServer.h"
#include "ecu_lua_script.h"
#include "request_byte_tree_node.h"
#include <functional>
#include <thread>
#include <vector>

class EcuLuaScript;

class DoIPSimulator
{
public:
    static bool hasSimulation(EcuLuaScript *pEcuScript);

public:
    DoIPSimulator(EcuLuaScript *pEcuScript);
    std::vector<unsigned char> proceedDoIPData(const unsigned char* buffer, const size_t num_bytes) noexcept;

    unsigned short getLogicalEcuAddress() { return logicalEcuAddress; };

private:
    EcuLuaScript *pEcuScript_;
    std::shared_ptr<RequestByteTreeNode<shared_ptr<Selector>>> requestByteTree;
    unsigned short logicalEcuAddress;

};

#endif /* DOIP_SIMULATOR_H */