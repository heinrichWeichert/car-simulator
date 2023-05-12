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
        ["10 02"] = "50 02 00 19 01 f4",
        ["22 fa bc"] = "10 33 11",
        -- As an alternative, requests and responses can be specified
        -- using concatenation, functions or any other language
        -- features of lua
        ["22 F1 91"] = "62 F1 91" .. ascii("SALGA2EV9HA298784"),
        ["19 02 AF"] = function(request, link)
            ses01 = getCurrentSession()
            link.send("current session: " .. ses01)
            switchToSession(2)
            sleep(2000)
            ses02 = getCurrentSession()
            link.send("Session after 'switchToSession()': " .. ses01)
            link.send("7f 19 78")
            sleep(2000)
            link.send("7f 19 78")
            sleep(2000)
            link.send("7f 19 78")
            sleep(1000)
            link.send("59 02 ff e3 00 54 2f")
        end
    },
    Seed = {
        [0x01] = "0x4455",
        [0x03] = "0x6677"
    }
}
