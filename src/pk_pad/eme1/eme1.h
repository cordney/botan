/*************************************************
* EME1 Header File                               *
* (C) 1999-2007 Jack Lloyd                       *
*************************************************/

#ifndef BOTAN_EME1_H__
#define BOTAN_EME1_H__

#include <botan/pk_pad.h>
#include <botan/kdf.h>

namespace Botan {

/*************************************************
* EME1                                           *
*************************************************/
class BOTAN_DLL EME1 : public EME
   {
   public:
      u32bit maximum_input_size(u32bit) const;

      /**
       EME1 constructor. Hash will be deleted by ~EME1 (when mgf is deleted)

       P is an optional label. Normally empty.
      */
      EME1(HashFunction* hash, const std::string& P = "");

      ~EME1() { delete mgf; }
   private:
      SecureVector<byte> pad(const byte[], u32bit, u32bit,
                             RandomNumberGenerator&) const;
      SecureVector<byte> unpad(const byte[], u32bit, u32bit) const;

      const u32bit HASH_LENGTH;
      SecureVector<byte> Phash;
      MGF* mgf;
   };

}

#endif
