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

#ifndef SRC_CORE_TUNNEL_TUNNELCONFIG_H_
#define SRC_CORE_TUNNEL_TUNNELCONFIG_H_

#include <inttypes.h>

#include <memory>
#include <sstream>
#include <vector>

#include "RouterContext.h"
#include "RouterInfo.h"
#include "crypto/Tunnel.h"
#include "util/Timestamp.h"

namespace i2p {
namespace tunnel {

struct TunnelHopConfig {
  std::shared_ptr<const i2p::data::RouterInfo> router, nextRouter;
  uint32_t tunnelID,
           nextTunnelID;
  uint8_t layerKey[32];
  uint8_t ivKey[32];
  uint8_t replyKey[32];
  uint8_t replyIV[16];
  bool isGateway,
       isEndpoint;

  TunnelHopConfig* next, *prev;
  i2p::crypto::TunnelDecryption decryption;
  int recordIndex;  // record # in tunnel build message

  explicit TunnelHopConfig(std::shared_ptr<const i2p::data::RouterInfo> r) {
    CryptoPP::RandomNumberGenerator& rnd =
      i2p::context.GetRandomNumberGenerator();
    rnd.GenerateBlock(layerKey, 32);
    rnd.GenerateBlock(ivKey, 32);
    rnd.GenerateBlock(replyIV, 16);
    tunnelID = rnd.GenerateWord32();
    isGateway = true;
    isEndpoint = true;
    router = r;
    // nextRouter = nullptr;
    nextTunnelID = 0;
    next = nullptr;
    prev = nullptr;
  }

  void SetNextRouter(
      std::shared_ptr<const i2p::data::RouterInfo> r) {
    nextRouter = r;
    isEndpoint = false;
    CryptoPP::RandomNumberGenerator& rnd =
      i2p::context.GetRandomNumberGenerator();
    nextTunnelID = rnd.GenerateWord32();
  }

  void SetReplyHop(
      const TunnelHopConfig* replyFirstHop) {
    nextRouter = replyFirstHop->router;
    nextTunnelID = replyFirstHop->tunnelID;
    isEndpoint = true;
  }

  void SetNext(
      TunnelHopConfig* n) {
    next = n;
    if (next) {
      next->prev = this;
      next->isGateway = false;
      isEndpoint = false;
      nextRouter = next->router;
      nextTunnelID = next->tunnelID;
    }
  }

  void SetPrev(
      TunnelHopConfig* p) {
    prev = p;
    if (prev) {
      prev->next = this;
      prev->isEndpoint = false;
      isGateway = false;
    }
  }

  void CreateBuildRequestRecord(
      uint8_t* record,
      uint32_t replyMsgID) const {
    uint8_t clearText[BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE] = {};
    htobe32buf(
        clearText + BUILD_REQUEST_RECORD_RECEIVE_TUNNEL_OFFSET,
        tunnelID);
    memcpy(
        clearText + BUILD_REQUEST_RECORD_OUR_IDENT_OFFSET,
        router->GetIdentHash(),
        32);
    htobe32buf(
        clearText + BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET,
        nextTunnelID);
    memcpy(
        clearText + BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET,
        nextRouter->GetIdentHash(),
        32);
    memcpy(
        clearText + BUILD_REQUEST_RECORD_LAYER_KEY_OFFSET,
        layerKey,
        32);
    memcpy(
        clearText + BUILD_REQUEST_RECORD_IV_KEY_OFFSET,
        ivKey,
        32);
    memcpy(
        clearText + BUILD_REQUEST_RECORD_REPLY_KEY_OFFSET,
        replyKey,
        32);
    memcpy(
        clearText + BUILD_REQUEST_RECORD_REPLY_IV_OFFSET,
        replyIV,
        16);
    uint8_t flag = 0;
    if (isGateway)
      flag |= 0x80;
    if (isEndpoint)
      flag |= 0x40;
    clearText[BUILD_REQUEST_RECORD_FLAG_OFFSET] = flag;
    htobe32buf(
        clearText + BUILD_REQUEST_RECORD_REQUEST_TIME_OFFSET,
        i2p::util::GetHoursSinceEpoch());
    htobe32buf(
        clearText + BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET,
        replyMsgID);
    // TODO(unassigned): fill padding
    router->GetElGamalEncryption()->Encrypt(
        clearText,
        BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE,
        record + BUILD_REQUEST_RECORD_ENCRYPTED_OFFSET);
    memcpy(
        record + BUILD_REQUEST_RECORD_TO_PEER_OFFSET,
        (const uint8_t *)router->GetIdentHash(),
        16);
  }
};

class TunnelConfig : public std::enable_shared_from_this<TunnelConfig> {
 public:
  TunnelConfig(
      std::vector<std::shared_ptr<const i2p::data::RouterInfo> > peers,
      std::shared_ptr<const TunnelConfig> replyTunnelConfig = nullptr) {
      // replyTunnelConfig=nullptr means inbound
    TunnelHopConfig* prev = nullptr;
    for (auto it : peers) {
      auto hop = new TunnelHopConfig(it);
      if (prev)
        prev->SetNext(hop);
      else
        m_FirstHop = hop;
      prev = hop;
    }
    m_LastHop = prev;
    if (replyTunnelConfig) {  // outbound
      m_FirstHop->isGateway = false;
      m_LastHop->SetReplyHop(replyTunnelConfig->GetFirstHop());
    } else {  // inbound
      m_LastHop->SetNextRouter(i2p::context.GetSharedRouterInfo());
    }
  }
  ~TunnelConfig() {
    TunnelHopConfig* hop = m_FirstHop;
    while (hop) {
      auto tmp = hop;
      hop = hop->next;
      delete tmp;
    }
  }

  TunnelHopConfig* GetFirstHop() const {
    return m_FirstHop;
  }

  TunnelHopConfig* GetLastHop() const {
    return m_LastHop;
  }

  int GetNumHops() const {
    int num = 0;
    TunnelHopConfig* hop = m_FirstHop;
    while (hop) {
      num++;
      hop = hop->next;
    }
    return num;
  }

  bool IsInbound() const {
    return m_FirstHop->isGateway;
  }

  std::vector<std::shared_ptr<const i2p::data::RouterInfo> > GetPeers() const {
    std::vector<std::shared_ptr<const i2p::data::RouterInfo> > peers;
    TunnelHopConfig* hop = m_FirstHop;
    while (hop) {
      peers.push_back(hop->router);
      hop = hop->next;
    }
    return peers;
  }

  void Print(
      std::stringstream& s) const {
    TunnelHopConfig* hop = m_FirstHop;
    if (!IsInbound())  // outbound
      s << "me";
    s << "-->" << m_FirstHop->tunnelID;
    while (hop) {
      s << ":" << hop->router->GetIdentHashAbbreviation() << "-->";
      if (!hop->isEndpoint)
        s << hop->nextTunnelID;
      else
        return;
      hop = hop->next;
    }
    // we didn't reach endpoint that mean we are last hop
    s << ":me";
  }

  std::shared_ptr<TunnelConfig> Invert() const {
    auto peers = GetPeers();
    std::reverse(peers.begin(), peers.end());
    // we use ourself as reply tunnel for outbound tunnel
    return IsInbound() ?
      std::make_shared<TunnelConfig>(
          peers,
          shared_from_this()) :
      std::make_shared<TunnelConfig>(peers);
  }

  std::shared_ptr<TunnelConfig> Clone(
      std::shared_ptr<const TunnelConfig> replyTunnelConfig = nullptr) const {
    return std::make_shared<TunnelConfig> (GetPeers(), replyTunnelConfig);
  }

 private:
  // this constructor can't be called from outside
  TunnelConfig()
      : m_FirstHop(nullptr),
        m_LastHop(nullptr) {}

 private:
  TunnelHopConfig* m_FirstHop, *m_LastHop;
};

}  // namespace tunnel
}  // namespace i2p

#endif  // SRC_CORE_TUNNEL_TUNNELCONFIG_H_
