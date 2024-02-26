// Copyright (c) 2014-2019, The Monero Project
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
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include <unordered_set>
#include <random>
#include "include_base_utils.h"
#include "string_tools.h"
using namespace epee;

#include "common/equilibria.h"
#include "common/apply_permutation.h"
#include "cryptonote_tx_utils.h"
#include "cryptonote_config.h"
#include "blockchain.h"
#include "cryptonote_basic/miner.h"
#include "cryptonote_basic/tx_extra.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "ringct/rctSigs.h"
#include "multisig/multisig.h"
#include "int-util.h"
#include "cryptonote_core/service_node_list.h"

using namespace crypto;

namespace cryptonote
{
  //---------------------------------------------------------------
	static  void classify_addresses(const std::vector<tx_destination_entry> &destinations, const boost::optional<cryptonote::tx_destination_entry>& change_addr, size_t &num_stdaddresses, size_t &num_subaddresses, account_public_address &single_dest_subaddress)
	{
    num_stdaddresses = 0;
    num_subaddresses = 0;
    std::unordered_set<cryptonote::account_public_address> unique_dst_addresses;
    bool change_found = false;
    for(const tx_destination_entry& dst_entr: destinations)
    {
      if (change_addr && *change_addr == dst_entr && !change_found)
      {
		    change_found = true;
        continue;
      }
      if (unique_dst_addresses.count(dst_entr.addr) == 0)
      {
        unique_dst_addresses.insert(dst_entr.addr);
        if (dst_entr.is_subaddress)
        {
          ++num_subaddresses;
          single_dest_subaddress = dst_entr.addr;
        }
        else
        {
          ++num_stdaddresses;
        }
      }
    }
    LOG_PRINT_L2("destinations include " << num_stdaddresses << " standard addresses and " << num_subaddresses << " subaddresses");
  }

	bool get_deterministic_output_key(const account_public_address& address, const keypair& tx_key, size_t output_index, crypto::public_key& output_key)
	{
		crypto::key_derivation derivation = AUTO_VAL_INIT(derivation);
		bool r = crypto::generate_key_derivation(address.m_view_public_key, tx_key.sec, derivation);
		CHECK_AND_ASSERT_MES(r, false, "failed to generate key derivation(" << address.m_view_public_key << ", " << tx_key.sec << ")");

		r = crypto::derive_public_key(derivation, output_index, address.m_spend_public_key, output_key);
		CHECK_AND_ASSERT_MES(r, false, "Failed to derive_public_key(" << derivation << ", " << output_index << ", " << address.m_spend_public_key << ")");

		return true;
	}

	bool validate_governance_reward_key(uint64_t height, const std::string& governance_wallet_address_str, size_t output_index, const crypto::public_key& output_key, const cryptonote::network_type nettype)
	{
		keypair sn_key = get_deterministic_keypair_from_height(height);

		cryptonote::address_parse_info governance_wallet_address;
		cryptonote::get_account_address_from_str(governance_wallet_address, nettype, governance_wallet_address_str);

		crypto::public_key correct_key;

		if(!get_deterministic_output_key(governance_wallet_address.address, sn_key, output_index, correct_key))
		{
			MERROR("Failed to generate deterministic output key for governance wallet output validation");
			return false;
		}

		return correct_key == output_key;
  }
  bool validate_dev_fund_reward_key(uint64_t height, const std::string& dev_fund_wallet_address_str, size_t output_index, const crypto::public_key& output_key, const cryptonote::network_type nettype)
  {
    keypair sn_key = get_deterministic_keypair_from_height(height);
    cryptonote::address_parse_info dev_fund_wallet_address;
    cryptonote::get_account_address_from_str(dev_fund_wallet_address, nettype, dev_fund_wallet_address_str);
    crypto::public_key correct_key;
    if(!get_deterministic_output_key(dev_fund_wallet_address.address, sn_key, output_index, correct_key))
    {
      MERROR("Failed to generate deterministic output key for dev fund wallet output validation");
      return false;
    }

    return correct_key == output_key;
  }
  //
  keypair get_deterministic_keypair_from_height(uint64_t height)
  {
    keypair k;

    ec_scalar& sec = k.sec;

    for (int i=0; i < 8; i++)
    {
      uint64_t height_byte = height & ((uint64_t)0xFF << (i*8));
      uint8_t byte = height_byte >> i*8;
      sec.data[i] = byte;
    }
    for (int i=8; i < 32; i++)
    {
      sec.data[i] = 0x00;
    }

    generate_keys(k.pub, k.sec, k.sec, true);

    return k;
  }

  uint64_t allow_dev_fund(uint64_t height, cryptonote::network_type nettype)
  {
    uint64_t fork_height = 0;
    if (nettype == MAINNET)
    {
      fork_height = 352846;
      if (height == (fork_height + 703568))
      {
        return 125000 * COIN;
      }
      else if (height > (fork_height + 703568) && (height % 10800 == 0) && height < 1238350)
      {
        return 125000 * COIN;
      }
      else if (height > (fork_height + 885504) && (height % 5400 == 0))
      {
        return 125000 * COIN;
      }
    }

    return 0;
  }

  uint64_t allow_governance(uint64_t height, cryptonote::network_type nettype)
  {
    uint64_t fork_height = 0;
    if(nettype == MAINNET)
    {
      fork_height = 352846;

      if (height == fork_height)
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + 21600))
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + (2 * 21600)))
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + (3 * 21600)))
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + (4 * 21600)))
      {
        return 1000000 * COIN;
      }
      else if(height == (fork_height + (5 * 21600)))
      {
        return 1000000 * COIN;
      }
      else if(height == (fork_height + (6 * 21600)))
      {
        return 1000000 * COIN;
      } else if(height == 500000)
      {
        //wXEQ pre-sale, will be burnt on height 500100
        return 11000000 * COIN;
      } else if(height == 663269)
      {
        return MINT_BRIDGE;
      } else if(height == 841197)
      {
        return BURN_2;
      } else if(height == 898176)
      {
        return CORP_MINT;
      } else if(height == (fork_height + 583654))
      {
        return NEW_XEQ_BRIDGE;
      } else if(height > (fork_height + 583654) && (height % 21600 == 0) && height < 991430)
      {
        return 200000 * COIN;
      } else if (height == (fork_height + 638584))
      {
        return CORP_MINT * 5;
      } else if (height > (fork_height + 638584) && (height % 10800 == 0) && height < 1056414)
      {
        return 225000 * COIN;
      } else if (height == (fork_height + 703568))
      {
        return (0x502f9000 / 0x2 * 0x3) / equilibria::exp2(0xfe014 / 130500.0) / 100 * 10e6;
      } else if (height > (fork_height + (uint64_t)0xd8303) && (height % 2 == 0) && height < (uint64_t)0x12e56f)
      {
        return 0xBA43B7400;
      } else if (height > (fork_height + (uint64_t)0xd8321) && (height % 1 == 0) && height < (uint64_t)0x12e5d4)
      {
        return 0x2540BE400;
      }
    }
    else if(nettype == TESTNET)
    {
      fork_height = 250;
      if (height == fork_height && nettype == TESTNET)
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + 216))
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + (2 * 216)))
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + (3 * 216)))
      {
        return 1000000 * COIN;
      }
      else if (height == (fork_height + (4 * 216)))
      {
        return 1000000 * COIN;
      }
      else if(height == (fork_height + (5 * 216)))
      {
        return 1000000 * COIN;
      }
      else if(height == (fork_height + (6 * 216)))
      {
        return 1000000 * COIN;
      }
      else if(height == (fork_height + 7))
      {
        return NEW_XEQ_BRIDGE;
      }
      else if(height > (fork_height + 7) && (height % 10 == 0))
      {
        return 200000 * COIN;
      }
      else if (height == (fork_height + 50))
      {
        return CORP_MINT * 5;
      }
      else if (height > (fork_height + 50) && (height % 5 == 0))
      {
        return 225000 * COIN;
      }
      else if(height == 500000)
      { //wXEQ + extra wXEQ 1M LP rewards!
        return 11000000 * COIN;
      }
    }
    else if (nettype == STAGENET)
    {
      fork_height = 12000;
    }

    return 0;
  }
  //---------------------------------------------------------------
  const int SERVICE_NODE_BASE_REWARD_DIVISOR = 2;

  uint64_t service_node_reward_formula(uint64_t base_reward, uint8_t hard_fork_version)
  {
    if(hard_fork_version > 11)
      return base_reward / 4 * 3;
    if(hard_fork_version >= SERVICE_NODE_VERSION)
      return base_reward / 2;
    return 0;
  }

  uint64_t get_portion_of_reward(uint64_t portions, uint64_t total_service_node_reward)
  {
	  uint64_t hi, lo, rewardhi, rewardlo;
	  lo = mul128(total_service_node_reward, portions, &hi);
	  div128_64(hi, lo, STAKING_PORTIONS, &rewardhi, &rewardlo);
	  return rewardlo;
  }

  static uint64_t calculate_sum_of_portions(const std::vector<std::pair<cryptonote::account_public_address, uint64_t>>& portions, block_reward_parts brr, uint8_t hf_version)
  {
    uint64_t reward = 0;
    for (size_t i = 0; i < portions.size(); i++)
    {
      if (hf_version >= 17)
      {
        reward += get_portion_of_reward(portions[i].second, brr.service_node_total);
      }
      else if (hf_version >= 12)
      {
        if(i == 0)
        {
          reward += get_portion_of_reward(portions[i].second, brr.operator_reward);
        } else {
          reward += get_portion_of_reward(portions[i].second, brr.staker_reward);
        }

      } else {
        reward += get_portion_of_reward(portions[i].second, brr.service_node_total);
      }

    }
	  return reward;
  }


  miner_tx_context::miner_tx_context(network_type type, crypto::public_key winner, std::vector<std::pair<account_public_address, stake_portions>> winner_info)
	  : nettype(type)
	  , snode_winner_key(winner)
	  , snode_winner_info(winner_info)
  {
  }

  //---------------------------------------------------------------
  bool construct_miner_tx(
     uint64_t height,
     size_t median_size,
     uint64_t already_generated_coins,
     size_t current_block_size,
     uint64_t fee,
     const account_public_address &miner_address,
     transaction& tx,
     const blobdata& extra_nonce,
     uint8_t hard_fork_version,
	 const miner_tx_context &miner_context)
    {
	  tx.vin.clear();
	  tx.vout.clear();
	  tx.extra.clear();
	  tx.output_unlock_times.clear();
	  tx.type = txtype::standard;
	  tx.version = transaction::get_max_version_for_hf(hard_fork_version);

	  const network_type                                             nettype = miner_context.nettype;
	  const crypto::public_key                                       &service_node_key = miner_context.snode_winner_key;
	  const std::vector<std::pair<account_public_address, uint64_t>> &service_node_info = miner_context.snode_winner_info.empty() ? service_nodes::null_winner : miner_context.snode_winner_info;

	  keypair txkey = keypair::generate(hw::get_device("default"));
	  add_tx_pub_key_to_extra(tx, txkey.pub);
	  if (!extra_nonce.empty())
		if (!add_extra_nonce_to_tx_extra(tx.extra, extra_nonce))
			return false;
	  if (!sort_tx_extra(tx.extra, tx.extra))
		  return false;

	keypair sn_key = get_deterministic_keypair_from_height(height); // NOTE: Always need since we use same key for service node
	if (already_generated_coins != 0)
	{
		add_tx_pub_key_to_extra(tx, sn_key.pub);
	}

  add_service_node_winner_to_tx_extra(tx.extra, service_node_key);

  txin_gen in;
  in.height = height;

	miner_reward_context block_reward_context = {};
	block_reward_context.fee = fee;
	block_reward_context.height = height;
	block_reward_context.snode_winner_info = miner_context.snode_winner_info;

	block_reward_parts reward_parts;
	if(!get_equilibria_block_reward(median_size, current_block_size, already_generated_coins, hard_fork_version, reward_parts, block_reward_context, height, nettype))
  {
    LOG_PRINT_L0("Failed to calculate block reward");
    return false;
  }

	uint64_t summary_amounts = 0;
	// Miner Reward
	{
		crypto::key_derivation derivation = AUTO_VAL_INIT(derivation);
		crypto::public_key out_eph_public_key = AUTO_VAL_INIT(out_eph_public_key);
		bool r = crypto::generate_key_derivation(miner_address.m_view_public_key, txkey.sec, derivation);
		LOG_PRINT_L1("while creating outs:  to generate_key_derivation(" << miner_address.m_view_public_key << ", " << txkey.sec << ")");

		CHECK_AND_ASSERT_MES(r, false, "while creating outs: failed to generate_key_derivation(" << miner_address.m_view_public_key << ", " << txkey.sec << ")");

		r = crypto::derive_public_key(derivation, 0, miner_address.m_spend_public_key, out_eph_public_key);
		LOG_PRINT_L1("while creating outs:  to derive_public_key(" << derivation << ", " << 0 << ", " << miner_address.m_spend_public_key << ")");
		CHECK_AND_ASSERT_MES(r, false, "while creating outs: failed to derive_public_key(" << derivation << ", " << 0 << ", " << miner_address.m_spend_public_key << ")");

		txout_to_key tk;
		tk.key = out_eph_public_key;

		tx_out out;
		summary_amounts += out.amount = reward_parts.miner_reward();
		out.target = tk;
		tx.vout.push_back(out);
		tx.output_unlock_times.push_back(height + CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW);
	}

	if (hard_fork_version >= SERVICE_NODE_VERSION) // Service Node Reward
	{
		for (size_t i = 0; i < service_node_info.size(); i++)
		{
			crypto::key_derivation derivation = AUTO_VAL_INIT(derivation);
			crypto::public_key out_eph_public_key = AUTO_VAL_INIT(out_eph_public_key);
			bool r = crypto::generate_key_derivation(service_node_info[i].first.m_view_public_key, sn_key.sec, derivation);
			LOG_PRINT_L1("while creating outs: generate_key_derivation(" << service_node_info[i].first.m_view_public_key << ")");
			CHECK_AND_ASSERT_MES(r, false, "while creating outs: failed to generate_key_derivation(" << service_node_info[i].first.m_view_public_key << ", "<< sn_key.sec << ")");
			r = crypto::derive_public_key(derivation, 1 + i, service_node_info[i].first.m_spend_public_key, out_eph_public_key);
			LOG_PRINT_L1("while creating outs:  derive_public_key(" << derivation << ", " << (1 + i) << ", " << service_node_info[i].first.m_spend_public_key << ")");
			CHECK_AND_ASSERT_MES(r, false, "while creating outs: failed to derive_public_key(" << derivation << ", " << (1 + i) << ", " << service_node_info[i].first.m_spend_public_key << ")");


			txout_to_key tk;
			tk.key = out_eph_public_key;
			tx_out out;

      if (hard_fork_version >= 17)
      {
        summary_amounts += out.amount = get_portion_of_reward(service_node_info[i].second, reward_parts.service_node_total);
      }
      else if (hard_fork_version >= 12)
      {
        uint64_t reward_part = i == 0 ? reward_parts.operator_reward : reward_parts.staker_reward;
        summary_amounts += out.amount = get_portion_of_reward(service_node_info[i].second, reward_part);
      }
      else
      {
        summary_amounts += out.amount = get_portion_of_reward(service_node_info[i].second, reward_parts.service_node_total);
      }

			out.target = tk;
			tx.vout.push_back(out);
			tx.output_unlock_times.push_back(height + CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW);
		}
	}

  if(hard_fork_version >= 7 && reward_parts.governance > 0)
  {
      cryptonote::address_parse_info governance_wallet_address;
      if(hard_fork_version < 11) {
        cryptonote::get_account_address_from_str(governance_wallet_address, nettype, *cryptonote::get_config(nettype).GOVERNANCE_WALLET_ADDRESS);
      } else if(hard_fork_version < 14) {
        cryptonote::get_account_address_from_str(governance_wallet_address, nettype, *cryptonote::get_config(nettype).BRIDGE_WALLET_ADDRESS);
      } else if (hard_fork_version < 19) {
        cryptonote::get_account_address_from_str(governance_wallet_address, nettype, *cryptonote::get_config(nettype).NEW_BRIDGE_WALLET_ADDRESS);
      } else {
        cryptonote::get_account_address_from_str(governance_wallet_address, nettype, *cryptonote::get_config(nettype).NEW_GOV_WALLET);
      }
      crypto::public_key out_eph_public_key = AUTO_VAL_INIT(out_eph_public_key);

			if(!get_deterministic_output_key(governance_wallet_address.address, sn_key, tx.vout.size(), out_eph_public_key))
			{
				MERROR("Failed to generate deterministic output key for governance wallet output creation");
				return false;
			}
      txout_to_key tk;
			tk.key = out_eph_public_key;
      tx_out out;

      summary_amounts += out.amount = reward_parts.governance;
      out.target = tk;
			tx.vout.push_back(out);
			tx.output_unlock_times.push_back(height + 4);
  }

  if (hard_fork_version >= 17 && reward_parts.dev_fund > 0)
  {
    cryptonote::address_parse_info dev_fund_wallet_address;
    if (hard_fork_version < 19) {
      cryptonote::get_account_address_from_str(dev_fund_wallet_address, nettype, *cryptonote::get_config(nettype).DEV_FUND_WALLET);
    } else {
      cryptonote::get_account_address_from_str(dev_fund_wallet_address, nettype, *cryptonote::get_config(nettype).NEW_DEV_WALLET);
    }
    crypto::public_key out_eph_public_key = AUTO_VAL_INIT(out_eph_public_key);

    if(!get_deterministic_output_key(dev_fund_wallet_address.address, sn_key, tx.vout.size(), out_eph_public_key))
    {
      MERROR("Failed to generate deterministic output key for dev_fund wallet output creation");
      return false;
    }
    txout_to_key tk;
    tk.key = out_eph_public_key;
    tx_out out;
    summary_amounts += out.amount = reward_parts.dev_fund;
    out.target = tk;
    tx.vout.push_back(out);
    tx.output_unlock_times.push_back(height + CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW);
  }

	uint64_t expected_amount = reward_parts.miner_reward() + reward_parts.service_node_paid + reward_parts.governance + reward_parts.dev_fund;
	CHECK_AND_ASSERT_MES(summary_amounts == expected_amount, false, "Failed to construct miner tx, summary_amounts = " << summary_amounts << " not equal total block_reward = " << expected_amount);

	//lock
	tx.unlock_time = height + CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW;
	tx.vin.push_back(in);
	tx.invalidate_hashes();
	LOG_PRINT_L1("MINER_TX generated ok, block_reward=" << print_money(reward_parts.original_base_reward) << "(" << print_money(reward_parts.original_base_reward - fee) << "+" << print_money(fee)
		 << "), current_block_size=" << current_block_size << ", already_generated_coins=" << already_generated_coins << ", tx_id=" << get_transaction_hash(tx));
	return true;
  }

  bool get_equilibria_block_reward(size_t median_weight, size_t current_block_weight, uint64_t already_generated_coins, uint8_t hard_fork_version, block_reward_parts &result, const miner_reward_context &miner_context, uint64_t height, const cryptonote::network_type nettype)
  {
	  result = {};
	  uint64_t base_reward;
	  if (!get_block_reward(median_weight, current_block_weight, already_generated_coins, base_reward, hard_fork_version, miner_context.height))
	  {
		  MERROR("Failed to calculate base block reward");
		  return false;
	  }

    if(hard_fork_version >= 7)
    {
      result.governance = allow_governance(miner_context.height, nettype);
    }
    else
    {
      result.governance = 0;
    }

    base_reward += result.governance;

    if (hard_fork_version >= 17)
    {
      result.dev_fund = allow_dev_fund(miner_context.height, nettype);
    }
    else
    {
      result.dev_fund = 0;
    }

    base_reward += result.dev_fund;

	  if (base_reward == 0)
	  {
		  MERROR("Unexpected base reward of 0");
		  return false;
	  }

	  if (already_generated_coins == 0)
	  {
		  result.original_base_reward = result.adjusted_base_reward = result.base_miner = base_reward;
		  return true;
	  }

	  result.original_base_reward = base_reward;
	  result.adjusted_base_reward = result.original_base_reward - (result.governance + result.dev_fund);
	  result.service_node_total = service_node_reward_formula(result.adjusted_base_reward, hard_fork_version);
	  result.operator_reward = result.service_node_total / 2;
	  result.staker_reward = result.service_node_total - result.operator_reward;

	  if (miner_context.snode_winner_info.empty())
          {
            result.service_node_paid = calculate_sum_of_portions(service_nodes::null_winner, result, hard_fork_version);
          }
          else
          {
            result.service_node_paid = calculate_sum_of_portions(miner_context.snode_winner_info, result, hard_fork_version);
          }

	  result.base_miner = result.adjusted_base_reward - result.service_node_total;
	  result.base_miner_fee = miner_context.fee;
	  return true;
  }

  //---------------------------------------------------------------
  crypto::public_key get_destination_view_key_pub(const std::vector<tx_destination_entry> &destinations, const boost::optional<cryptonote::tx_destination_entry>& change_addr)
  {
     account_public_address addr = {null_pkey, null_pkey};
    size_t count = 0;
    bool found_change = false;
    for (const auto &i : destinations)
    {
      if (i.amount == 0)
        continue;
      if (change_addr && *change_addr == i && !found_change)
      {
        found_change = true;
        continue;
      }
      if (i.addr == addr)
        continue;
      if (count > 0)
        return null_pkey;
      addr = i.addr;
      ++count;
    }
    if (count == 0 && change_addr)
      return change_addr->addr.m_view_public_key;
    return addr.m_view_public_key;
  }
  //---------------------------------------------------------------
  bool construct_tx_with_tx_key(const account_keys& sender_account_keys, const std::unordered_map<crypto::public_key, subaddress_index>& subaddresses, std::vector<tx_source_entry>& sources, std::vector<tx_destination_entry>& destinations, const boost::optional<cryptonote::tx_destination_entry>& change_addr, const std::vector<uint8_t> &extra, transaction& tx, uint64_t unlock_time, const crypto::secret_key &tx_key, const std::vector<crypto::secret_key> &additional_tx_keys, const rct::RCTConfig &rct_config, rct::multisig_out *msout, bool shuffle_outs, xeq_construct_tx_params const &tx_params)  {
    hw::device &hwdev = sender_account_keys.get_device();

    if (sources.empty())
    {
      LOG_ERROR("Empty sources");
      return false;
    }

    std::vector<rct::key> amount_keys;
    tx.set_null();
    amount_keys.clear();
    if (msout)
    {
      msout->c.clear();
    }

    tx.version = transaction::get_max_version_for_hf(tx_params.hard_fork_version);
    tx.type = tx_params.tx_type;

    if (tx.version <= txversion::v2)
      tx.unlock_time = unlock_time;

    tx.extra = extra;
    crypto::public_key txkey_pub;


    // if we have a stealth payment id, find it and encrypt it with the tx key now
    std::vector<tx_extra_field> tx_extra_fields;
    if (parse_tx_extra(tx.extra, tx_extra_fields))
    {
      bool add_dummy_payment_id = true;
      tx_extra_nonce extra_nonce;
      if (find_tx_extra_field_by_type(tx_extra_fields, extra_nonce))
      {
        crypto::hash payment_id = null_hash;
        crypto::hash8 payment_id8 = null_hash8;
        if (get_encrypted_payment_id_from_tx_extra_nonce(extra_nonce.nonce, payment_id8))
        {
          LOG_PRINT_L2("Encrypting payment id " << payment_id8);
          crypto::public_key view_key_pub = get_destination_view_key_pub(destinations, change_addr);
          if (view_key_pub == null_pkey)
          {
            LOG_ERROR("Destinations have to have exactly one output to support encrypted payment ids");
            return false;
          }

          if (!hwdev.encrypt_payment_id(payment_id8, view_key_pub, tx_key))
          {
            LOG_ERROR("Failed to encrypt payment id");
            return false;
          }

          std::string extra_nonce;
          set_encrypted_payment_id_to_tx_extra_nonce(extra_nonce, payment_id8);
          remove_field_from_tx_extra(tx.extra, typeid(tx_extra_nonce));
          if (!add_extra_nonce_to_tx_extra(tx.extra, extra_nonce))
          {
            LOG_ERROR("Failed to add encrypted payment id to tx extra");
            return false;
          }
          LOG_PRINT_L1("Encrypted payment ID: " << payment_id8);
          add_dummy_payment_id = false;
        }
        else if (get_payment_id_from_tx_extra_nonce(extra_nonce.nonce, payment_id))
        {
          add_dummy_payment_id = false;
        }
      }

      // we don't add one if we've got more than the usual 1 destination plus change
      if (destinations.size() > 2)
        add_dummy_payment_id = false;

      if (add_dummy_payment_id)
      {
        // if we have neither long nor short payment id, add a dummy short one,
        // this should end up being the vast majority of txes as time goes on
        std::string extra_nonce;
        crypto::hash8 payment_id8 = null_hash8;
        crypto::public_key view_key_pub = get_destination_view_key_pub(destinations, change_addr);
        if (view_key_pub == null_pkey)
        {
          LOG_ERROR("Failed to get key to encrypt dummy payment id with");
        }
        else
        {
          hwdev.encrypt_payment_id(payment_id8, view_key_pub, tx_key);
          set_encrypted_payment_id_to_tx_extra_nonce(extra_nonce, payment_id8);
          if (!add_extra_nonce_to_tx_extra(tx.extra, extra_nonce))
          {
            LOG_ERROR("Failed to add dummy encrypted payment id to tx extra");
            // continue anyway
          }
        }
      }
    }
    else
    {
      MWARNING("Failed to parse tx extra");
      tx_extra_fields.clear();
    }

    struct input_generation_context_data
    {
      keypair in_ephemeral;
    };
    std::vector<input_generation_context_data> in_contexts;

    uint64_t summary_inputs_money = 0;
    //fill inputs
    int idx = -1;
    for(const tx_source_entry& src_entr:  sources)
    {
      ++idx;
      if(src_entr.real_output >= src_entr.outputs.size())
      {
        LOG_ERROR("real_output index (" << src_entr.real_output << ")bigger than output_keys.size()=" << src_entr.outputs.size());
        return false;
      }
      summary_inputs_money += src_entr.amount;

      //key_derivation recv_derivation;
      in_contexts.push_back(input_generation_context_data());
      keypair& in_ephemeral = in_contexts.back().in_ephemeral;
      crypto::key_image img;
      const auto& out_key = reinterpret_cast<const crypto::public_key&>(src_entr.outputs[src_entr.real_output].second.dest);
      if(!generate_key_image_helper(sender_account_keys, subaddresses, out_key, src_entr.real_out_tx_key, src_entr.real_out_additional_tx_keys, src_entr.real_output_in_tx_index, in_ephemeral,img, hwdev))
      {
        LOG_ERROR("Key image generation failed!");
        return false;
      }

      //check that derivated key is equal with real output key (if non multisig)
      if(!msout && !(in_ephemeral.pub == src_entr.outputs[src_entr.real_output].second.dest) )
      {
        LOG_ERROR("derived public key mismatch with output public key at index " << idx << ", real out " << src_entr.real_output << "! "<< ENDL << "derived_key:"
          << string_tools::pod_to_hex(in_ephemeral.pub) << ENDL << "real output_public_key:"
          << string_tools::pod_to_hex(src_entr.outputs[src_entr.real_output].second.dest) );
        LOG_ERROR("amount " << src_entr.amount << ", rct " << src_entr.rct);
        LOG_ERROR("tx pubkey " << src_entr.real_out_tx_key << ", real_output_in_tx_index " << src_entr.real_output_in_tx_index);
        return false;
      }

      //put key image into tx input
      txin_to_key input_to_key;
      input_to_key.amount = src_entr.amount;
      input_to_key.k_image = msout ? rct::rct2ki(src_entr.multisig_kLRki.ki) : img;

      //fill outputs array and use relative offsets
      for(const tx_source_entry::output_entry& out_entry: src_entr.outputs)
        input_to_key.key_offsets.push_back(out_entry.first);

      input_to_key.key_offsets = absolute_output_offsets_to_relative(input_to_key.key_offsets);
      tx.vin.push_back(input_to_key);
    }

    if (shuffle_outs)
    {
      std::shuffle(destinations.begin(), destinations.end(), crypto::random_device{});
    }

    // sort ins by their key image
    std::vector<size_t> ins_order(sources.size());
    for (size_t n = 0; n < sources.size(); ++n)
      ins_order[n] = n;
    std::sort(ins_order.begin(), ins_order.end(), [&](const size_t i0, const size_t i1) {
      const txin_to_key &tk0 = boost::get<txin_to_key>(tx.vin[i0]);
      const txin_to_key &tk1 = boost::get<txin_to_key>(tx.vin[i1]);
      return memcmp(&tk0.k_image, &tk1.k_image, sizeof(tk0.k_image)) > 0;
    });
    tools::apply_permutation(ins_order, [&] (size_t i0, size_t i1) {
      std::swap(tx.vin[i0], tx.vin[i1]);
      std::swap(in_contexts[i0], in_contexts[i1]);
      std::swap(sources[i0], sources[i1]);
    });

    // figure out if we need to make additional tx pubkeys
    size_t num_stdaddresses = 0;
    size_t num_subaddresses = 0;
    account_public_address single_dest_subaddress;
    classify_addresses(destinations, change_addr, num_stdaddresses, num_subaddresses, single_dest_subaddress);

    // if this is a single-destination transfer to a subaddress, we set the tx pubkey to R=s*D
    if (num_stdaddresses == 0 && num_subaddresses == 1)
    {
      txkey_pub = rct::rct2pk(hwdev.scalarmultKey(rct::pk2rct(single_dest_subaddress.m_spend_public_key), rct::sk2rct(tx_key)));
    }
    else
    {
      txkey_pub = rct::rct2pk(hwdev.scalarmultBase(rct::sk2rct(tx_key)));
    }
    remove_field_from_tx_extra(tx.extra, typeid(tx_extra_pub_key));
    add_tx_pub_key_to_extra(tx, txkey_pub);

    std::vector<crypto::public_key> additional_tx_public_keys;

    // we don't need to include additional tx keys if:
    //   - all the destinations are standard addresses
    //   - there's only one destination which is a subaddress
    bool need_additional_txkeys = num_subaddresses > 0 && (num_stdaddresses > 0 || num_subaddresses > 1);
    if (need_additional_txkeys)
      CHECK_AND_ASSERT_MES(destinations.size() == additional_tx_keys.size(), false, "Wrong amount of additional tx keys");

    uint64_t summary_outs_money = 0;
    bool found_change = false;

    //fill outputs
    size_t output_index = 0;

    for(const tx_destination_entry& dst_entr: destinations)
    {
      CHECK_AND_ASSERT_MES(dst_entr.amount > 0 || tx.version >= txversion::v2, false, "Destination with wrong amount: " << dst_entr.amount);
      crypto::public_key out_eph_public_key;

      hwdev.generate_output_ephemeral_keys(tx.version,sender_account_keys, txkey_pub, tx_key,
                                          dst_entr, change_addr, output_index,
                                          need_additional_txkeys, additional_tx_keys,
                                          additional_tx_public_keys, amount_keys, out_eph_public_key, found_change, tx.output_unlock_times, unlock_time);
      tx_out out;
      out.amount = dst_entr.amount;
      txout_to_key tk;
      tk.key = out_eph_public_key;
      out.target = tk;
      tx.vout.push_back(out);
      output_index++;
      summary_outs_money += dst_entr.amount;
    }
    CHECK_AND_ASSERT_MES(additional_tx_public_keys.size() == additional_tx_keys.size(), false, "Internal error creating additional public keys");

    remove_field_from_tx_extra(tx.extra, typeid(tx_extra_additional_pub_keys));

    LOG_PRINT_L2("tx pubkey: " << txkey_pub);
    if (need_additional_txkeys)
    {
      LOG_PRINT_L2("additional tx pubkeys: ");
      for (size_t i = 0; i < additional_tx_public_keys.size(); ++i)
        LOG_PRINT_L2(additional_tx_public_keys[i]);
      add_additional_tx_pub_keys_to_extra(tx.extra, additional_tx_public_keys);
    }

    if (!sort_tx_extra(tx.extra, tx.extra))
      return false;

    //check money
    if(summary_outs_money > summary_inputs_money )
    {
      LOG_ERROR("Transaction inputs money ("<< summary_inputs_money << ") less than outputs money (" << summary_outs_money << ")");
      return false;
    }

    // check for watch only wallet
    bool zero_secret_key = true;
    for (size_t i = 0; i < sizeof(sender_account_keys.m_spend_secret_key); ++i)
      zero_secret_key &= (sender_account_keys.m_spend_secret_key.data[i] == 0);
    if (zero_secret_key)
    {
      MDEBUG("Null secret key, skipping signatures");
    }

    if (tx.version == txversion::v1)
    {
      //generate ring signatures
      crypto::hash tx_prefix_hash;
      get_transaction_prefix_hash(tx, tx_prefix_hash);

      std::stringstream ss_ring_s;
      size_t i = 0;
      for(const tx_source_entry& src_entr:  sources)
      {
        ss_ring_s << "pub_keys:" << ENDL;
        std::vector<const crypto::public_key*> keys_ptrs;
        std::vector<crypto::public_key> keys(src_entr.outputs.size());
        size_t ii = 0;
        for(const tx_source_entry::output_entry& o: src_entr.outputs)
        {
          keys[ii] = rct2pk(o.second.dest);
          keys_ptrs.push_back(&keys[ii]);
          ss_ring_s << o.second.dest << ENDL;
          ++ii;
        }

        tx.signatures.push_back(std::vector<crypto::signature>());
        std::vector<crypto::signature>& sigs = tx.signatures.back();
        sigs.resize(src_entr.outputs.size());
        if (!zero_secret_key)
          crypto::generate_ring_signature(tx_prefix_hash, boost::get<txin_to_key>(tx.vin[i]).k_image, keys_ptrs, in_contexts[i].in_ephemeral.sec, src_entr.real_output, sigs.data());
        ss_ring_s << "signatures:" << ENDL;
        std::for_each(sigs.begin(), sigs.end(), [&](const crypto::signature& s){ss_ring_s << s << ENDL;});
        ss_ring_s << "prefix_hash:" << tx_prefix_hash << ENDL << "in_ephemeral_key: " << in_contexts[i].in_ephemeral.sec << ENDL << "real_output: " << src_entr.real_output << ENDL;
        i++;
      }

      MCINFO("construct_tx", "transaction_created: " << get_transaction_hash(tx) << ENDL << obj_to_json_str(tx) << ENDL << ss_ring_s.str());
    }
    else
    {
      size_t n_total_outs = sources[0].outputs.size(); // only for non-simple rct

      // the non-simple version is slightly smaller, but assumes all real inputs
      // are on the same index, so can only be used if there just one ring.
      bool use_simple_rct = sources.size() > 1 || rct_config.range_proof_type != rct::RangeProofBorromean;

      if (!use_simple_rct)
      {
        // non simple ringct requires all real inputs to be at the same index for all inputs
        for(const tx_source_entry& src_entr:  sources)
        {
          if(src_entr.real_output != sources.begin()->real_output)
          {
            LOG_ERROR("All inputs must have the same index for non-simple ringct");
            return false;
          }
        }

        // enforce same mixin for all outputs
        for (size_t i = 1; i < sources.size(); ++i) {
          if (n_total_outs != sources[i].outputs.size()) {
            LOG_ERROR("Non-simple ringct transaction has varying ring size");
            return false;
          }
        }
      }

      uint64_t amount_in = 0, amount_out = 0;
      rct::ctkeyV inSk;
      inSk.reserve(sources.size());
      // mixRing indexing is done the other way round for simple
      rct::ctkeyM mixRing(use_simple_rct ? sources.size() : n_total_outs);
      rct::keyV destinations;
      std::vector<uint64_t> inamounts, outamounts;
      std::vector<unsigned int> index;
      std::vector<rct::multisig_kLRki> kLRki;
      for (size_t i = 0; i < sources.size(); ++i)
      {
        rct::ctkey ctkey;
        amount_in += sources[i].amount;
        inamounts.push_back(sources[i].amount);
        index.push_back(sources[i].real_output);
        // inSk: (secret key, mask)
        ctkey.dest = rct::sk2rct(in_contexts[i].in_ephemeral.sec);
        ctkey.mask = sources[i].mask;
        inSk.push_back(ctkey);
        memwipe(&ctkey, sizeof(rct::ctkey));
        // inPk: (public key, commitment)
        // will be done when filling in mixRing
        if (msout)
        {
          kLRki.push_back(sources[i].multisig_kLRki);
        }
      }
      for (size_t i = 0; i < tx.vout.size(); ++i)
      {
        destinations.push_back(rct::pk2rct(boost::get<txout_to_key>(tx.vout[i].target).key));
        outamounts.push_back(tx.vout[i].amount);
        amount_out += tx.vout[i].amount;
      }

      if (use_simple_rct)
      {
        // mixRing indexing is done the other way round for simple
        for (size_t i = 0; i < sources.size(); ++i)
        {
          mixRing[i].resize(sources[i].outputs.size());
          for (size_t n = 0; n < sources[i].outputs.size(); ++n)
          {
            mixRing[i][n] = sources[i].outputs[n].second;
          }
        }
      }
      else
      {
        for (size_t i = 0; i < n_total_outs; ++i) // same index assumption
        {
          mixRing[i].resize(sources.size());
          for (size_t n = 0; n < sources.size(); ++n)
          {
            mixRing[i][n] = sources[n].outputs[i].second;
          }
        }
      }

      // fee
      if (!use_simple_rct && amount_in > amount_out)
        outamounts.push_back(amount_in - amount_out);

      // zero out all amounts to mask rct outputs, real amounts are now encrypted
      for (size_t i = 0; i < tx.vin.size(); ++i)
      {
        if (sources[i].rct)
          boost::get<txin_to_key>(tx.vin[i]).amount = 0;
      }
      for (size_t i = 0; i < tx.vout.size(); ++i)
        tx.vout[i].amount = 0;

      crypto::hash tx_prefix_hash;
      get_transaction_prefix_hash(tx, tx_prefix_hash);
      rct::ctkeyV outSk;

      if (use_simple_rct)
        tx.rct_signatures = rct::genRctSimple(rct::hash2rct(tx_prefix_hash), inSk, destinations, inamounts, outamounts, amount_in - amount_out, mixRing, amount_keys, msout ? &kLRki : NULL, msout, index, outSk, rct_config, hwdev);
      else
        tx.rct_signatures = rct::genRct(rct::hash2rct(tx_prefix_hash), inSk, destinations, outamounts, mixRing, amount_keys, msout ? &kLRki[0] : NULL, msout, sources[0].real_output, outSk, rct_config, hwdev); // same index assumption
      memwipe(inSk.data(), inSk.size() * sizeof(rct::ctkey));

      CHECK_AND_ASSERT_MES(tx.vout.size() == outSk.size(), false, "outSk size does not match vout");

      MCINFO("construct_tx", "transaction_created: " << get_transaction_hash(tx) << ENDL << obj_to_json_str(tx) << ENDL);
    }

    tx.invalidate_hashes();

    return true;
  }
  //---------------------------------------------------------------
  bool construct_tx_and_get_tx_key(const account_keys& sender_account_keys, const std::unordered_map<crypto::public_key, subaddress_index>& subaddresses, std::vector<tx_source_entry>& sources, std::vector<tx_destination_entry>& destinations, const boost::optional<cryptonote::tx_destination_entry>& change_addr, const std::vector<uint8_t> &extra, transaction& tx, uint64_t unlock_time, crypto::secret_key &tx_key, std::vector<crypto::secret_key> &additional_tx_keys, const rct::RCTConfig &rct_config, rct::multisig_out *msout, xeq_construct_tx_params const &tx_params)
  {
    hw::device &hwdev = sender_account_keys.get_device();
    hwdev.open_tx(tx_key);

     // figure out if we need to make additional tx pubkeys
    size_t num_stdaddresses = 0;
    size_t num_subaddresses = 0;
    account_public_address single_dest_subaddress;
    classify_addresses(destinations, change_addr, num_stdaddresses, num_subaddresses, single_dest_subaddress);
    bool need_additional_txkeys = num_subaddresses > 0 && (num_stdaddresses > 0 || num_subaddresses > 1);
    if (need_additional_txkeys)
    {
      additional_tx_keys.clear();
      for (size_t i = 0; i < destinations.size(); ++i)
        additional_tx_keys.push_back(keypair::generate(sender_account_keys.get_device()).sec);
    }

    try {
      if (tx.type == txtype::stake || tx.type == txtype::swap)
        add_tx_secret_key_to_tx_extra(tx.extra, tx_key);

    bool r = construct_tx_with_tx_key(sender_account_keys, subaddresses, sources, destinations, change_addr, extra, tx, unlock_time, tx_key, additional_tx_keys, rct_config, msout, true/*shuffle_outs*/, tx_params);
      hwdev.close_tx();
      return r;
    } catch(...) {
      hwdev.close_tx();
      throw;
    }
  }
  //---------------------------------------------------------------
  bool construct_tx(const account_keys& sender_account_keys, std::vector<tx_source_entry>& sources, const std::vector<tx_destination_entry>& destinations, const boost::optional<cryptonote::tx_destination_entry>& change_addr, const std::vector<uint8_t> &extra, transaction& tx, uint64_t unlock_time, bool is_staking, bool per_output_unlock, bool is_swap_tx)
  {
     std::unordered_map<crypto::public_key, cryptonote::subaddress_index> subaddresses;
     subaddresses[sender_account_keys.m_account_address.m_spend_public_key] = {0,0};
     crypto::secret_key tx_key;
     std::vector<crypto::secret_key> additional_tx_keys;
     std::vector<tx_destination_entry> destinations_copy = destinations;

     rct::RCTConfig rct_config = {};
     rct_config.range_proof_type = (tx_params.hard_fork_version < 4) ? rct::RangeProofBorromean : rct::RangeProofPaddedBulletproof;
     rct_config.bp_version = (tx_params.hard_fork_version >= 6) ? 2 : (tx_params.hard_fork_version >= 4) ? 1 : 0;

     return construct_tx_and_get_tx_key(sender_account_keys, subaddresses, sources, destinations_copy, change_addr, extra, tx, unlock_time, tx_key, additional_tx_keys, rct_config, NULL, tx_params);
  }
  //---------------------------------------------------------------
  bool generate_genesis_block(block& bl)
  {
    //genesis block
    bl = {};

    blobdata tx_bl;
    bool r = string_tools::parse_hexstr_to_binbuff(config::GENESIS_TX, tx_bl);
    CHECK_AND_ASSERT_MES(r, false, "failed to parse coinbase tx from hard coded blob");
    r = parse_and_validate_tx_from_blob(tx_bl, bl.miner_tx);
    CHECK_AND_ASSERT_MES(r, false, "failed to parse coinbase tx from hard coded blob");
    bl.major_version = CURRENT_BLOCK_MAJOR_VERSION;
    bl.minor_version = CURRENT_BLOCK_MINOR_VERSION;
    bl.timestamp = 0;
    bl.nonce = config::GENESIS_NONCE;
    miner::find_nonce_for_given_block(bl, 1, 0);
    bl.invalidate_hashes();
    return true;
  }
}
