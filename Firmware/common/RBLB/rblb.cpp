#include "rblb.h"
#include "../CRC/crc.h"

#include <stdio.h>
#include <string.h>

RBLB::RBLB(uint64_t uid,
        void (*txFunc)(const uint8_t *buf, size_t size),
        void (*packetCallback)(uidCommHeader_t *header, uint8_t *payload),
        uint32_t (*getCurrentMillis)()) {
    _uid = uid;
    _sendBytes = txFunc;
    _packetCallback = packetCallback;
    _getCurrentMillis = getCurrentMillis;
}

void RBLB::handleByte(uint8_t byte) {
    if (_getCurrentMillis() - _lastByteReceived >= PACKET_TIMEOUT) {
        // discard previous packet if no new bytes arrived for some time
        _curReadIdx = 0;
    }

    if (_curReadIdx == 0) {
        if (byte >= DiscoveryInit) {    // uid command
            memset(_packetBuf, 0, sizeof(_packetBuf));
        }
    }

    if (_curReadIdx < sizeof(_packetBuf)) {
        _packetBuf[_curReadIdx] = byte;
    }

    // UID command
    if (_packetBuf[0] >= DiscoveryInit) {
        // complete header read?
        if (_curReadIdx >= sizeof(uidCommHeader_t)) {
            // complete data read? (need to check this separately, otherwise data length is unknown)
            uidCommHeader_t *header = (uidCommHeader_t*)_packetBuf;
            if (_curReadIdx >= (sizeof(uidCommHeader_t) + header->len + sizeof(uint16_t)) - 1) {

                if (header->len > sizeof(_packetBuf)) {
                    // can't handle oversized packet, don't do anything as of right now.
                    _curReadIdx = 0;
                    return;
                }


                // printf("Packet: ");
                // for (int i = 0; i <= _curReadIdx; i++) {
                //     printf("%02X ", _packetBuf[i]);
                // }
                // printf("\n");

                // check checksum
                uint16_t crc = crc16(_packetBuf, sizeof(uidCommHeader_t) + header->len);
                uint16_t packetCrc;
                memcpy(&packetCrc, _packetBuf + sizeof(uidCommHeader_t) + header->len, sizeof(uint16_t));

                if (crc == packetCrc) {
                    handlePacketInternal(header, header->data);
                }
                else {
                    // printf("Wrong CRC\n");
                }


                // received correct packet, so we don't need to rely on timeout to detect next packet
                _curReadIdx = 0;
                return;
            }
        }
    }
    else {  // data packet
        // TODO: filter out data relevant for oneself and parse it according to the config
        // save into buffer and only commit once datastream ends / crc/length correct for synchronization purposes
    }


    _curReadIdx++;
    _lastByteReceived = _getCurrentMillis();
}

void RBLB::handlePacketInternal(uidCommHeader_t *header, uint8_t *payload) {
    if (_uid != ADDR_HOST) {    // Am node
        if (header->address != _uid && header->address != ADDR_BROADCAST) {
            // not addressed to me
            return;
        }

        switch (header->cmd) {
            case DiscoveryInit:
                _discovered = false;
                break;
            case DiscoveryBurst:
                // TODO
                // Send time staggered? One discovery burst reply takes roughly 130 us @ 1MBaud
                // calculate timeslots based on baud rate, set maximum slots / delay, maybe 1ms? (~7 slots @ 1MBaud)
                break;
            case DiscoverySilence:
                _discovered = true;
                break;
            default:
                _packetCallback(header, payload);
                break;
        }
    }
    else {                      // Am host
        // TODO: evaluate need to add a bit for packet direction or is direction always implied despite using address field for both src and dst?
        // if (header->address != _uid) {
        //     // not addressed to me
        //     return;
        // }

        switch (header->cmd) {
            // TODO: handle RBLB discovery internally
            default:
                _packetCallback(header, payload);
                break;
        }
    }
}

size_t RBLB::sendPacket(uint8_t cmd, uint64_t dstUid, const uint8_t *payload, size_t payloadSize) {
    uint16_t packetSize = sizeof(uidCommHeader_t) + payloadSize + sizeof(uint16_t);     // header + payload + CRC
    uint8_t buf[packetSize];
    uidCommHeader_t *header = (uidCommHeader_t *)buf;

    header->cmd = cmd;
    header->address = dstUid;
    header->len = payloadSize;
    if (payload != NULL) {
        memcpy(buf + sizeof(uidCommHeader_t), payload, payloadSize);    // copy payload to packet buffer
    }
    uint16_t crc = crc16(buf, sizeof(uidCommHeader_t) + payloadSize);   // calculate CRC and store at end of packet
    memcpy(buf + sizeof(uidCommHeader_t) + payloadSize, &crc, sizeof(uint16_t));

    _sendBytes(buf, packetSize);
    return packetSize;
}
