/**
 * Copyright (c) 2015-2016, The Kovri I2P Router Project
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <cryptopp/gzip.h>

#include <string.h>

#include <atomic>
#include <vector>
#include <set>

#include "Garlic.h"
#include "I2NPProtocol.h"
#include "NetworkDatabase.h"
#include "RouterContext.h"
#include "crypto/ElGamal.h"
#include "transport/Transports.h"
#include "tunnel/Tunnel.h"
#include "util/I2PEndian.h"
#include "util/Timestamp.h"

// TODO(anonimal): do not use namespace using-directives
using namespace i2p::transport;
namespace i2p {

I2NPMessage* NewI2NPMessage() {
  return new I2NPMessageBuffer<I2NP_MAX_MESSAGE_SIZE>();
}

I2NPMessage * NewI2NPShortMessage() {
  return new I2NPMessageBuffer<I2NP_MAX_SHORT_MESSAGE_SIZE>();
}

I2NPMessage* NewI2NPMessage(
    size_t len) {
  return (len < I2NP_MAX_SHORT_MESSAGE_SIZE/2) ?
    NewI2NPShortMessage() :
    NewI2NPMessage();
}

void DeleteI2NPMessage(
    I2NPMessage* msg) {
  delete msg;
}

std::shared_ptr<I2NPMessage> ToSharedI2NPMessage(
    I2NPMessage* msg) {
  return std::shared_ptr<I2NPMessage>(msg, DeleteI2NPMessage);
}

void I2NPMessage::FillI2NPMessageHeader(
    I2NPMessageType msgType,
    uint32_t replyMsgID) {
  SetTypeID(msgType);
  if (replyMsgID)  // for tunnel creation
    SetMsgID(replyMsgID);
  else
    SetMsgID(i2p::context.GetRandomNumberGenerator().GenerateWord32());
  // TODO(unassigned): 5 secs is a magic number
  SetExpiration(i2p::util::GetMillisecondsSinceEpoch() + 5000);
  UpdateSize();
  UpdateChks();
}

void I2NPMessage::RenewI2NPMessageHeader() {
  SetMsgID(i2p::context.GetRandomNumberGenerator().GenerateWord32());
  SetExpiration(i2p::util::GetMillisecondsSinceEpoch() + 5000);
}

I2NPMessage* CreateI2NPMessage(
    I2NPMessageType msgType,
    const uint8_t* buf,
    int len,
    uint32_t replyMsgID) {
  I2NPMessage* msg = NewI2NPMessage(len);
  if (msg->len + len < msg->maxLen) {
    memcpy(msg->GetPayload(), buf, len);
    msg->len += len;
  } else {
    LogPrint(eLogError, "I2NP message length ", len, " exceeds max length");
  }
  msg->FillI2NPMessageHeader(msgType, replyMsgID);
  return msg;
}

std::shared_ptr<I2NPMessage> CreateI2NPMessage(
    const uint8_t* buf,
    int len,
    std::shared_ptr<i2p::tunnel::InboundTunnel> from) {
  I2NPMessage* msg = NewI2NPMessage();
  if (msg->offset + len < msg->maxLen) {
    memcpy(msg->GetBuffer(), buf, len);
    msg->len = msg->offset + len;
    msg->from = from;
  } else {
    LogPrint(eLogError, "I2NP message length ", len, " exceeds max length");
  }
  return ToSharedI2NPMessage(msg);
}

std::shared_ptr<I2NPMessage> CreateDeliveryStatusMsg(
    uint32_t msgID) {
  I2NPMessage* m = NewI2NPShortMessage();
  uint8_t* buf = m->GetPayload();
  if (msgID) {
    htobe32buf(buf + DELIVERY_STATUS_MSGID_OFFSET, msgID);
    htobe64buf(buf + DELIVERY_STATUS_TIMESTAMP_OFFSET,
        i2p::util::GetMillisecondsSinceEpoch());
  } else {  // for SSU establishment
    htobe32buf(buf + DELIVERY_STATUS_MSGID_OFFSET,
        i2p::context.GetRandomNumberGenerator().GenerateWord32());
    htobe64buf(buf + DELIVERY_STATUS_TIMESTAMP_OFFSET, 2);  // netID = 2
  }
  m->len += DELIVERY_STATUS_SIZE;
  m->FillI2NPMessageHeader(eI2NPDeliveryStatus);
  return ToSharedI2NPMessage(m);
}

std::shared_ptr<I2NPMessage> CreateRouterInfoDatabaseLookupMsg(
    const uint8_t* key,
    const uint8_t* from,
    uint32_t replyTunnelID,
    bool exploratory,
    std::set<i2p::data::IdentHash>* excludedPeers) {
  auto m = ToSharedI2NPMessage(
      excludedPeers ? NewI2NPMessage() : NewI2NPShortMessage());
  uint8_t* buf = m->GetPayload();
  memcpy(buf, key, 32);  // key
  buf += 32;
  memcpy(buf, from, 32);  // from
  buf += 32;
  uint8_t flag = exploratory ? DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP :
    DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP;
  if (replyTunnelID) {
    *buf = flag | DATABASE_LOOKUP_DELIVERY_FLAG;  // set delivery flag
    htobe32buf(buf + 1, replyTunnelID);
    buf += 5;
  } else {
    *buf = flag;  // flag
    buf++;
  }
  if (excludedPeers) {
    int cnt = excludedPeers->size();
    htobe16buf(buf, cnt);
    buf += 2;
    for (auto& it : *excludedPeers) {
      memcpy(buf, it, 32);
      buf += 32;
    }
  } else {
    // nothing to exclude
    htobuf16(buf, 0);
    buf += 2;
  }
  m->len += (buf - m->GetPayload());
  m->FillI2NPMessageHeader(eI2NPDatabaseLookup);
  return m;
}

std::shared_ptr<I2NPMessage> CreateLeaseSetDatabaseLookupMsg(
    const i2p::data::IdentHash& dest,
    const std::set<i2p::data::IdentHash>& excludedFloodfills,
    const i2p::tunnel::InboundTunnel* replyTunnel,
    const uint8_t* replyKey,
    const uint8_t* replyTag) {
  int cnt = excludedFloodfills.size();
  auto m = ToSharedI2NPMessage(
      cnt > 0 ? NewI2NPMessage() : NewI2NPShortMessage());
  uint8_t* buf = m->GetPayload();
  memcpy(buf, dest, 32);  // key
  buf += 32;
  memcpy(buf, replyTunnel->GetNextIdentHash(), 32);  // reply tunnel GW
  buf += 32;
  *buf = DATABASE_LOOKUP_DELIVERY_FLAG |
         DATABASE_LOOKUP_ENCYPTION_FLAG |
         DATABASE_LOOKUP_TYPE_LEASESET_LOOKUP;  // flags
  htobe32buf(buf + 1, replyTunnel->GetNextTunnelID());  // reply tunnel ID
  buf += 5;
  // excluded
  htobe16buf(buf, cnt);
  buf += 2;
  if (cnt > 0) {
    for (auto& it : excludedFloodfills) {
      memcpy(buf, it, 32);
      buf += 32;
    }
  }
  // encryption
  memcpy(buf, replyKey, 32);
  buf[32] = 1;  // 1 tag
  memcpy(buf + 33, replyTag, 32);
  buf += 65;
  m->len += (buf - m->GetPayload());
  m->FillI2NPMessageHeader(eI2NPDatabaseLookup);
  return m;
}

std::shared_ptr<I2NPMessage> CreateDatabaseSearchReply(
    const i2p::data::IdentHash& ident,
    std::vector<i2p::data::IdentHash> routers) {
  auto m =  ToSharedI2NPMessage(NewI2NPShortMessage());
  uint8_t* buf = m->GetPayload();
  size_t len = 0;
  memcpy(buf, ident, 32);
  len += 32;
  buf[len] = routers.size();
  len++;
  for (auto it : routers) {
    memcpy(buf + len, it, 32);
    len += 32;
  }
  memcpy(buf + len, i2p::context.GetRouterInfo().GetIdentHash(), 32);
  len += 32;
  m->len += len;
  m->FillI2NPMessageHeader(eI2NPDatabaseSearchReply);
  return m;
}

std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg(
    std::shared_ptr<const i2p::data::RouterInfo> router,
    uint32_t replyToken) {
  if (!router)  // we send own RouterInfo
    router = context.GetSharedRouterInfo();
  auto m =  ToSharedI2NPMessage(NewI2NPShortMessage());
  uint8_t * payload = m->GetPayload();
  memcpy(payload + DATABASE_STORE_KEY_OFFSET, router->GetIdentHash(), 32);
  payload[DATABASE_STORE_TYPE_OFFSET] = 0;  // RouterInfo
  htobe32buf(payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, replyToken);
  uint8_t* buf = payload + DATABASE_STORE_HEADER_SIZE;
  if (replyToken) {
    memset(buf, 0, 4);  // zero tunnelID means direct reply
    buf += 4;
    memcpy(buf, router->GetIdentHash(), 32);
    buf += 32;
  }
  CryptoPP::Gzip compressor;
  compressor.Put(router->GetBuffer(), router->GetBufferLen());
  compressor.MessageEnd();
  auto size = compressor.MaxRetrievable();
  htobe16buf(buf, size);  // size
  buf += 2;
  m->len += (buf - payload);  // payload size
  if (m->len + size > m->maxLen) {
    LogPrint(eLogInfo,
        "DatabaseStore message size is not enough for ", m->len + size);
    auto newMsg =  ToSharedI2NPMessage(NewI2NPMessage());
    *newMsg = *m;
    m = newMsg;
    buf = m->buf + m->len;
  }
  compressor.Get(buf, size);
  m->len += size;
  m->FillI2NPMessageHeader(eI2NPDatabaseStore);
  return m;
}

std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg(
    std::shared_ptr<const i2p::data::LeaseSet> leaseSet,
    uint32_t replyToken) {
  if (!leaseSet) return nullptr;
  auto m = ToSharedI2NPMessage(NewI2NPShortMessage());
  uint8_t* payload = m->GetPayload();
  memcpy(payload + DATABASE_STORE_KEY_OFFSET, leaseSet->GetIdentHash(), 32);
  payload[DATABASE_STORE_TYPE_OFFSET] = 1;  // LeaseSet
  htobe32buf(payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, replyToken);
  size_t size = DATABASE_STORE_HEADER_SIZE;
  if (replyToken) {
    auto leases = leaseSet->GetNonExpiredLeases();
    if (leases.size() > 0) {
      htobe32buf(payload + size, leases[0].tunnelID);
      size += 4;  // reply tunnelID
      memcpy(payload + size, leases[0].tunnelGateway, 32);
      size += 32;  // reply tunnel gateway
    } else {
      htobe32buf(payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, 0);
    }
  }
  memcpy(payload + size, leaseSet->GetBuffer(), leaseSet->GetBufferLen());
  size += leaseSet->GetBufferLen();
  m->len += size;
  m->FillI2NPMessageHeader(eI2NPDatabaseStore);
  return m;
}

bool HandleBuildRequestRecords(
    int num,
    uint8_t* records,
    uint8_t* clearText) {
  for (int i = 0; i < num; i++) {
    uint8_t * record = records + i*TUNNEL_BUILD_RECORD_SIZE;
    if (!memcmp(
          record + BUILD_REQUEST_RECORD_TO_PEER_OFFSET,
          (const uint8_t *)i2p::context.GetRouterInfo().GetIdentHash(), 16)) {
      LogPrint("Record ", i, " is ours");
      i2p::crypto::ElGamalDecrypt(
          i2p::context.GetEncryptionPrivateKey(),
          record + BUILD_REQUEST_RECORD_ENCRYPTED_OFFSET,
          clearText);
      // replace record to reply
      if (i2p::context.AcceptsTunnels() &&
        i2p::tunnel::tunnels.GetTransitTunnels().size() <=
        MAX_NUM_TRANSIT_TUNNELS &&
        !i2p::transport::transports.IsBandwidthExceeded()) {
        i2p::tunnel::TransitTunnel* transitTunnel =
          i2p::tunnel::CreateTransitTunnel(
              bufbe32toh(clearText + BUILD_REQUEST_RECORD_RECEIVE_TUNNEL_OFFSET),
              clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
              bufbe32toh(clearText + BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET),
              clearText + BUILD_REQUEST_RECORD_LAYER_KEY_OFFSET,
              clearText + BUILD_REQUEST_RECORD_IV_KEY_OFFSET,
              clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET] & 0x80,
              clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET ] & 0x40);
        i2p::tunnel::tunnels.AddTransitTunnel(transitTunnel);
        record[BUILD_RESPONSE_RECORD_RET_OFFSET] = 0;
      } else {  // always reject with bandwidth reason (30)
        record[BUILD_RESPONSE_RECORD_RET_OFFSET] = 30;
      }
      // TODO(unassigned): fill filler
      CryptoPP::SHA256().CalculateDigest(
          record + BUILD_RESPONSE_RECORD_HASH_OFFSET,
          record + BUILD_RESPONSE_RECORD_PADDING_OFFSET,
          BUILD_RESPONSE_RECORD_PADDING_SIZE + 1);  // + 1 byte of ret
      // encrypt reply
      i2p::crypto::CBCEncryption encryption;
      for (int j = 0; j < num; j++) {
        encryption.SetKey(clearText + BUILD_REQUEST_RECORD_REPLY_KEY_OFFSET);
        encryption.SetIV(clearText + BUILD_REQUEST_RECORD_REPLY_IV_OFFSET);
        uint8_t* reply = records + j * TUNNEL_BUILD_RECORD_SIZE;
        encryption.Encrypt(reply, TUNNEL_BUILD_RECORD_SIZE, reply);
      }
      return true;
    }
  }
  return false;
}

void HandleVariableTunnelBuildMsg(
    uint32_t replyMsgID,
    uint8_t* buf,
    size_t len) {
  int num = buf[0];
  LogPrint("VariableTunnelBuild ", num, " records");
  auto tunnel =  i2p::tunnel::tunnels.GetPendingInboundTunnel(replyMsgID);
  if (tunnel) {
    // endpoint of inbound tunnel
    LogPrint("VariableTunnelBuild reply for tunnel ", tunnel->GetTunnelID());
    if (tunnel->HandleTunnelBuildResponse(buf, len)) {
      LogPrint("Inbound tunnel ", tunnel->GetTunnelID(), " has been created");
      tunnel->SetState(i2p::tunnel::eTunnelStateEstablished);
      i2p::tunnel::tunnels.AddInboundTunnel(tunnel);
    } else {
      LogPrint("Inbound tunnel ", tunnel->GetTunnelID(), " has been declined");
      tunnel->SetState(i2p::tunnel::eTunnelStateBuildFailed);
    }
  } else {
    uint8_t clearText[BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE] = {};
    if (HandleBuildRequestRecords(num, buf + 1, clearText)) {
      // we are endpoint of outboud tunnel
      if (clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET] & 0x40) {
        // so we send it to reply tunnel
        transports.SendMessage(
            clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
            ToSharedI2NPMessage(
              CreateTunnelGatewayMsg(
                bufbe32toh(
                  clearText + BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET),
                eI2NPVariableTunnelBuildReply,
                buf,
                len,
                bufbe32toh(
                  clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET))));
      } else {
        transports.SendMessage(
            clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
            ToSharedI2NPMessage(
              CreateI2NPMessage(
                eI2NPVariableTunnelBuild,
                buf,
                len,
                bufbe32toh(
                  clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET))));
      }
    }
  }
}

void HandleTunnelBuildMsg(
    uint8_t* buf,
    size_t len) {
  uint8_t clearText[BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE];
  if (HandleBuildRequestRecords(NUM_TUNNEL_BUILD_RECORDS, buf, clearText)) {
    // we are endpoint of outbound tunnel
    if (clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET] & 0x40) {
      // so we send it to reply tunnel
      transports.SendMessage(
          clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
          ToSharedI2NPMessage(
            CreateTunnelGatewayMsg(
              bufbe32toh(
                clearText + BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET),
              eI2NPTunnelBuildReply,
              buf,
              len,
              bufbe32toh(
                clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET))));
    } else {
      transports.SendMessage(
          clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
          ToSharedI2NPMessage(
            CreateI2NPMessage(
              eI2NPTunnelBuild,
              buf,
              len,
              bufbe32toh(
                clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET))));
    }
  }
}

void HandleVariableTunnelBuildReplyMsg(
    uint32_t replyMsgID,
    uint8_t* buf,
    size_t len) {
  LogPrint("VariableTunnelBuildReplyMsg replyMsgID=", replyMsgID);
  auto tunnel = i2p::tunnel::tunnels.GetPendingOutboundTunnel(replyMsgID);
  if (tunnel) {
    // reply for outbound tunnel
    if (tunnel->HandleTunnelBuildResponse(buf, len)) {
      LogPrint("Outbound tunnel ", tunnel->GetTunnelID(), " has been created");
      tunnel->SetState(i2p::tunnel::eTunnelStateEstablished);
      i2p::tunnel::tunnels.AddOutboundTunnel(tunnel);
    } else {
      LogPrint("Outbound tunnel ", tunnel->GetTunnelID(), " has been declined");
      tunnel->SetState(i2p::tunnel::eTunnelStateBuildFailed);
    }
  } else {
    LogPrint("Pending tunnel for message ", replyMsgID, " not found");
  }
}


I2NPMessage* CreateTunnelDataMsg(
    const uint8_t * buf) {
  I2NPMessage* msg = NewI2NPShortMessage();
  memcpy(msg->GetPayload(), buf, i2p::tunnel::TUNNEL_DATA_MSG_SIZE);
  msg->len += i2p::tunnel::TUNNEL_DATA_MSG_SIZE;
  msg->FillI2NPMessageHeader(eI2NPTunnelData);
  return msg;
}

I2NPMessage* CreateTunnelDataMsg(
    uint32_t tunnelID,
    const uint8_t* payload) {
  I2NPMessage* msg = NewI2NPShortMessage();
  memcpy(msg->GetPayload() + 4, payload, i2p::tunnel::TUNNEL_DATA_MSG_SIZE - 4);
  htobe32buf(msg->GetPayload(), tunnelID);
  msg->len += i2p::tunnel::TUNNEL_DATA_MSG_SIZE;
  msg->FillI2NPMessageHeader(eI2NPTunnelData);
  return msg;
}

std::shared_ptr<I2NPMessage> CreateEmptyTunnelDataMsg() {
  I2NPMessage* msg = NewI2NPShortMessage();
  msg->len += i2p::tunnel::TUNNEL_DATA_MSG_SIZE;
  return ToSharedI2NPMessage(msg);
}

I2NPMessage* CreateTunnelGatewayMsg(
    uint32_t tunnelID,
    const uint8_t* buf,
    size_t len) {
  I2NPMessage* msg = NewI2NPMessage(len);
  uint8_t* payload = msg->GetPayload();
  htobe32buf(payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
  htobe16buf(payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, len);
  memcpy(payload + TUNNEL_GATEWAY_HEADER_SIZE, buf, len);
  msg->len += TUNNEL_GATEWAY_HEADER_SIZE + len;
  msg->FillI2NPMessageHeader(eI2NPTunnelGateway);
  return msg;
}

std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg(
    uint32_t tunnelID,
    std::shared_ptr<I2NPMessage> msg) {
  if (msg->offset >= I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE) {
    // message is capable to be used without copying
    uint8_t* payload = msg->GetBuffer() - TUNNEL_GATEWAY_HEADER_SIZE;
    htobe32buf(payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
    int len = msg->GetLength();
    htobe16buf(payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, len);
    msg->offset -= (I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE);
    msg->len = msg->offset + I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE +len;
    msg->FillI2NPMessageHeader(eI2NPTunnelGateway);
    return msg;
  } else {
    I2NPMessage* msg1 = CreateTunnelGatewayMsg(
        tunnelID,
        msg->GetBuffer(),
        msg->GetLength());
    return ToSharedI2NPMessage(msg1);
  }
}

I2NPMessage* CreateTunnelGatewayMsg(
    uint32_t tunnelID,
    I2NPMessageType msgType,
    const uint8_t* buf,
    size_t len,
    uint32_t replyMsgID) {
  I2NPMessage* msg = NewI2NPMessage(len);
  size_t gatewayMsgOffset = I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE;
  msg->offset += gatewayMsgOffset;
  msg->len += gatewayMsgOffset;
  memcpy(msg->GetPayload(), buf, len);
  msg->len += len;
  msg->FillI2NPMessageHeader(msgType, replyMsgID);  // create content message
  len = msg->GetLength();
  msg->offset -= gatewayMsgOffset;
  uint8_t* payload = msg->GetPayload();
  htobe32buf(payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
  htobe16buf(payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, len);
  msg->FillI2NPMessageHeader(eI2NPTunnelGateway);  // gateway message
  return msg;
}

size_t GetI2NPMessageLength(
    const uint8_t* msg) {
  return bufbe16toh(msg + I2NP_HEADER_SIZE_OFFSET) + I2NP_HEADER_SIZE;
}

void HandleI2NPMessage(
    uint8_t* msg,
    size_t len) {
  uint8_t typeID = msg[I2NP_HEADER_TYPEID_OFFSET];
  uint32_t msgID = bufbe32toh(msg + I2NP_HEADER_MSGID_OFFSET);
  LogPrint("I2NP msg received len=", len,
      ", type=", static_cast<int>(typeID),
      ", msgID=", (unsigned int)msgID);
  uint8_t* buf = msg + I2NP_HEADER_SIZE;
  int size = bufbe16toh(msg + I2NP_HEADER_SIZE_OFFSET);
  switch (typeID) {
    case eI2NPVariableTunnelBuild:
      LogPrint("VariableTunnelBuild");
      HandleVariableTunnelBuildMsg(msgID, buf, size);
    break;
    case eI2NPVariableTunnelBuildReply:
      LogPrint("VariableTunnelBuildReply");
      HandleVariableTunnelBuildReplyMsg(msgID, buf, size);
    break;
    case eI2NPTunnelBuild:
      LogPrint("TunnelBuild");
      HandleTunnelBuildMsg(buf, size);
    break;
    case eI2NPTunnelBuildReply:
      LogPrint("TunnelBuildReply");
      // TODO(unassigned): ???
    break;
    default:
      LogPrint("Unexpected message ", static_cast<int>(typeID));
  }
}

void HandleI2NPMessage(
    std::shared_ptr<I2NPMessage> msg) {
  if (msg) {
    switch (msg->GetTypeID()) {
      case eI2NPTunnelData:
        LogPrint("TunnelData");
        i2p::tunnel::tunnels.PostTunnelData(msg);
      break;
      case eI2NPTunnelGateway:
        LogPrint("TunnelGateway");
        i2p::tunnel::tunnels.PostTunnelData(msg);
      break;
      case eI2NPGarlic: {
        LogPrint("Garlic");
        if (msg->from) {
          if (msg->from->GetTunnelPool())
            msg->from->GetTunnelPool()->ProcessGarlicMessage(msg);
          else
            LogPrint(eLogInfo,
                "Local destination for garlic doesn't exist anymore");
        } else {
          i2p::context.ProcessGarlicMessage(msg);
        }
        break;
      }
      case eI2NPDatabaseStore:
      case eI2NPDatabaseSearchReply:
      case eI2NPDatabaseLookup:
        // forward to netDb
        i2p::data::netdb.PostI2NPMsg(msg);
      break;
      case eI2NPDeliveryStatus: {
        LogPrint("DeliveryStatus");
        if (msg->from && msg->from->GetTunnelPool())
          msg->from->GetTunnelPool()->ProcessDeliveryStatus(msg);
        else
          i2p::context.ProcessDeliveryStatusMessage(msg);
        break;
      }
      case eI2NPVariableTunnelBuild:
      case eI2NPVariableTunnelBuildReply:
      case eI2NPTunnelBuild:
      case eI2NPTunnelBuildReply:
        // forward to tunnel thread
        i2p::tunnel::tunnels.PostTunnelData(msg);
      break;
      default:
        HandleI2NPMessage(msg->GetBuffer(), msg->GetLength());
    }
  }
}

I2NPMessagesHandler::~I2NPMessagesHandler() {
  Flush();
}

void I2NPMessagesHandler::PutNextMessage(
    std::shared_ptr<I2NPMessage> msg) {
  if (msg) {
    switch (msg->GetTypeID()) {
      case eI2NPTunnelData:
        m_TunnelMsgs.push_back(msg);
      break;
      case eI2NPTunnelGateway:
        m_TunnelGatewayMsgs.push_back(msg);
      break;
      default:
        HandleI2NPMessage(msg);
    }
  }
}

void I2NPMessagesHandler::Flush() {
  if (!m_TunnelMsgs.empty()) {
    i2p::tunnel::tunnels.PostTunnelData(m_TunnelMsgs);
    m_TunnelMsgs.clear();
  }
  if (!m_TunnelGatewayMsgs.empty()) {
    i2p::tunnel::tunnels.PostTunnelData(m_TunnelGatewayMsgs);
    m_TunnelGatewayMsgs.clear();
  }
}
}  // namespace i2p
