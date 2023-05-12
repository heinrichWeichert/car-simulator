-----------------------------------------------------------------------
-- These functions are only stubs to  make this script run through the
-- lua interpreter. Do NOT include these in the lua file but rather
-- implement them in C++

----- function ascii()
-- Convert the given string into another string that represents
-- the hex bytes of the input string.
-- This is a convenience function to use ascii strings in responses.
-- Example:
--     ascii("Hello") -> " 48 65 6c 6c 6f "
function ascii(asciistring)
    return 1
end

----- function toByteResponse()
-- Convert the given integer value into a hex byte string as used
-- in requests and responses. The parameter len gives the number
-- of bytes that should be returned
-- Examples:
--     toByteResponse(2, 13248) -> "33 C0"
--     toByteResponse(3, 13248) -> "00 33 C0"
function toByteResponse(len, intvalue)
    return 1
end

----- function sleep()
-- suspend the script for the given number of milliseconds
function sleep(ms)
    return 1
end

----- function getCurrentSession()
-- Returns the currently active diagnostic session to be
-- used in custom functions
function getCurrentSession()
    return 1
end

----- function switchToSession()
-- Switch to the given (numeric) diagnostic session
function switchToSession(diagnosticSessionType)
    return 1
end

-----------------------------------------------------------------------
-- General remark regarding byte strings:
-- Requests and Responses are defined using standard lua strings.
-- Strings consist of hex bytes, optionally separated by spaces
--    Example: "22 F1 90"
-- Border conditions:
-- - The strings are not case sensitive
--   i.e. "22 f1 90" and "22 F1 90" are the same
-- - Spaces are ignored and thus optional
--   i.e. "22F190", "22 F190" and "22 F1 90" are the same

-----------------------------------------------------------------------

-- Global variables
myCounter = 0

-- One section represents one ECU. In this example it defines an
-- engine control, or powertrain control module (PCM).
-- These acronyms can be arbitrary
--
PCM = {
    -- These parameters define the ECU addresses as
    -- used by the transport layer (CAN ISO-TP)
    RequestId = 0x7e0,
    ResponseId = 0x7e8,
    RequestFunctionalId = 0x7df,
    -- Supported Sessions
    -- The first element is always the DefaultSession, to which the
    -- state is returned after S3 Timeout or EcuReset
    Sessions = {
        0x01, -- DefaultSession
        0x02, -- ProgrammingSession
        0x03, -- ExtendedSession
        0x40 -- EndOfLineSession
    },
    -- The section "Raw" specifies simple request/response pairs
    -- That means as soon as simulation receives the specified
    -- request (e.g. 10 02) it sends out the specified response
    -- (e.g. 50 02 00 19 01 f4)
    -- *Requests defined in the Raw section are always preferred
    -- over any service specific definition*
    Raw = {
        -- Requests and responses are specified as hex bytes in
        -- lua strings. See definition earlier in this document.
        ["10 02"] = "50 02 00 19 01 f4",
        ["22 fa bc"] = "10 33 11",
        -- As an alternative, requests and responses can be specified
        -- using concatenation, functions or any other language
        -- features of lua
        ["22 F1 90"] = "62 F1 90" .. ascii("SALGA2EV9HA298784"),
        -- In addition, functions (optinally inline) can be defined
        -- to dynamically generate the response. This function is called
        -- whenever the given request is received. The parameter given is
        -- the request itself as hex string. This example increases a simple
        -- counter on every request and sends the new value in the response
        ["22 12 89"] = function(request)
            myCounter = myCounter + 1
            response = "62 12 89" .. toByteResponse(2, myCounter)
            return response
        end,
        -- This example emulates ResponsePending messages, followed
        -- by the final response
        ["19 02 af"] = function(request, link)
            link.send("7f 19 78")
            sleep(2000)
            link.send("7f 19 78")
            sleep(2000)
            link.send("7f 19 78")
            sleep(1000)
            link.send("59 02 ff e3 00 54 2f")
        end
    },
    -- Specification of the service ReadDataByIdentifier
    -- This section defines a mapping between identifier and value.
    -- That means e.g. request "22 1e 23" will lead to
    -- response "62 1e 23 23 11 32"
    ReadDataByIdentifier = {
        ["1e23"] = "23 11 32",
        -- as with raw request/response, any function or
        -- lua language feature can be used
        ["f190"] = ascii("SALGA2EV9HA298784"),
        ["f124"] = ascii("HPLA-12345-AB"),
        -- a value of "nil" tells the simulation to
        -- *not* send any reponse but rather ignore
        -- the request
        ["fabc"] = nil,
        -- as with raw request/response an inline function can be used
        ["12ab"] = function(request, link)
            link.send("7f 22 78")
            sleep(1000)
            -- the return value specified the identifier value to be
            -- sent, not the whole response. i.e. the line below will
            -- lead to the response "62 12 ab 11 22 33 44 AA BB CC DD"
            link.send("11 22 33 44 AA BB CC DD")
        end
    },
    -- (To be defined) Specification of the service DiagnosicSessionControl
    -- In general the session and TesterPresent handling should be done
    -- in the simulation framework according to ISO14229-1.
    -- This section should only be used to override this behavior
    DiagnosticSessionControl = {},
    -- (to be defined) Specification of the service ClearDiagnosticInformation
    ClearDiagnosticInformation = {},
    -- (to be defined) more sections for other services

    -- The postReceive function is called as soon as a request was received.
    -- This allows to modify the request before processing it.
    postReceive = function(request)
        if request == "221789" then
            return "221788"
        else
            return request
        end
    end,
    -- The preSend function is called just before a response is sent.
    -- This allows to modify the response.
    preSend = function(response)
        if string.sub(response, 1, 2) == "621788" then
            return "621789" .. string.sub(response, 3)
        else
            return response
        end
    end
}

-- Another ECU definition
BCM = {}
