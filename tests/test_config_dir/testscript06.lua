PCM = {
    RequestId = 0x100,
    ResponseId = 0x200,
    BroadcastId = 0x300,
    ReadDataByIdentifier = {
        ["F1 90"] = "SALGA2EV9HA298784",
        ["F1 24"] = "HPLA-12345-AB",
        ["F1 23"] = nil,
        ["FA BC"] = nil,
        ["1E 23"] = "231132"
    },
    Raw = {
        ["10 02"] = "50 02 00 19 01 F4",
        ["22 FA BC"] = "10 33 11",
        ["7F 19 78"] = "FA BF AB FA BF AB",
        -- As an alternative, requests and responses can be specified
        -- using concatenation, functions or any other language
        -- features of lua
        ["22 F1 91"] = "62 F1 91" .. ascii("SALGA2EV9HA298784"),
        ["19 02 B1"] = function(request)
            myNumber = 21550
            return "59 02 FF E3" .. toByteResponse(myNumber, 3) -- "59 02 FF E3 00 54 2E"
        end,
        ["19 02 B2"] = function(request)
            session = getCurrentSession()
            return "47 11" .. toByteResponse(session, 1) -- "47 11 01"
        end,
        ["19 02 B3"] = function(request)
            switchToSession(3) -- extended session
            session = getCurrentSession()
            return "47 11" .. toByteResponse(session, 1) -- "47 11 03"
        end,
        ["19 02 AF"] = function(request, link)
            link.send("7F 19 78")
            sleep(5000)
            link.send("59 02 FF E3 00 54 2F")
        end
    },
    Seed = {
        [0x01] = "0x4455",
        [0x03] = "0x6677"
    }
}
