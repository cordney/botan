/*
* PKCS#11 ECDSA
* (C) 2016 Daniel Neus, Sirrix AG
* (C) 2016 Philipp Weber, Sirrix AG
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#include <botan/p11_ecdsa.h>

#if defined(BOTAN_HAS_ECDSA)

#include <botan/internal/p11_mechanism.h>
#include <botan/internal/algo_registry.h>
#include <botan/internal/pk_utils.h>
#include <botan/keypair.h>
#include <botan/rng.h>

namespace Botan {
namespace PKCS11 {

ECDSA_PublicKey PKCS11_ECDSA_PublicKey::export_key() const
   {
   return ECDSA_PublicKey(domain(), public_point());
   }

bool PKCS11_ECDSA_PrivateKey::check_key(RandomNumberGenerator& rng, bool strong) const
   {
   if(!public_point().on_the_curve())
      {
      return false;
      }


   if(!strong)
      {
      return true;
      }

   return KeyPair::signature_consistency_check(rng, *this, "EMSA1(SHA-1)");
   }

ECDSA_PrivateKey PKCS11_ECDSA_PrivateKey::export_key() const
   {
   auto priv_key = get_attribute_value(AttributeType::Value);

   Null_RNG rng;
   return ECDSA_PrivateKey(rng, domain(), BigInt::decode(priv_key));
   }

secure_vector<byte> PKCS11_ECDSA_PrivateKey::pkcs8_private_key() const
   {
   return export_key().pkcs8_private_key();
   }

namespace {

class PKCS11_ECDSA_Signature_Operation : public PK_Ops::Signature
   {
   public:
      typedef PKCS11_EC_PrivateKey Key_Type;

      PKCS11_ECDSA_Signature_Operation(const PKCS11_EC_PrivateKey& key, const std::string& emsa)
         : PK_Ops::Signature(), m_key(key), m_order(key.domain().get_order()), m_mechanism(MechanismWrapper::create_ecdsa_mechanism(emsa))
         {}

      size_t message_parts() const override
         {
         return 2;
         }

      size_t message_part_size() const override
         {
         return m_order.bytes();
         }

      void update(const byte msg[], size_t msg_len) override
         {
         if(!m_initialized)
            {
            // first call to update: initialize and cache message because we can not determine yet whether a single- or multiple-part operation will be performed
            m_key.module()->C_SignInit(m_key.session().handle(), m_mechanism.data(), m_key.handle());
            m_initialized = true;
            m_first_message = secure_vector<byte>(msg, msg + msg_len);
            return;
            }

         if(!m_first_message.empty())
            {
            // second call to update: start multiple-part operation
            m_key.module()->C_SignUpdate(m_key.session().handle(), m_first_message);
            m_first_message.clear();
            }

         m_key.module()->C_SignUpdate(m_key.session().handle(), const_cast<Byte*>(msg), msg_len);
         }

      secure_vector<byte> sign(RandomNumberGenerator&) override
         {
         secure_vector<byte> signature;
         if(!m_first_message.empty())
            {
            // single call to update: perform single-part operation
            m_key.module()->C_Sign(m_key.session().handle(), m_first_message, signature);
            m_first_message.clear();
            }
         else
            {
            // multiple calls to update (or none): finish multiple-part operation
            m_key.module()->C_SignFinal(m_key.session().handle(), signature);
            }
         m_initialized = false;
         return signature;
         }

   private:
      const PKCS11_EC_PrivateKey& m_key;
      const BigInt& m_order;
      MechanismWrapper m_mechanism;
      secure_vector<byte> m_first_message;
      bool m_initialized = false;
   };


class PKCS11_ECDSA_Verification_Operation : public PK_Ops::Verification
   {
   public:
      typedef PKCS11_EC_PublicKey Key_Type;

      PKCS11_ECDSA_Verification_Operation(const PKCS11_EC_PublicKey& key, const std::string& emsa)
         : PK_Ops::Verification(), m_key(key), m_order(key.domain().get_order()), m_mechanism(MechanismWrapper::create_ecdsa_mechanism(emsa))
         {}

      size_t message_parts() const override
         {
         return 2;
         }

      size_t message_part_size() const override
         {
         return m_order.bytes();
         }

      size_t max_input_bits() const override
         {
         return m_order.bits();
         }

      void update(const byte msg[], size_t msg_len) override
         {
         if(!m_initialized)
            {
            // first call to update: initialize and cache message because we can not determine yet whether a single- or multiple-part operation will be performed
            m_key.module()->C_VerifyInit(m_key.session().handle(), m_mechanism.data(), m_key.handle());
            m_initialized = true;
            m_first_message = secure_vector<byte>(msg, msg + msg_len);
            return;
            }

         if(!m_first_message.empty())
            {
            // second call to update: start multiple-part operation
            m_key.module()->C_VerifyUpdate(m_key.session().handle(), m_first_message);
            m_first_message.clear();
            }

         m_key.module()->C_VerifyUpdate(m_key.session().handle(), const_cast<Byte*>(msg), msg_len);
         }

      bool is_valid_signature(const byte sig[], size_t sig_len) override
         {
         ReturnValue return_value = ReturnValue::SignatureInvalid;
         if(!m_first_message.empty())
            {
            // single call to update: perform single-part operation
            m_key.module()->C_Verify(m_key.session().handle(), m_first_message.data(), m_first_message.size(),
                                     const_cast<Byte*>(sig), sig_len, &return_value);
            m_first_message.clear();
            }
         else
            {
            // multiple calls to update (or none): finish multiple-part operation
            m_key.module()->C_VerifyFinal(m_key.session().handle(), const_cast<Byte*>(sig), sig_len, &return_value);
            }
         m_initialized = false;
         if(return_value != ReturnValue::OK && return_value != ReturnValue::SignatureInvalid)
            {
            throw PKCS11_ReturnError(return_value);
            }
         return return_value == ReturnValue::OK;
         }

   private:
      const PKCS11_EC_PublicKey& m_key;
      const BigInt& m_order;
      MechanismWrapper m_mechanism;
      secure_vector<byte> m_first_message;
      bool m_initialized = false;
   };

BOTAN_REGISTER_TYPE(PK_Ops::Signature, PKCS11_ECDSA_Signature_Operation, "ECDSA",
                    (make_pk_op<PK_Ops::Signature, PKCS11_ECDSA_Signature_Operation>), "pkcs11", BOTAN_PKCS11_ECDSA_PRIO);

BOTAN_REGISTER_TYPE(PK_Ops::Verification, PKCS11_ECDSA_Verification_Operation, "ECDSA",
                    (make_pk_op<PK_Ops::Verification, PKCS11_ECDSA_Verification_Operation>), "pkcs11", BOTAN_PKCS11_ECDSA_PRIO);

}

PKCS11_ECDSA_KeyPair generate_ecdsa_keypair(Session& session, const EC_PublicKeyGenerationProperties& pub_props,
      const EC_PrivateKeyGenerationProperties& priv_props)
   {
   ObjectHandle pub_key_handle = 0;
   ObjectHandle priv_key_handle = 0;

   Mechanism mechanism = { static_cast<CK_MECHANISM_TYPE>(MechanismType::EcKeyPairGen), nullptr, 0 };

   session.module()->C_GenerateKeyPair(session.handle(), &mechanism,
                                       pub_props.data(), pub_props.count(), priv_props.data(), priv_props.count(),
                                       &pub_key_handle, &priv_key_handle);

   return std::make_pair(PKCS11_ECDSA_PublicKey(session, pub_key_handle), PKCS11_ECDSA_PrivateKey(session,
                         priv_key_handle));
   }

}

}

#endif
