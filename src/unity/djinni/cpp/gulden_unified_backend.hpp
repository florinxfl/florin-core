// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GuldenMonitorListener;
class GuldenUnifiedFrontend;
enum class LegacyWalletResult;
enum class PaymentResultStatus;
struct AddressRecord;
struct BlockInfoRecord;
struct MonitorRecord;
struct MutationRecord;
struct PeerRecord;
struct QrCodeRecord;
struct TransactionRecord;
struct UriRecipient;
struct UriRecord;

/** This interface will be implemented in C++ and can be called from any language. */
class GuldenUnifiedBackend {
public:
    virtual ~GuldenUnifiedBackend() {}

    /** Interface constants */
    static constexpr int32_t VERSION = 1;

    /** Get the build information (ie. commit id and status) */
    static std::string BuildInfo();

    /**
     * Start the library
     * extraArgs - any additional commandline arguments as passed to GuldenD
     * NB!!! This call blocks until the library is terminated, it is the callers responsibility to place it inside a thread or similar.
     * If you are in an environment where this is not possible (node.js for example use InitUnityLibThreaded instead which places it in a thread on your behalf)
     */
    static int32_t InitUnityLib(const std::string & data_dir, const std::string & staticFilterPath, int64_t staticFilterOffset, int64_t staticFilterLength, bool testnet, bool spvMode, const std::shared_ptr<GuldenUnifiedFrontend> & signalHandler, const std::string & extraArgs);

    /** Threaded implementation of InitUnityLib */
    static void InitUnityLibThreaded(const std::string & data_dir, const std::string & staticFilterPath, int64_t staticFilterOffset, int64_t staticFilterLength, bool testnet, bool spvMode, const std::shared_ptr<GuldenUnifiedFrontend> & signalHandler, const std::string & extraArgs);

    /** Create the wallet - this should only be called after receiving a `notifyInit...` signal from InitUnityLib */
    static bool InitWalletFromRecoveryPhrase(const std::string & phrase, const std::string & password);

    /** Continue creating wallet that was previously erased using EraseWalletSeedsAndAccounts */
    static bool ContinueWalletFromRecoveryPhrase(const std::string & phrase, const std::string & password);

    /** Create the wallet - this should only be called after receiving a `notifyInit...` signal from InitUnityLib */
    static bool InitWalletLinkedFromURI(const std::string & linked_uri, const std::string & password);

    /** Continue creating wallet that was previously erased using EraseWalletSeedsAndAccounts */
    static bool ContinueWalletLinkedFromURI(const std::string & linked_uri, const std::string & password);

    /** Create the wallet - this should only be called after receiving a `notifyInit...` signal from InitUnityLib */
    static bool InitWalletFromAndroidLegacyProtoWallet(const std::string & wallet_file, const std::string & old_password, const std::string & new_password);

    /** Check if a file is a valid legacy proto wallet */
    static LegacyWalletResult isValidAndroidLegacyProtoWallet(const std::string & wallet_file, const std::string & old_password);

    /** Check link URI for validity */
    static bool IsValidLinkURI(const std::string & phrase);

    /** Replace the existing wallet accounts with a new one from a linked URI - only after first emptying the wallet. */
    static bool ReplaceWalletLinkedFromURI(const std::string & linked_uri, const std::string & password);

    /**
     * Erase the seeds and accounts of a wallet leaving an empty wallet (with things like the address book intact)
     * After calling this it will be necessary to create a new linked account or recovery phrase account again.
     * NB! This will empty a wallet regardless of whether it has funds in it or not and makes no provisions to check for this - it is the callers responsibility to ensure that erasing the wallet is safe to do in this regard.
     */
    static bool EraseWalletSeedsAndAccounts();

    /**
     * Check recovery phrase for (syntactic) validity
     * Considered valid if the contained mnemonic is valid and the birth-number is either absent or passes Base-10 checksum
     */
    static bool IsValidRecoveryPhrase(const std::string & phrase);

    /** Generate a new recovery mnemonic */
    static std::string GenerateRecoveryMnemonic();

    static std::string GenerateGenesisKeys();

    /** Compute recovery phrase with birth number */
    static std::string ComposeRecoveryPhrase(const std::string & mnemonic, int64_t birthTime);

    /** Stop the library */
    static void TerminateUnityLib();

    /** Generate a QR code for a string, QR code will be as close to width_hint as possible when applying simple scaling. */
    static QrCodeRecord QRImageFromString(const std::string & qr_string, int32_t width_hint);

    /** Get a receive address from the wallet */
    static std::string GetReceiveAddress();

    /** Get the recovery phrase for the wallet */
    static std::string GetRecoveryPhrase();

    /** Check if the wallet is using a mnemonic seed ie. recovery phrase (else it is a linked wallet) */
    static bool IsMnemonicWallet();

    /** Check if the phrase mnemonic is a correct one for the wallet (phrase can be with or without birth time) */
    static bool IsMnemonicCorrect(const std::string & phrase);

    /** Unlock wallet */
    static bool UnlockWallet(const std::string & password);

    /** Forcefully lock wallet again */
    static bool LockWallet();

    /** Change the waller password */
    static bool ChangePassword(const std::string & oldPassword, const std::string & newPassword);

    /** Check if the wallet has any transactions that are still pending confirmation, to be used to determine if e.g. it is safe to perform a link or whether we should wait. */
    static bool HaveUnconfirmedFunds();

    /** Check current wallet balance (including unconfirmed funds) */
    static int64_t GetBalance();

    /** Rescan blockchain for wallet transactions */
    static void DoRescan();

    /** Check if text/address is something we are capable of sending money too */
    static UriRecipient IsValidRecipient(const UriRecord & request);

    /** Compute the fee required to send amount to given recipient */
    static int64_t feeForRecipient(const UriRecipient & request);

    /** Attempt to pay a recipient, will throw on failure with description */
    static PaymentResultStatus performPaymentToRecipient(const UriRecipient & request, bool substract_fee);

    /** Get list of all transactions wallet has been involved in */
    static std::vector<TransactionRecord> getTransactionHistory();

    /**
     * Get the wallet transaction for the hash
     * Will throw if not found
     */
    static TransactionRecord getTransaction(const std::string & txHash);

    /** Get list of wallet mutations */
    static std::vector<MutationRecord> getMutationHistory();

    /** Get list of all address book entries */
    static std::vector<AddressRecord> getAddressBookRecords();

    /** Add a record to the address book */
    static void addAddressBookRecord(const AddressRecord & address);

    /** Delete a record from the address book */
    static void deleteAddressBookRecord(const AddressRecord & address);

    /** Interim persist and prune of state. Use at key moments like app backgrounding. */
    static void PersistAndPruneForSPV();

    /**
     * Reset progress notification. In cases where there has been no progress for a long time, but the process
     * is still running the progress can be reset and will represent work to be done from this reset onwards.
     * For example when the process is in the background on iOS for a long long time (but has not been terminated
     * by the OS) this might make more sense then to continue the progress from where it was a day or more ago.
     */
    static void ResetUnifiedProgress();

    /** Get connected peer info */
    static std::vector<PeerRecord> getPeers();

    /** Get info of last blocks (at most 32) in SPV chain */
    static std::vector<BlockInfoRecord> getLastSPVBlockInfos();

    static float getUnifiedProgress();

    static MonitorRecord getMonitoringStats();

    static void RegisterMonitorListener(const std::shared_ptr<GuldenMonitorListener> & listener);

    static void UnregisterMonitorListener(const std::shared_ptr<GuldenMonitorListener> & listener);
};
