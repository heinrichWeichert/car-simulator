PCM = {
    RequestId = 0x100,
    ResponseId = 0x200,

    ReadDataByIdentifier = {
        ["F1 90"] = "SALGA2EV9HA298784",
        ["F1 24"] = "HPLA-12345-AB",
        ["F1 23"] = nil,
        ["FA BC"] = nil,
        ["1E 23"] = "231132",
    },

    Raw = {
        ["10 02"] = "50 02 00 19 01 f4",
        ["22 fa bc"] = "10 33 11",

        -- As an alternative, requests and responses can be specified
        -- using concatenation, functions or any other language
        -- features of lua
        ["22 F1 91"] = "62 F1 91" .. ascii("SALGA2EV9HA298784")
    },

   Seed = {
    	[0x01] = "0x4455",
    	[0x03] = "0x6677",
    }
}

