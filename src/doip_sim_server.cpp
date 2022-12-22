#include "doip_sim_server.h"

/**
 * Constructor. Creates a DoIPServer for this simulator
 */
DoIPSimServer::DoIPSimServer() :
        doipConnection(nullptr) {
    doipServer = new DoIPServer();
}

/**
 * Destructor.
 */
DoIPSimServer::~DoIPSimServer() {
    delete doipServer;
}

/**
 * Parse the given configuration file and start the doip server
 */
void DoIPSimServer::startWithConfig(std::string configFilePath) {

    doipConfig = new DoipConfigurationFile(configFilePath);
    configureDoipServer();
    
    doipServer->setupUdpSocket();
  
    serverActive = true;
    doipReceiver.push_back(std::thread(&DoIPSimServer::listenUdp, this));
    doipReceiver.push_back(std::thread(&DoIPSimServer::listenTcp, this));
    
    doipServer->sendVehicleAnnouncement();
}

void DoIPSimServer::shutdown() {
    serverActive = false;
    if(doipConnection) doipConnection->triggerDisconnection();
    doipServer->closeTcpSocket();
    doipServer->closeUdpSocket();
}

/**
 * Callback when connection is terminated
 */
void DoIPSimServer::closeConnection() {}

/**
 * Closes the connection from the server side
 */
void DoIPSimServer::triggerDisconnection() {
    if(doipConnection) doipConnection->triggerDisconnection();
}

void DoIPSimServer::sendVehicleAnnouncements() {
    doipServer->sendVehicleAnnouncement();
}

/*
 * Check permantly if udp message was received
 */
void DoIPSimServer::listenUdp() {

    while(serverActive) {
        doipServer->receiveUdpMessage();
    }
}

/*
 * Check permantly if tcp message was received
 */
void DoIPSimServer::listenTcp() {
    
    DiagnosticCallback receiveDiagnosticDataCallback = std::bind(&DoIPSimServer::receiveFromLibrary, 
            this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    DiagnosticMessageNotification notifyDiagnosticMessageCallback = std::bind(&DoIPSimServer::diagnosticMessageReceived, 
            this, std::placeholders::_1);
    CloseConnectionCallback closeConnectionCallback = std::bind(&DoIPSimServer::closeConnection, this);
    
    doipServer->setupTcpSocket();

    while(serverActive) {
        doipConnection = doipServer->waitForTcpConnection();
        doipConnection->setCallback(receiveDiagnosticDataCallback, notifyDiagnosticMessageCallback, closeConnectionCallback);
        doipConnection->setGeneralInactivityTime(50000);

         while(doipConnection->isSocketActive()) {
             doipConnection->receiveTcpMessage();
         }
         
    }
}

/**
 * Is called when the doip library receives a diagnostic message.
 * @param address   logical address of the ecu
 * @param data      message which was received
 * @param length    length of the message
 */
void DoIPSimServer::receiveFromLibrary(unsigned short address, unsigned char* data, int length) {
    printf("CarSimulator DoIP Simulator received: ");
    int logLength = (length > MAX_LOG_LENGTH ? MAX_LOG_LENGTH : length);
    for(int i = 0; i < logLength; i++) {
        printf("0x%02X ", data[i]);
    }
    printf(" from doip lib.\n");
    
    int index = findECU(address);
    if(index != -1) {
        std::vector<unsigned char> response = ecus.at(index)->proceedDoIPData(data,length);
        
        if(response.size() > 0) {
            unsigned short logicalAddress = ecus.at(index)->getLogicalEcuAddress();
            sendDiagnosticResponse(response, logicalAddress);
        }
    }
}

/**
 * Passes the received response from a ecu back to the doip library
 * @param data              respone from a ecu that will be send back
 * @param logicalAddress    logical address of the ecu where data came from
 */
void DoIPSimServer::sendDiagnosticResponse(const std::vector<unsigned char> data, unsigned short logicalAddress) {
    unsigned char* msg = new unsigned char[data.size()];
    for(unsigned int i = 0; i < data.size(); i++) {
        msg[i] = data[i];
    }
    
    doipConnection->sendDiagnosticPayload(logicalAddress, msg, data.size());
    delete[] msg;
}

/**
 * Adds a ECU to the list
 * @param ecu   Pointer to the ecu
 */
void DoIPSimServer::addECU(DoIPSimulator* ecu) {
    ecus.push_back(ecu);
}

/**
 * Will be called when the doip library receives a diagnostic message.
 * The library notifies the application about the message.
 * Checks if there is a ecu with the logical address
 * @param targetAddress     logical address to the ecu
 * @return                  If a positive or negative ACK should be send to the client
 */
bool DoIPSimServer::diagnosticMessageReceived(unsigned short targetAddress) {
    unsigned char ackCode;
    
    //if there isnt a ecu with the target address 
    if(findECU(targetAddress) == -1) {
        //send negative ack with unknown target address and return
        ackCode = 0x03;
        std::cout << "Send negative diagnostic message ack" << std::endl;
        doipConnection->sendDiagnosticAck(targetAddress, false, ackCode);
        return false;
    }
  
    //send positiv ack
    ackCode = 0x00;
    std::cout << "Send positive diagnostic message ack" << std::endl;
    doipConnection->sendDiagnosticAck(targetAddress, true, ackCode);
    return true;
}

/**
 * Find a ECU where the given address matches with the logical address
 * @param logicalEcuAddress   logical address of ECU to find
 * @return                    index of ecu in the vector
 */
int DoIPSimServer::findECU(unsigned short logicalEcuAddress) {
    int ecuIndex = -1;

    //Check if there is a running ecu where logicalAddress == targetAddress
    for(unsigned int i = 0; i < ecus.size(); i++) {
        unsigned short crntLogicalAddress = ecus.at(i)->getLogicalEcuAddress();
        
        if(crntLogicalAddress == logicalEcuAddress) {
            ecuIndex = i;
            break;
        }
    }
    
    return ecuIndex;
}

void DoIPSimServer::configureDoipServer() {
    
    std::string tempVIN = doipConfig->getVin();
    unsigned int tempLogicalAddress = doipConfig->getLogicalAddress();
    uint64_t tempEID = doipConfig->getEid();
    uint64_t tempGID = doipConfig->getGid();
    int tempFAR = doipConfig->getFurtherAction();
    
    int tempNum = doipConfig->getAnnounceNumber(); 
    int tempInterval = doipConfig->getAnnounceInterval();
        
    doipServer->setVIN(tempVIN);
    doipServer->setLogicalGatewayAddress(tempLogicalAddress);
    
    if(doipConfig->getEIDflag() == true)
    {
        doipServer->setEIDdefault();
    }
    else
    {
        doipServer->setEID(tempEID);
    }
    
    doipServer->setGID(tempGID);
    doipServer->setFAR(tempFAR);
    
    doipServer->setA_DoIP_Announce_Num(tempNum);
    doipServer->setA_DoIP_Announce_Interval(tempInterval);

    
}
