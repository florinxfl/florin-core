// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2017-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#include "wallet/wallet.h"

#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include "consensus/validation.h"
#include "rpc/server.h"
#include "test/test.h"
#include "validation/validation.h"
#include "blockstore.h"
#include "wallet/test/wallet_test_fixture.h"

#include <boost/test/unit_test.hpp>
#include <univalue.h>

#include <wallet/wallet.h>
#include <wallet/mnemonic.h>

extern CWallet* pwalletMain;

extern UniValue importmulti(const JSONRPCRequest& request);
extern UniValue dumpwallet(const JSONRPCRequest& request);
extern UniValue importwallet(const JSONRPCRequest& request);

// how many times to run all the tests to have a chance to catch errors that only show up with particular random shuffles
#define RUN_TESTS 100

// some tests fail 1% of the time due to bad luck.
// we repeat those tests this many times and only complain if all iterations of the test fail
#define RANDOM_REPEATS 5

std::vector<std::unique_ptr<CWalletTx>> wtxn;

typedef std::set<CInputCoin> CoinSet;

BOOST_FIXTURE_TEST_SUITE(wallet_tests, WalletTestingSetup)

static const CWallet wallet;
static std::vector<COutput> vCoins;

static void add_coin(CWallet& wallet, const CAmount& nValue, int nAge = 6*24, bool fIsFromMe = false, int nInput=0)
{
    static int nextLockTime = 0;
    CMutableTransaction tx(TEST_DEFAULT_TX_VERSION);
    tx.nLockTime = nextLockTime++;        // so all transactions get different hashes
    tx.vout.resize(nInput+1);
    tx.vout[nInput].nValue = nValue;
    if (fIsFromMe) {
        // IsFromMe() returns (GetDebit() > 0), and GetDebit() is 0 if vin.empty(),
        // so stop vin being empty, and cache a non-zero Debit to fake out IsFromMe()
        tx.vin.resize(1);
    }
    std::unique_ptr<CWalletTx> wtx(new CWalletTx(&wallet, MakeTransactionRef(std::move(tx))));
    // patch wtx cache, this is a dirty hack which replaces the former dirty bitcoin hack
    // it would be better if CWallet and CWalletTx could operate in this test environment
    // without needing these hacks to its data stuctures
    //
    if (fIsFromMe)
    {
        wtx->debitCached[0] = nValue;
    }
    COutput output(wtx.get(), nInput, nAge, true /* spendable */, true /* solvable */, true /* safe */);
    vCoins.push_back(output);
    wtxn.emplace_back(std::move(wtx));
}

static void empty_wallet(void)
{
    vCoins.clear();
    wtxn.clear();
}

static bool equal_sets(CoinSet a, CoinSet b)
{
    std::pair<CoinSet::iterator, CoinSet::iterator> ret = mismatch(a.begin(), a.end(), b.begin());
    return ret.first == a.end() && ret.second == b.end();
}

BOOST_AUTO_TEST_CASE(coin_selection_tests)
{
    CoinSet setCoinsRet, setCoinsRet2;
    CAmount nValueRet;

    CWallet wallet;
    LOCK(wallet.cs_wallet);
    wallet.GenerateNewLegacyAccount("My account");

    // test multiple times to allow for differences in the shuffle order
    for (int i = 0; i < RUN_TESTS; i++)
    {
        empty_wallet();

        // with an empty wallet we can't even pay one cent
        BOOST_CHECK(!wallet.SelectCoinsMinConf( 1 * CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet, false));

        add_coin(wallet, 1*CENT, 4);        // add a new 1 cent coin

        // with a new 1 cent coin, we still can't find a mature 1 cent
        BOOST_CHECK(!wallet.SelectCoinsMinConf( 1 * CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet, false));

        // but we can find a new 1 cent
        BOOST_CHECK( wallet.SelectCoinsMinConf( 1 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 1 * CENT);

        add_coin(wallet, 2*CENT);           // add a mature 2 cent coin

        // we can't make 3 cents of mature coins
        BOOST_CHECK(!wallet.SelectCoinsMinConf( 3 * CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet, false));

        // we can make 3 cents of new  coins
        BOOST_CHECK( wallet.SelectCoinsMinConf( 3 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 3 * CENT);

        add_coin(wallet, 5*CENT);           // add a mature 5 cent coin,
        add_coin(wallet, 10*CENT, 3, true); // a new 10 cent coin sent from one of our own addresses
        add_coin(wallet, 20*CENT);          // and a mature 20 cent coin

        // now we have new: 1+10=11 (of which 10 was self-sent), and mature: 2+5+20=27.  total = 38

        // we can't make 38 cents only if we disallow new coins:
        BOOST_CHECK(!wallet.SelectCoinsMinConf(38 * CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet, false));
        // we can't even make 37 cents if we don't allow new coins even if they're from us
        BOOST_CHECK(!wallet.SelectCoinsMinConf(38 * CENT, 6, 6, 0, vCoins, setCoinsRet, nValueRet, false));
        // but we can make 37 cents if we accept new coins from ourself
        BOOST_CHECK( wallet.SelectCoinsMinConf(37 * CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 37 * CENT);
        // and we can make 38 cents if we accept all new coins
        BOOST_CHECK( wallet.SelectCoinsMinConf(38 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 38 * CENT);

        // try making 34 cents from 1,2,5,10,20 - we can't do it exactly
        BOOST_CHECK( wallet.SelectCoinsMinConf(34 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 35 * CENT);       // but 35 cents is closest
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3U);     // the best should be 20+10+5.  it's incredibly unlikely the 1 or 2 got included (but possible)

        // when we try making 7 cents, the smaller coins (1,2,5) are enough.  We should see just 2+5
        BOOST_CHECK( wallet.SelectCoinsMinConf( 7 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 7 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2U);

        // when we try making 8 cents, the smaller coins (1,2,5) are exactly enough.
        BOOST_CHECK( wallet.SelectCoinsMinConf( 8 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK(nValueRet == 8 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3U);

        // when we try making 9 cents, no subset of smaller coins is enough, and we get the next bigger coin (10)
        BOOST_CHECK( wallet.SelectCoinsMinConf( 9 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 10 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1U);

        // now clear out the wallet and start again to test choosing between subsets of smaller coins and the next biggest coin
        empty_wallet();

        add_coin(wallet,  6*CENT);
        add_coin(wallet,  7*CENT);
        add_coin(wallet,  8*CENT);
        add_coin(wallet, 20*CENT);
        add_coin(wallet, 30*CENT); // now we have 6+7+8+20+30 = 71 cents total

        // check that we have 71 and not 72
        BOOST_CHECK( wallet.SelectCoinsMinConf(71 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK(!wallet.SelectCoinsMinConf(72 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));

        // now try making 16 cents.  the best smaller coins can do is 6+7+8 = 21; not as good at the next biggest coin, 20
        BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 20 * CENT); // we should get 20 in one coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1U);

        add_coin(wallet,  5*CENT); // now we have 5+6+7+8+20+30 = 75 cents total

        // now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, better than the next biggest coin, 20
        BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 18 * CENT); // we should get 18 in 3 coins
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3U);

        add_coin(wallet,  18*CENT); // now we have 5+6+7+8+18+20+30

        // and now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, the same as the next biggest coin, 18
        BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 18 * CENT);  // we should get 18 in 1 coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1U); // because in the event of a tie, the biggest coin wins

        // now try making 11 cents.  we should get 5+6
        BOOST_CHECK( wallet.SelectCoinsMinConf(11 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 11 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2U);

        // check that the smallest bigger coin is used
        add_coin(wallet,  1*COIN);
        add_coin(wallet,  2*COIN);
        add_coin(wallet,  3*COIN);
        add_coin(wallet,  4*COIN); // now we have 5+6+7+8+18+20+30+100+200+300+400 = 1094 cents
        BOOST_CHECK( wallet.SelectCoinsMinConf(95 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 1 * COIN);  // we should get 1 NLG in 1 coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1U);

        BOOST_CHECK( wallet.SelectCoinsMinConf(195 * CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 2 * COIN);  // we should get 2 NLG in 1 coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1U);

        // empty the wallet and start again, now with fractions of a cent, to test small change avoidance

        empty_wallet();
        add_coin(wallet, MIN_CHANGE * 1 / 10);
        add_coin(wallet, MIN_CHANGE * 2 / 10);
        add_coin(wallet, MIN_CHANGE * 3 / 10);
        add_coin(wallet, MIN_CHANGE * 4 / 10);
        add_coin(wallet, MIN_CHANGE * 5 / 10);

        // try making 1 * MIN_CHANGE from the 1.5 * MIN_CHANGE
        // we'll get change smaller than MIN_CHANGE whatever happens, so can expect MIN_CHANGE exactly
        BOOST_CHECK( wallet.SelectCoinsMinConf(MIN_CHANGE, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, MIN_CHANGE);

        // but if we add a bigger coin, small change is avoided
        add_coin(wallet, 1111*MIN_CHANGE);

        // try making 1 from 0.1 + 0.2 + 0.3 + 0.4 + 0.5 + 1111 = 1112.5
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * MIN_CHANGE, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 1 * MIN_CHANGE); // we should get the exact amount

        // if we add more small coins:
        add_coin(wallet, MIN_CHANGE * 6 / 10);
        add_coin(wallet, MIN_CHANGE * 7 / 10);

        // and try again to make 1.0 * MIN_CHANGE
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * MIN_CHANGE, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 1 * MIN_CHANGE); // we should get the exact amount

        // run the 'mtgox' test (see http://blockexplorer.com/tx/29a3efd3ef04f9153d47a990bd7b048a4b2d213daaa5fb8ed670fb85f13bdbcf)
        // they tried to consolidate 10 50k coins into one 500k coin, and ended up with 50k in change
        empty_wallet();
        for (int j = 0; j < 20; j++)
            add_coin(wallet, 50000 * COIN);

        BOOST_CHECK( wallet.SelectCoinsMinConf(500000 * COIN, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 500000 * COIN); // we should get the exact amount
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 10U); // in ten coins

        // if there's not enough in the smaller coins to make at least 1 * MIN_CHANGE change (0.5+0.6+0.7 < 1.0+1.0),
        // we need to try finding an exact subset anyway

        // sometimes it will fail, and so we use the next biggest coin:
        empty_wallet();
        add_coin(wallet, MIN_CHANGE * 5 / 10);
        add_coin(wallet, MIN_CHANGE * 6 / 10);
        add_coin(wallet, MIN_CHANGE * 7 / 10);
        add_coin(wallet, 1111 * MIN_CHANGE);
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * MIN_CHANGE, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 1111 * MIN_CHANGE); // we get the bigger coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1U);

        // but sometimes it's possible, and we use an exact subset (0.4 + 0.6 = 1.0)
        empty_wallet();
        add_coin(wallet, MIN_CHANGE * 4 / 10);
        add_coin(wallet, MIN_CHANGE * 6 / 10);
        add_coin(wallet, MIN_CHANGE * 8 / 10);
        add_coin(wallet, 1111 * MIN_CHANGE);
        BOOST_CHECK( wallet.SelectCoinsMinConf(MIN_CHANGE, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, MIN_CHANGE);   // we should get the exact amount
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2U); // in two coins 0.4+0.6

        // test avoiding small change
        empty_wallet();
        add_coin(wallet, MIN_CHANGE * 5 / 100);
        add_coin(wallet, MIN_CHANGE * 1);
        add_coin(wallet, MIN_CHANGE * 100);

        // trying to make 100.01 from these three coins
        BOOST_CHECK(wallet.SelectCoinsMinConf(MIN_CHANGE * 10001 / 100, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, MIN_CHANGE * 10105 / 100); // we should get all coins
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3U);

        // but if we try to make 99.9, we should take the bigger of the two small coins to avoid small change
        BOOST_CHECK(wallet.SelectCoinsMinConf(MIN_CHANGE * 9990 / 100, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
        BOOST_CHECK_EQUAL(nValueRet, 101 * MIN_CHANGE);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2U);

        // test with many inputs
        for (CAmount amt=1500; amt < COIN; amt*=10) {
             empty_wallet();
             // Create 676 inputs (=  (old MAX_STANDARD_TX_SIZE == 100000)  / 148 bytes per input)
             for (uint16_t j = 0; j < 676; j++)
                 add_coin(wallet, amt);
             BOOST_CHECK(wallet.SelectCoinsMinConf(2000, 1, 1, 0, vCoins, setCoinsRet, nValueRet, false));
             if (amt - 2000 < MIN_CHANGE) {
                 // needs more than one input:
                 uint16_t returnSize = std::ceil((2000.0 + MIN_CHANGE)/amt);
                 CAmount returnValue = amt * returnSize;
                 BOOST_CHECK_EQUAL(nValueRet, returnValue);
                 BOOST_CHECK_EQUAL(setCoinsRet.size(), returnSize);
             } else {
                 // one input is sufficient:
                 BOOST_CHECK_EQUAL(nValueRet, amt);
                 BOOST_CHECK_EQUAL(setCoinsRet.size(), 1U);
             }
        }

        // test randomness
        {
            empty_wallet();
            for (int i2 = 0; i2 < 100; i2++)
                add_coin(wallet, COIN);

            // picking 50 from 100 coins doesn't depend on the shuffle,
            // but does depend on randomness in the stochastic approximation code
            BOOST_CHECK(wallet.SelectCoinsMinConf(50 * COIN, 1, 6, 0, vCoins, setCoinsRet , nValueRet, false));
            BOOST_CHECK(wallet.SelectCoinsMinConf(50 * COIN, 1, 6, 0, vCoins, setCoinsRet2, nValueRet, false));
            BOOST_CHECK(!equal_sets(setCoinsRet, setCoinsRet2));

            int fails = 0;
            for (int j = 0; j < RANDOM_REPEATS; j++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                BOOST_CHECK(wallet.SelectCoinsMinConf(COIN, 1, 6, 0, vCoins, setCoinsRet , nValueRet, false));
                BOOST_CHECK(wallet.SelectCoinsMinConf(COIN, 1, 6, 0, vCoins, setCoinsRet2, nValueRet, false));
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            BOOST_CHECK_NE(fails, RANDOM_REPEATS);

            // add 75 cents in small change.  not enough to make 90 cents,
            // then try making 90 cents.  there are multiple competing "smallest bigger" coins,
            // one of which should be picked at random
            add_coin(wallet, 5 * CENT);
            add_coin(wallet, 10 * CENT);
            add_coin(wallet, 15 * CENT);
            add_coin(wallet, 20 * CENT);
            add_coin(wallet, 25 * CENT);

            fails = 0;
            for (int j = 0; j < RANDOM_REPEATS; j++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                BOOST_CHECK(wallet.SelectCoinsMinConf(90*CENT, 1, 6, 0, vCoins, setCoinsRet , nValueRet, false));
                BOOST_CHECK(wallet.SelectCoinsMinConf(90*CENT, 1, 6, 0, vCoins, setCoinsRet2, nValueRet, false));
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            BOOST_CHECK_NE(fails, RANDOM_REPEATS);
        }
    }
    empty_wallet();
}

BOOST_AUTO_TEST_CASE(ApproximateBestSubset)
{
    CoinSet setCoinsRet;
    CAmount nValueRet;

    CWallet wallet;
    LOCK(wallet.cs_wallet);
    wallet.GenerateNewLegacyAccount("My account");
    empty_wallet();

    // Test vValue sort order
    for (int i = 0; i < 1000; i++)
        add_coin(wallet, 1000 * COIN);
    add_coin(wallet, 3 * COIN);

    BOOST_CHECK(wallet.SelectCoinsMinConf(1003 * COIN, 1, 6, 0, vCoins, setCoinsRet, nValueRet, false));
    BOOST_CHECK_EQUAL(nValueRet, 1003 * COIN);
    BOOST_CHECK_EQUAL(setCoinsRet.size(), 2U);

    empty_wallet();
}

BOOST_FIXTURE_TEST_CASE(rescan, TestChain100Setup)
{
    LOCK(cs_main);

    CScript scriptPubKey = GetScriptForRawPubKey(coinbaseKey.GetPubKey());
    std::shared_ptr<CReserveKeyOrScript> reservedScript = std::make_shared<CReserveKeyOrScript>(scriptPubKey);

    // Cap last block file size, and mine new block in a new block file.
    CBlockIndex* const nullBlock = nullptr;
    CBlockIndex* oldTip = chainActive.Tip();
    GetBlockFileInfo(oldTip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE;
    CreateAndProcessBlock({}, reservedScript);
    CBlockIndex* newTip = chainActive.Tip();

    // Verify ScanForWalletTransactions picks up transactions in both the old
    // and new block files.
    {
        CWallet wallet;
        LOCK(wallet.cs_wallet);
        wallet.GenerateNewLegacyAccount("My account");
        CAccount* account = wallet.getActiveAccount();
        wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey(), *account, KEYCHAIN_EXTERNAL);
        BOOST_CHECK_EQUAL(nullBlock, wallet.ScanForWalletTransactions(oldTip));
        BOOST_CHECK_EQUAL(wallet.GetImmatureBalance(), 2 * 1000 * COIN);
    }

    // Prune the older block file.
    int nFile = oldTip->GetBlockPos().nFile;
    PruneOneBlockFile(nFile);
    blockStore.UnlinkPrunedFiles({nFile});

    // Verify ScanForWalletTransactions only picks transactions in the new block
    // file.
    {
        CWallet wallet;
        LOCK(wallet.cs_wallet);
        wallet.GenerateNewLegacyAccount("My account");
        CAccount* account = wallet.getActiveAccount();
        wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey(), *account, KEYCHAIN_EXTERNAL);
        BOOST_CHECK_EQUAL(oldTip, wallet.ScanForWalletTransactions(oldTip));
        BOOST_CHECK_EQUAL(wallet.GetImmatureBalance(), 1000 * COIN);
    }

    // Verify importmulti RPC returns failure for a key whose creation time is
    // before the missing block, and success for a key whose creation time is
    // after.
    {
        CWallet wallet;
        vpwallets.insert(vpwallets.begin(), &wallet);
        UniValue keys;
        keys.setArray();
        UniValue key;
        key.setObject();
        key.pushKV("scriptPubKey", HexStr(GetScriptForRawPubKey(coinbaseKey.GetPubKey())));
        key.pushKV("timestamp", 0);
        key.pushKV("internal", UniValue(true));
        keys.push_back(key);
        key.clear();
        key.setObject();
        CKey futureKey;
        futureKey.MakeNewKey(true);
        key.pushKV("scriptPubKey", HexStr(GetScriptForRawPubKey(futureKey.GetPubKey())));
        key.pushKV("timestamp", newTip->GetBlockTimeMax() + TIMESTAMP_WINDOW + 1);
        key.pushKV("internal", UniValue(true));
        keys.push_back(key);

        // We need a legacy account where we want the keys to go.
        wallet.GenerateNewLegacyAccount("LegacyForImport");

        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back("LegacyForImport");
        request.params.push_back(keys);


        UniValue response = importmulti(request);
        BOOST_CHECK_EQUAL(response.write(), strprintf("[{\"success\":false,\"error\":{\"code\":-1,\"message\":\"Rescan failed for key with creation timestamp %d. There was an error reading a block from time %d, which is after or within %d seconds of key creation, and could contain transactions pertaining to the key. As a result, transactions and coins using this key may not appear in the wallet. This error could be caused by pruning or data corruption (see " GLOBAL_APPNAME " log for details) and could be dealt with by downloading and rescanning the relevant blocks (see -reindex and -rescan options).\"}},{\"success\":true}]", 0, oldTip->GetBlockTimeMax(), TIMESTAMP_WINDOW));
        vpwallets.erase(vpwallets.begin());
    }
}

// Verify importwallet RPC starts rescan at earliest block with timestamp
// greater or equal than key birthday. Previously there was a bug where
// importwallet RPC would start the scan at the latest block with timestamp less
// than or equal to key birthday.
BOOST_FIXTURE_TEST_CASE(importwallet_rescan, TestChain100Setup)
{
    LOCK(cs_main);

    CScript scriptPubKey = GetScriptForRawPubKey(coinbaseKey.GetPubKey());
    std::shared_ptr<CReserveKeyOrScript> reservedScript = std::make_shared<CReserveKeyOrScript>(scriptPubKey);

    // Create two blocks with same timestamp to verify that importwallet rescan
    // will pick up both blocks, not just the first.
    const int64_t BLOCK_TIME = chainActive.Tip()->GetBlockTimeMax() + 5;
    SetMockTime(BLOCK_TIME);
    coinbaseTxns.emplace_back(*CreateAndProcessBlock({}, reservedScript).vtx[0]);
    coinbaseTxns.emplace_back(*CreateAndProcessBlock({}, reservedScript).vtx[0]);

    // Set key birthday to block time increased by the timestamp window, so
    // rescan will start at the block time.
    const int64_t KEY_TIME = BLOCK_TIME + TIMESTAMP_WINDOW;
    SetMockTime(KEY_TIME);
    coinbaseTxns.emplace_back(*CreateAndProcessBlock({}, reservedScript).vtx[0]);

    // Import key into wallet and call dumpwallet to create backup file.
    {
        CWallet wallet;
        LOCK(wallet.cs_wallet);
        wallet.GenerateNewLegacyAccount("My account");
        CAccount* account = wallet.getActiveAccount();
        wallet.mapKeyMetadata[coinbaseKey.GetPubKey().GetID()].nCreateTime = KEY_TIME;
        wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey(), *account, KEYCHAIN_EXTERNAL);

        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back("wallet.backup");
        vpwallets.insert(vpwallets.begin(), &wallet);
        ::dumpwallet(request);
    }

    // Call importwallet RPC and verify all blocks with timestamps >= BLOCK_TIME
    // were scanned, and no prior blocks were scanned.
    {
        CWallet wallet;
        wallet.GenerateNewLegacyAccount("My account");

        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back("wallet.backup");
        request.params.push_back("My account");
        vpwallets[0] = &wallet;
        ::importwallet(request);

        BOOST_CHECK_EQUAL(wallet.mapWallet.size(), 3U);
        BOOST_CHECK_EQUAL(coinbaseTxns.size(), COINBASE_MATURITY+3U);
        for (size_t i = 0; i < coinbaseTxns.size(); ++i)
        {
            bool found = wallet.GetWalletTx(coinbaseTxns[i].GetHash());
            bool expected = i >= (size_t)COINBASE_MATURITY;
            BOOST_CHECK_EQUAL(found, expected);
        }
    }

    SetMockTime(0);
    vpwallets.erase(vpwallets.begin());
}

// Check that GetImmatureCredit() returns a newly calculated value instead of
// the cached value after a MarkDirty() call.
//
// This is a regression test written to verify a bugfix for the immature credit
// function. Similar tests probably should be written for the other credit and
// debit functions.
BOOST_FIXTURE_TEST_CASE(coin_mark_dirty_immature_credit, TestChain100Setup)
{
    CWallet wallet;
    CWalletTx wtx(&wallet, MakeTransactionRef(coinbaseTxns.back()));
    LOCK2(cs_main, wallet.cs_wallet);
    wtx.hashBlock = chainActive.Tip()->GetBlockHashLegacy();
    wtx.nIndex = 0;

    // Call GetImmatureCredit() once before adding the key to the wallet to
    // cache the current immature credit amount, which is 0.
    BOOST_CHECK_EQUAL(wtx.GetImmatureCredit(), 0);

    // Invalidate the cached value, add the key, and make sure a new immature
    // credit amount is calculated.
    wtx.MarkDirty();
    wallet.GenerateNewLegacyAccount("My account");
    CAccount* account = wallet.getActiveAccount();
    wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey(), *account, KEYCHAIN_EXTERNAL);
    BOOST_CHECK_EQUAL(wtx.GetImmatureCredit(), 1000*COIN);
}

static int64_t AddTx(CWallet& wallet, uint32_t lockTime, int64_t mockTime, int64_t blockTime)
{
    CMutableTransaction tx(TEST_DEFAULT_TX_VERSION);
    tx.nLockTime = lockTime;
    SetMockTime(mockTime);
    CBlockIndex* block = nullptr;
    if (blockTime > 0) {
        auto inserted = mapBlockIndex.emplace(GetRandHash(), new CBlockIndex);
        assert(inserted.second);
        const uint256& hash = inserted.first->first;
        block = inserted.first->second;
        block->nTime = blockTime;
        block->phashBlock = &hash;
    }

    CWalletTx wtx(&wallet, MakeTransactionRef(tx));
    if (block) {
        wtx.SetMerkleBranch(block, 0);
    }
    wallet.AddToWallet(wtx);
    return wallet.mapWallet.at(wtx.GetHash()).nTimeSmart;
}

// Simple test to verify assignment of CWalletTx::nTimeSmart value. Could be
// expanded to cover more corner cases of smart time logic.
BOOST_AUTO_TEST_CASE(ComputeTimeSmart)
{
    CWallet wallet;

    // New transaction should use clock time if lower than block time.
    BOOST_CHECK_EQUAL(AddTx(wallet, 1, 100, 120), 100);

    // Test that updating existing transaction does not change smart time.
    BOOST_CHECK_EQUAL(AddTx(wallet, 1, 200, 220), 100);

    // New transaction should use clock time if there's no block time.
    BOOST_CHECK_EQUAL(AddTx(wallet, 2, 300, 0), 300);

    // New transaction should use block time if lower than clock time.
    BOOST_CHECK_EQUAL(AddTx(wallet, 3, 420, 400), 400);

    // New transaction should use block time if lower than clock time and latest entry time.
    //NB! This behaviour differs from bitcoin where the latest entry time (400) would be expected to be returned instead.
    BOOST_CHECK_EQUAL(AddTx(wallet, 4, 500, 390), 390);

    // If there are future entries, new transaction should use time of the
    // newest entry that is no more than 300 seconds ahead of the clock time.
    BOOST_CHECK_EQUAL(AddTx(wallet, 5, 50, 600), 300);

    // Reset mock time for other tests.
    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(LoadReceiveRequests)
{
    CTxDestination dest = CKeyID();
    pwalletMain->AddDestData(dest, "misc", "val_misc");
    pwalletMain->AddDestData(dest, "rr0", "val_rr0");
    pwalletMain->AddDestData(dest, "rr1", "val_rr1");

    auto values = pwalletMain->GetDestValues("rr");
    BOOST_CHECK_EQUAL(values.size(), 2U);
    BOOST_CHECK_EQUAL(values[0], "val_rr0");
    BOOST_CHECK_EQUAL(values[1], "val_rr1");
}

class ListCoinsTestingSetup : public TestChain100Setup
{
public:
    ListCoinsTestingSetup()
    {
        CScript scriptPubKey = GetScriptForRawPubKey(coinbaseKey.GetPubKey());
        std::shared_ptr<CReserveKeyOrScript> reservedScript = std::make_shared<CReserveKeyOrScript>(scriptPubKey);
        CreateAndProcessBlock({}, reservedScript);
        ::bitdb.MakeMock();
        wallet.reset(new CWallet(std::unique_ptr<CWalletDBWrapper>(new CWalletDBWrapper(&bitdb, "wallet_test.dat"))));
        WalletLoadState firstRun;
        wallet->LoadWallet(firstRun);
        LOCK(wallet->cs_wallet);
        wallet->GenerateNewLegacyAccount("My account");
        CAccount* account = wallet->getActiveAccount();
        wallet->AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey(), *account, KEYCHAIN_EXTERNAL);
        wallet->ScanForWalletTransactions(chainActive.Genesis());
    }

    ~ListCoinsTestingSetup()
    {
        wallet.reset();
        ::bitdb.Flush(true);
        ::bitdb.Reset();
    }

    CWalletTx& AddTx(CRecipient recipient)
    {
        CScript scriptPubKey = GetScriptForRawPubKey(coinbaseKey.GetPubKey());
        std::shared_ptr<CReserveKeyOrScript> reservedScript = std::make_shared<CReserveKeyOrScript>(scriptPubKey);

        CWalletTx wtx;
        CAccount* account = wallet->getActiveAccount();
        CReserveKeyOrScript reservekey(wallet.get(), account, KEYCHAIN_CHANGE);
        CAmount fee;
        int changePos = -1;
        std::string error;
        BOOST_CHECK(wallet->CreateTransaction(account, {recipient}, wtx, reservekey, fee, changePos, error));
        CValidationState state;
        BOOST_CHECK(wallet->CommitTransaction(wtx, reservekey, nullptr, state));
        auto it = wallet->mapWallet.find(wtx.GetHash());
        BOOST_CHECK(it != wallet->mapWallet.end());
        CreateAndProcessBlock({CMutableTransaction(*it->second.tx)}, reservedScript);
        it->second.SetMerkleBranch(chainActive.Tip(), 1);
        return it->second;
    }

    std::unique_ptr<CWallet> wallet;
};

BOOST_FIXTURE_TEST_CASE(ListCoins, ListCoinsTestingSetup)
{
    std::string coinbaseAddress = coinbaseKey.GetPubKey().GetID().ToString();
    LOCK(wallet->cs_wallet);
    CAccount* account = wallet->getActiveAccount();

    // Confirm ListCoins initially returns 1 coin grouped under coinbaseKey
    // address.
    auto list = wallet->ListCoins(account);
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 1U);

    // Check initial balance from first mature coinbase transaction.
    BOOST_CHECK_EQUAL(17000000000000000, wallet->GetAvailableBalance(account));

    // Add a transaction creating a change address, and confirm ListCoins still
    // returns the coin associated with the change address underneath the
    // coinbaseKey pubkey, even though the change address has a different
    // pubkey.
    AddTx(CRecipient{GetScriptForRawPubKey({}), 1 * COIN, false /* subtract fee */});
    list = wallet->ListCoins(account);
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 2U);

    // Lock both coins. Confirm number of available coins drops to 0.
    std::vector<COutput> available;
    wallet->AvailableCoins(account, available);
    BOOST_CHECK_EQUAL(available.size(), 2U);
    for (const auto& group : list) {
        for (const auto& coin : group.second) {
            wallet->LockCoin(COutPoint(coin.tx->GetHash(), coin.i));
        }
    }
    wallet->AvailableCoins(account, available);
    BOOST_CHECK_EQUAL(available.size(), 0U);

    // Confirm ListCoins still returns same result as before, despite coins
    // being locked.
    list = wallet->ListCoins(account);
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 2U);
}

BOOST_AUTO_TEST_CASE(TestMnemonics)
{
    std::vector<std::pair<SecureString, SecureString> > testVector;

    testVector.push_back(std::pair("00000000000000000000000000000000", "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"));
    testVector.push_back(std::pair("7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f", "legal winner thank year wave sausage worth useful legal winner thank yellow"));
    testVector.push_back(std::pair("80808080808080808080808080808080", "letter advice cage absurd amount doctor acoustic avoid letter advice cage above"));
    testVector.push_back(std::pair("ffffffffffffffffffffffffffffffff", "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong"));
    testVector.push_back(std::pair("000000000000000000000000000000000000000000000000", "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon agent"));
    testVector.push_back(std::pair("7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f", "legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal will"));
    testVector.push_back(std::pair("808080808080808080808080808080808080808080808080", "letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic avoid letter always"));
    testVector.push_back(std::pair("ffffffffffffffffffffffffffffffffffffffffffffffff", "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo when"));
    testVector.push_back(std::pair("0000000000000000000000000000000000000000000000000000000000000000", "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art"));
    testVector.push_back(std::pair("7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f", "legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth useful legal winner thank year wave sausage worth title"));
    testVector.push_back(std::pair("8080808080808080808080808080808080808080808080808080808080808080",  "letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic bless"));
    testVector.push_back(std::pair("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",  "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo vote"));
    testVector.push_back(std::pair("9e885d952ad362caeb4efe34a8e91bd2",  "ozone drill grab fiber curtain grace pudding thank cruise elder eight picnic"));
    testVector.push_back(std::pair("6610b25967cdcca9d59875f5cb50b0ea75433311869e930b",  "gravity machine north sort system female filter attitude volume fold club stay feature office ecology stable narrow fog"));
    testVector.push_back(std::pair("68a79eaca2324873eacc50cb9c6eca8cc68ea5d936f98787c60c7ebc74e6ce7c",  "hamster diagram private dutch cause delay private meat slide toddler razor book happy fancy gospel tennis maple dilemma loan word shrug inflict delay length"));
    testVector.push_back(std::pair("c0ba5a8e914111210f2bd131f3d5e08d", "scheme spot photo card baby mountain device kick cradle pact join borrow"));
    testVector.push_back(std::pair("6d9be1ee6ebd27a258115aad99b7317b9c8d28b6d76431c3", "horn tenant knee talent sponsor spell gate clip pulse soap slush warm silver nephew swap uncle crack brave"));
    testVector.push_back(std::pair("9f6a2878b2520799a44ef18bc7df394e7061a224d2c33cd015b157d746869863",  "panda eyebrow bullet gorilla call smoke muffin taste mesh discover soft ostrich alcohol speed nation flash devote level hobby quick inner drive ghost inside"));
    testVector.push_back(std::pair("23db8160a31d3e0dca3688ed941adbf3",  "cat swing flag economy stadium alone churn speed unique patch report train"));
    testVector.push_back(std::pair("8197a4a47f0425faeaa69deebc05ca29c0a5b5cc76ceacc0",  "light rule cinnamon wrap drastic word pride squirrel upgrade then income fatal apart sustain crack supply proud access"));
    testVector.push_back(std::pair("066dca1a2bb7e8a1db2832148ce9933eea0f3ac9548d793112d9a95c9407efad",  "all hour make first leader extend hole alien behind guard gospel lava path output census museum junior mass reopen famous sing advance salt reform"));
    testVector.push_back(std::pair("f30f8c1da665478f49b001d94c5fc452", "vessel ladder alter error federal sibling chat ability sun glass valve picture"));
    testVector.push_back(std::pair("c10ec20dc3cd9f652c7fac2f1230f7a3c828389a14392f05",  "scissors invite lock maple supreme raw rapid void congress muscle digital elegant little brisk hair mango congress clump"));
    testVector.push_back(std::pair("f585c11aec520db57dd353c69554b21a89b20fb0650966fa0a9d6f74fd989d8f",  "void come effort suffer camp survey warrior heavy shoot primary clutch crush open amazing screen patrol group space point ten exist slush involve unfold"));

    for(auto test : testVector)
    {
        std::vector<unsigned char> data = ParseHex(test.first.c_str());
        SecureString comp = mnemonicFromEntropy(data, data.size()*8);
        BOOST_CHECK_EQUAL(comp, test.second);
        std::vector<unsigned char> datacomp = entropyFromMnemonic(test.second);
        BOOST_CHECK_EQUAL(SecureString(HexStr(datacomp.begin(),datacomp.end()).c_str()), test.first);
    }
}

BOOST_AUTO_TEST_SUITE_END()
