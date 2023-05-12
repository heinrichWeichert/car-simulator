# ![ProjectLogo](resources/images/ProjectLogoThumb.png) amos-ss17-proj4 [![Build Status](https://travis-ci.org/christian-reintges/amos-ss17-proj4.svg?branch=master)](https://travis-ci.org/christian-reintges/amos-ss17-proj4)

## Raspberry Pi as a simulated car

This project aims to provide a [UDS](https://en.wikipedia.org/wiki/Unified_Diagnostic_Services) mocking system based on a Raspberry Pi (+ CAN shield) driven by Lua scrips.

[_Selene_ Lua C++ Binding](Selene)  

## Requirements

 Tool / Library      | Version
-------------------- | ------------------
Lua                  | >= 5.2
C++ compiler         | >= ISO C++14
can-isotp            | >= git 2017/04/22
can-utils (optional) | >= git 2015/09/02
CppUnit (optional)   | >= 1.10
Doxygen (optional)   | >= 1.8
graphviz (optional)  | >= 2.38

For instructions to set up all necessary installations, read the [HACKME](howto/HACKME.md).

### Using the Lua scripts

#### Basics Communication

To simulate a ECU (Electronic Control Unit), the Lua file hold various tables describing the data and behavior. Therefore a single ECU has to provide at least a `RequesId` and a `ResponseId` to be able to communicate via the CAN ISO-TP protocol. Note: A single Lua file could contain various ECUs.

```lua
PCM = { -- ECU name
    RequestId = 0x100,
    ResponseId = 0x200,
    BroadcastId = 0x300, -- Optional, 0x7DF on default
}
```

#### Providing the Simulation Data

To provide a set of response data, there are two possibilities. The first option is to do this via a `ReadDataByIdentifier`-table, which holds a set of receiving requests and the corresponding answers. The response answer could be a string or a numerical type. The second option is to provide a `Raw`-table which does basically the same, with the slightly difference, that the entire data is provided as a literal hexadecimal string. This makes it possible to harness data sets from previous scans or logs. However, white-spaces in-between the string bytes are ignored to allow a easier way to separate the data sections.

```lua
PCM = {
    RequestId = 0x100,
    ResponseId = 0x200,

    ReadDataByIdentifier = {
        ["F1 90"] = "SALGA2EV9HA298784",
        ["F1 24"] = "HPLA-12345-AB",
        ["1E 23"] = "231132",
    },

    Raw = {
        ["10 02"] = "50 02 00 19 01 F4",
        ["22 FA BC"] = "10 33 11",
    }
}
```

#### Sending multiple responses

In scenarios where an ECU responds with multiple consecutives responses (physically), it is possible to send more than one response via a callback.
These responses will be sent as they occur. Inside the function invoking the callback, other actions like `sleep` can be used to simulate delays.

Example:

```lua
PCM = {
    RequestId = 0x100,
    ResponseId = 0x200,

    ReadDataByIdentifier = {
        ["F1 90"] = "SALGA2EV9HA298784",
        ["F1 24"] = "HPLA-12345-AB",
        ["1E 23"] = "231132",
    },

    Raw = {
        ["10 02"] = "50 02 00 19 01 F4",
        ["22 FA BC"] = function(request, link)
            link.send("10 33 11")
            link.send("10 33 12")
            sleep(1000)
            link.send("10 33 13")
        end
    }
}
```

#### Integrated Functions

Since it could be a little inconvenient to provide the entire data set in a static, Look-Up-Table styled way, there are also functions to allow a more advanced behavior.  

The following functions are available:

* `ascii(string)` – Converts a UTF-8 string into a lexical hexadecimal byte string
* `toByteResponse(number, number)` – Converts a int number into a hexadecimal byte string
* `getCurrentSession()` – Returns the current session
* `switchToSession(number)` – Sets ECU in the given session
* `sleep(number)` – Sleeps the amount in milliseconds before proceeding any further

All these functions could be used in self defined functions to build a more advanced behavior structure.  

Example:

```lua
...
    Raw = {
        ["22 F1 91"] = "62 F1 91" .. ascii("SALGA2EV9HA298784"),
        ["19 02 B1"] = function (request)
            myNumber = 21550
            return "59 02 FF E3" .. toByteResponse(myNumber, 3) -- "59 02 FF E3 00 54 2E" 
        end,

        ["19 02 B2"] = function (request)
            session = getCurrentSession()
            return "47 11" .. toByteResponse(session, 1) -- "47 11 01"
        end,

        ["19 02 B3"] = function (request)
            switchToSession(3) -- extended session
            session = getCurrentSession()
            return "47 11" .. toByteResponse(session, 1) -- "47 11 03"
        end
    },
...
