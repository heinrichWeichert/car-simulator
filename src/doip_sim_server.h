#ifndef DOIP_SIM_SERVER_H
#define DOIP_SIM_SERVER_H

#include "doip_configuration_file.h"
#include "doip_simulator.h"
#include "DoIPServer.h"
#include <functional>
#include <thread>
#include <vector>

#define MAX_LOG_LENGTH 10

class DoIPSimServer
{
public:
    DoIPSimServer();
    ~DoIPSimServer();
    void startWithConfig(std::string configFilePath);
    void shutdown();
    void receiveFromLibrary(unsigned short address, unsigned char* data, int length);
    void sendDiagnosticResponse(const std::vector<unsigned char> data, unsigned short logicalAddress);
    void addECU(DoIPSimulator* ecu);
    bool isServerActive() { return serverActive; };
    DoIPServer* getServerInstance();
    std::vector<std::thread> doipReceiver;
    
private:
    DoIPServer* doipServer;
    DoipConfigurationFile* doipConfig;

    std::vector<DoIPSimulator*> ecus;
    std::unique_ptr<DoIPConnection> doipConnection;
    bool serverActive = false;
    
    bool diagnosticMessageReceived(unsigned short targetAddress);
    int findECU(unsigned short logicalEcuAddress);
    
    void configureDoipServer();
    void listenUdp();
    void listenTcp();
    void closeConnection();
   
};

#endif /* DOIP_SIM_SERVER_H */
