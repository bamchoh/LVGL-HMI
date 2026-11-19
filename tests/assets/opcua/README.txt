Test-only OPC UA application certificates for tests/test_driver_opcua_client.cpp (never use
them for anything else; the keys are public). Self-signed RSA 2048, DER, valid for 100 years:
server (urn:hml:test:server), other_server (an impostor with the same URI), client
(urn:hml:test:client).
