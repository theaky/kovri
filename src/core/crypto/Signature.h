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

#ifndef SRC_CORE_CRYPTO_SIGNATURE_H_
#define SRC_CORE_CRYPTO_SIGNATURE_H_

#include <cryptopp/asn.h>
#include <cryptopp/dsa.h>
#include <cryptopp/eccrypto.h>
#include <cryptopp/oids.h>
#include <cryptopp/osrng.h>
#include <cryptopp/rsa.h>

#include <inttypes.h>

#include "CryptoConst.h"
#include "EdDSA25519.h"
#include "SignatureBase.h"

namespace i2p {
namespace crypto {

const size_t DSA_PUBLIC_KEY_LENGTH = 128;
const size_t DSA_SIGNATURE_LENGTH = 40;
const size_t DSA_PRIVATE_KEY_LENGTH = DSA_SIGNATURE_LENGTH/2;

class DSAVerifier
    : public Verifier {
 public:
  DSAVerifier(
      const uint8_t* signingKey) {
    m_PublicKey.Initialize(
        dsap,
        dsaq,
        dsag,
        CryptoPP::Integer(
          signingKey,
          DSA_PUBLIC_KEY_LENGTH));
  }

  bool Verify(
      const uint8_t* buf,
      size_t len,
      const uint8_t* signature) const {
    CryptoPP::DSA::Verifier verifier(m_PublicKey);
    return verifier.VerifyMessage(buf, len, signature, DSA_SIGNATURE_LENGTH);
  }

  size_t GetPublicKeyLen() const {
    return DSA_PUBLIC_KEY_LENGTH;
  }

  size_t GetSignatureLen() const {
    return DSA_SIGNATURE_LENGTH;
  }

 private:
  CryptoPP::DSA::PublicKey m_PublicKey;
};

class DSASigner
    : public Signer {
 public:
  DSASigner(
      const uint8_t* signingPrivateKey);

  void Sign(
      CryptoPP::RandomNumberGenerator& rnd,
      const uint8_t* buf,
      int len,
      uint8_t* signature) const;

 private:
  CryptoPP::DSA::PrivateKey m_PrivateKey;
};

void CreateDSARandomKeys(
    CryptoPP::RandomNumberGenerator& rnd,
    uint8_t* signingPrivateKey,
    uint8_t* signingPublicKey);

template<typename Hash, size_t keyLen>
class ECDSAVerifier
    : public Verifier {
 public:
  template<typename Curve>
  ECDSAVerifier(
      Curve curve,
      const uint8_t* signingKey) {
    m_PublicKey.Initialize(
        curve,
        CryptoPP::ECP::Point(
          CryptoPP::Integer(
            signingKey,
            keyLen / 2),
          CryptoPP::Integer(
            signingKey + keyLen / 2, keyLen / 2)));
  }

  bool Verify(
      const uint8_t* buf,
      size_t len,
      const uint8_t * signature) const {
    typename CryptoPP::ECDSA<CryptoPP::ECP, Hash>::Verifier verifier(m_PublicKey);
    return verifier.VerifyMessage(
        buf, len, signature, keyLen);  // signature length
  }

  size_t GetPublicKeyLen() const {
    return keyLen;
  }

  size_t GetSignatureLen() const {
    return keyLen;  // signature length = key length
  }

 private:
  typename CryptoPP::ECDSA<CryptoPP::ECP, Hash>::PublicKey m_PublicKey;
};

template<typename Hash>
class ECDSASigner
    : public Signer {
 public:
  template<typename Curve>
  ECDSASigner(
      Curve curve,
      const uint8_t* signingPrivateKey,
      size_t keyLen) {
    m_PrivateKey.Initialize(
        curve,
        CryptoPP::Integer(
          signingPrivateKey,
          keyLen/2));  // private key length
  }

  void Sign(
      CryptoPP::RandomNumberGenerator& rnd,
      const uint8_t* buf,
      int len,
      uint8_t* signature) const {
    typename CryptoPP::ECDSA<CryptoPP::ECP, Hash>::Signer signer(m_PrivateKey);
    signer.SignMessage(rnd, buf, len, signature);
  }

 private:
  typename CryptoPP::ECDSA<CryptoPP::ECP, Hash>::PrivateKey m_PrivateKey;
};

template<typename Hash, typename Curve>
inline void CreateECDSARandomKeys(
    CryptoPP::RandomNumberGenerator& rnd,
    Curve curve,
    size_t keyLen,
    uint8_t* signingPrivateKey,
    uint8_t* signingPublicKey) {
  typename CryptoPP::ECDSA<CryptoPP::ECP, Hash>::PrivateKey privateKey;
  typename CryptoPP::ECDSA<CryptoPP::ECP, Hash>::PublicKey publicKey;
  privateKey.Initialize(rnd, curve);
  privateKey.MakePublicKey(publicKey);
  privateKey.GetPrivateExponent().Encode(signingPrivateKey, keyLen / 2);
  auto q = publicKey.GetPublicElement();
  q.x.Encode(signingPublicKey, keyLen / 2);
  q.y.Encode(signingPublicKey + keyLen / 2, keyLen / 2);
}

// ECDSA_SHA256_P256
const size_t ECDSAP256_KEY_LENGTH = 64;

struct ECDSAP256Verifier
    : public ECDSAVerifier<CryptoPP::SHA256, ECDSAP256_KEY_LENGTH> {
  ECDSAP256Verifier(
      const uint8_t * signingKey)
      : ECDSAVerifier(
          CryptoPP::ASN1::secp256r1(),
          signingKey) {}
};

struct ECDSAP256Signer
    : public ECDSASigner<CryptoPP::SHA256> {
  explicit ECDSAP256Signer(
      const uint8_t* signingPrivateKey)
      : ECDSASigner(
          CryptoPP::ASN1::secp256r1(),
          signingPrivateKey,
          ECDSAP256_KEY_LENGTH) {}
};

inline void CreateECDSAP256RandomKeys(
    CryptoPP::RandomNumberGenerator& rnd,
    uint8_t* signingPrivateKey,
    uint8_t* signingPublicKey) {
  CreateECDSARandomKeys<CryptoPP::SHA256>(
      rnd,
      CryptoPP::ASN1::secp256r1(),
      ECDSAP256_KEY_LENGTH,
      signingPrivateKey,
      signingPublicKey);
}

// ECDSA_SHA384_P384
const size_t ECDSAP384_KEY_LENGTH = 96;
class ECDSAP384Verifier
    : public ECDSAVerifier<CryptoPP::SHA384, ECDSAP384_KEY_LENGTH> {
 public:
  ECDSAP384Verifier(
      const uint8_t * signingKey):

  ECDSAVerifier(
      CryptoPP::ASN1::secp384r1(),
      signingKey) {}
};

class ECDSAP384Signer
    : public ECDSASigner<CryptoPP::SHA384> {
 public:
  ECDSAP384Signer(
      const uint8_t * signingPrivateKey):

  ECDSASigner(
      CryptoPP::ASN1::secp384r1(),
      signingPrivateKey,
      ECDSAP384_KEY_LENGTH) {}
};

inline void CreateECDSAP384RandomKeys(
    CryptoPP::RandomNumberGenerator& rnd,
    uint8_t* signingPrivateKey,
    uint8_t* signingPublicKey) {
  CreateECDSARandomKeys<CryptoPP::SHA384>(
      rnd,
      CryptoPP::ASN1::secp384r1(),
      ECDSAP384_KEY_LENGTH,
      signingPrivateKey,
      signingPublicKey);
}

// ECDSA_SHA512_P521
const size_t ECDSAP521_KEY_LENGTH = 132;
class ECDSAP521Verifier
    : public ECDSAVerifier<CryptoPP::SHA512, ECDSAP521_KEY_LENGTH> {
 public:
  ECDSAP521Verifier(
    const uint8_t * signingKey):

  ECDSAVerifier(
      CryptoPP::ASN1::secp521r1(),
      signingKey) {}
};

class ECDSAP521Signer
    : public ECDSASigner<CryptoPP::SHA512> {
 public:
  ECDSAP521Signer(
      const uint8_t * signingPrivateKey):

  ECDSASigner(
      CryptoPP::ASN1::secp521r1(),
      signingPrivateKey,
      ECDSAP521_KEY_LENGTH) {}
};

inline void CreateECDSAP521RandomKeys(
    CryptoPP::RandomNumberGenerator& rnd,
    uint8_t* signingPrivateKey,
    uint8_t* signingPublicKey) {
  CreateECDSARandomKeys<CryptoPP::SHA512>(
      rnd,
      CryptoPP::ASN1::secp521r1(),
      ECDSAP521_KEY_LENGTH,
      signingPrivateKey,
      signingPublicKey);
}

// RSA
template<typename Hash, size_t keyLen>
class RSAVerifier
    : public Verifier {
 public:
  explicit RSAVerifier(
      const uint8_t* signingKey) {
    m_PublicKey.Initialize(
        CryptoPP::Integer(
          signingKey,
          keyLen),
        CryptoPP::Integer(
          rsae));
  }

  bool Verify(
      const uint8_t* buf,
      size_t len,
      const uint8_t* signature) const {
    typename CryptoPP::RSASS<CryptoPP::PKCS1v15, Hash>::Verifier verifier(m_PublicKey);
    return verifier.VerifyMessage(buf, len, signature, keyLen);  // signature length
  }

  size_t GetPublicKeyLen() const {
    return keyLen;
  }

  size_t GetSignatureLen() const {
    return keyLen;
  }

  size_t GetPrivateKeyLen() const {
    return GetSignatureLen() * 2;
  }

 private:
  CryptoPP::RSA::PublicKey m_PublicKey;
};


template<typename Hash>
class RSASigner
    : public Signer {
 public:
  RSASigner(
       const uint8_t* signingPrivateKey,
       size_t keyLen) {
     m_PrivateKey.Initialize(
         CryptoPP::Integer(
           signingPrivateKey,
           keyLen / 2),
         rsae,
         CryptoPP::Integer(
           signingPrivateKey + keyLen/2,
           keyLen/2));
  }

  void Sign(
      CryptoPP::RandomNumberGenerator& rnd,
      const uint8_t* buf,
      int len,
      uint8_t* signature) const {
    typename CryptoPP::RSASS<CryptoPP::PKCS1v15, Hash>::Signer signer(m_PrivateKey);
    signer.SignMessage(rnd, buf, len, signature);
  }

 private:
  CryptoPP::RSA::PrivateKey m_PrivateKey;
};

inline void CreateRSARandomKeys(
    CryptoPP::RandomNumberGenerator& rnd,
    size_t publicKeyLen,
    uint8_t* signingPrivateKey,
    uint8_t* signingPublicKey) {
  CryptoPP::RSA::PrivateKey privateKey;
  privateKey.Initialize(
      rnd,
      publicKeyLen * 8,
      rsae);
  privateKey.GetModulus().Encode(
      signingPrivateKey,
      publicKeyLen);
  privateKey.GetPrivateExponent().Encode(
      signingPrivateKey + publicKeyLen,
      publicKeyLen);
  privateKey.GetModulus().Encode(
      signingPublicKey,
      publicKeyLen);
}


//  RSA_SHA256_2048
const size_t RSASHA2562048_KEY_LENGTH = 256;
class RSASHA2562048Verifier
    : public RSAVerifier<CryptoPP::SHA256, RSASHA2562048_KEY_LENGTH> {
 public:
  RSASHA2562048Verifier(
      const uint8_t* signingKey)
      : RSAVerifier(signingKey) {}
};

class RSASHA2562048Signer
    : public RSASigner<CryptoPP::SHA256> {
 public:
  RSASHA2562048Signer(
      const uint8_t* signingPrivateKey)
      : RSASigner(
          signingPrivateKey,
          RSASHA2562048_KEY_LENGTH * 2) {}
};

// RSA_SHA384_3072
const size_t RSASHA3843072_KEY_LENGTH = 384;
class RSASHA3843072Verifier
    : public RSAVerifier<CryptoPP::SHA384, RSASHA3843072_KEY_LENGTH> {
 public:
  RSASHA3843072Verifier(
      const uint8_t* signingKey)
      : RSAVerifier(signingKey) {}
};

class RSASHA3843072Signer
    : public RSASigner<CryptoPP::SHA384> {
 public:
  RSASHA3843072Signer(
      const uint8_t * signingPrivateKey)
      : RSASigner(
          signingPrivateKey,
          RSASHA3843072_KEY_LENGTH * 2) {}
};

// RSA_SHA512_4096
const size_t RSASHA5124096_KEY_LENGTH = 512;
class RSASHA5124096Verifier
    : public RSAVerifier<CryptoPP::SHA512, RSASHA5124096_KEY_LENGTH> {
 public:
  RSASHA5124096Verifier(
      const uint8_t* signingKey)
      : RSAVerifier(signingKey) {}
};

class RSASHA5124096Signer
    : public RSASigner<CryptoPP::SHA512> {
 public:
  RSASHA5124096Signer(
      const uint8_t* signingPrivateKey)
      : RSASigner(
          signingPrivateKey,
          RSASHA5124096_KEY_LENGTH * 2) {}
};

// Raw verifiers
class RawVerifier {
 public:
  virtual ~RawVerifier() {}

  virtual void Update(
      const uint8_t* buf,
      size_t len) = 0;

  virtual bool Verify(
      const uint8_t* signature) = 0;
};

template<typename Hash, size_t keyLen>
class RSARawVerifier
    : public RawVerifier {
 public:
  RSARawVerifier(
      const uint8_t* signingKey)
      : n(signingKey, keyLen) {}

  void Update(
      const uint8_t* buf,
      size_t len) {
    m_Hash.Update(buf, len);
  }

  bool Verify(
      const uint8_t* signature) {
    // RSA encryption first
    CryptoPP::Integer enSig(
        a_exp_b_mod_c(
          CryptoPP::Integer(
            signature,
            keyLen),
          CryptoPP::Integer(
            i2p::crypto::rsae),
          n));  // s^e mod n
    uint8_t EnSigBuf[keyLen];
    enSig.Encode(EnSigBuf, keyLen);
    uint8_t digest[Hash::DIGESTSIZE];
    m_Hash.Final(digest);
    if (static_cast<int>(keyLen) < Hash::DIGESTSIZE)
      return false;  // can't verify digest longer than key
    // we assume digest is right aligned, at least for PKCS#1 v1.5 padding
    return !memcmp(
        EnSigBuf + (keyLen - Hash::DIGESTSIZE),
        digest,
        Hash::DIGESTSIZE);
  }

 private:
  CryptoPP::Integer n;  // RSA modulus
  Hash m_Hash;
};

class RSASHA5124096RawVerifier
    : public RSARawVerifier<CryptoPP::SHA512, RSASHA5124096_KEY_LENGTH> {
 public:
  RSASHA5124096RawVerifier(
      const uint8_t* signingKey)
      : RSARawVerifier(signingKey) {}
};

}  // namespace crypto
}  // namespace i2p

#endif  // SRC_CORE_CRYPTO_SIGNATURE_H_
