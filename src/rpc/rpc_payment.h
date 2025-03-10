// Copyright (c) 2018-2019, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <boost/thread/mutex.hpp>
#include <boost/serialization/version.hpp>
#include "cryptonote_basic/blobdatatype.h"
#include "cryptonote_basic/cryptonote_basic.h"

namespace cryptonote
{
  class rpc_payment
  {
  public:
    struct client_info
    {
      cryptonote::block block;
      cryptonote::block previous_block;
      cryptonote::blobdata hashing_blob;
      cryptonote::blobdata previous_hashing_blob;
      uint32_t cookie;
      crypto::hash top;
      crypto::hash previous_top;
      uint64_t credits;
      std::unordered_set<uint64_t> payments;
      std::unordered_set<uint64_t> previous_payments;
      uint64_t update_time;
      uint64_t last_request_timestamp;
      uint64_t block_template_update_time;
      uint64_t credits_total;
      uint64_t credits_used;
      uint64_t nonces_good;
      uint64_t nonces_stale;
      uint64_t nonces_bad;
      uint64_t nonces_dupe;

      client_info();

      template <class t_archive>
      inline void serialize(t_archive &a, const unsigned int ver)
      {
        a & block;
        a & previous_block;
        a & hashing_blob;
        a & previous_hashing_blob;
        a & cookie;
        a & top;
        a & previous_top;
        a & credits;
        a & payments;
        a & previous_payments;
        a & update_time;
        a & last_request_timestamp;
        a & block_template_update_time;
        a & credits_total;
        a & credits_used;
        a & nonces_good;
        a & nonces_stale;
        a & nonces_bad;
        a & nonces_dupe;
      }
    };

  public:
    rpc_payment(const cryptonote::account_public_address &address, uint64_t diff, uint64_t credits_per_hash_found);
    uint64_t balance(const crypto::public_key &client, int64_t delta = 0);
    bool pay(const crypto::public_key &client, uint64_t ts, uint64_t payment, const std::string &rpc, bool same_ts, uint64_t &credits);
    bool get_info(const crypto::public_key &client, const std::function<bool(const cryptonote::blobdata&, cryptonote::block&)> &get_block_template, cryptonote::blobdata &hashing_blob, const crypto::hash &top, uint64_t &diff, uint64_t &credits_per_hash_found, uint64_t &credits, uint32_t &cookie);
    bool submit_nonce(const crypto::public_key &client, uint32_t nonce, const crypto::hash &top, int64_t &error_code, std::string &error_message, uint64_t &credits, crypto::hash &hash, cryptonote::block &block, uint32_t cookie, bool &stale);
    const cryptonote::account_public_address &get_payment_address() const { return m_address; }
    bool foreach(const std::function<bool(const crypto::public_key &client, const client_info &info)> &f) const;
    unsigned int flush_by_age(time_t seconds = 0);
    uint64_t get_hashes(unsigned int seconds) const;
    void prune_hashrate(unsigned int seconds);
    bool on_idle();

    template <class t_archive>
    inline void serialize(t_archive &a, const unsigned int ver)
    {
      a & m_client_info;
      a & m_hashrate;
      a & m_credits_total;
      a & m_credits_used;
      a & m_nonces_good;
      a & m_nonces_stale;
      a & m_nonces_bad;
      a & m_nonces_dupe;
    }

    bool load(std::string directory);
    bool store(const std::string &directory = std::string()) const;

  private:
    cryptonote::account_public_address m_address;
    uint64_t m_diff;
    uint64_t m_credits_per_hash_found;
    std::unordered_map<crypto::public_key, client_info> m_client_info;
    std::string m_directory;
    std::map<uint64_t, uint64_t> m_hashrate;
    uint64_t m_credits_total;
    uint64_t m_credits_used;
    uint64_t m_nonces_good;
    uint64_t m_nonces_stale;
    uint64_t m_nonces_bad;
    uint64_t m_nonces_dupe;
    mutable boost::mutex mutex;
  };
}

BOOST_CLASS_VERSION(cryptonote::rpc_payment, 0);
BOOST_CLASS_VERSION(cryptonote::rpc_payment::client_info, 0);
