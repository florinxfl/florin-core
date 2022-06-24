// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni

/**
 * The library controller is used to Init/Terminate the library, and other similar tasks.
 * It is also home to various generic utility functions that don't (yet) have a place in more specific controllers
 * Specific functionality should go in specific controllers; account related functionality -> accounts_controller, network related functionality -> network_controller and so on
 */
declare class NJSILibraryController
{
    /** Get the build information (ie. commit id and status) */
    static declare function BuildInfo(): string;
    /**
     * Start the library
     * extraArgs - any additional commandline arguments as could normally be passed to the daemon binary
     * NB!!! This call blocks until the library is terminated, it is the callers responsibility to place it inside a thread or similar.
     * If you are in an environment where this is not possible (node.js for example use InitUnityLibThreaded instead which places it in a thread on your behalf)
     */
    static declare function InitUnityLib(data_dir: string, staticFilterPath: string, staticFilterOffset: number, staticFilterLength: number, testnet: boolean, spvMode: boolean, signalHandler: NJSILibraryListener, extraArgs: string): number;
    /** Threaded implementation of InitUnityLib */
    static declare function InitUnityLibThreaded(data_dir: string, staticFilterPath: string, staticFilterOffset: number, staticFilterLength: number, testnet: boolean, spvMode: boolean, signalHandler: NJSILibraryListener, extraArgs: string);
    /** Create the wallet - this should only be called after receiving a `notifyInit...` signal from InitUnityLib */
    static declare function InitWalletFromRecoveryPhrase(phrase: string, password: string): boolean;
    /** Continue creating wallet that was previously erased using EraseWalletSeedsAndAccounts */
    static declare function ContinueWalletFromRecoveryPhrase(phrase: string, password: string): boolean;
    /** Create the wallet - this should only be called after receiving a `notifyInit...` signal from InitUnityLib */
    static declare function InitWalletLinkedFromURI(linked_uri: string, password: string): boolean;
    /** Continue creating wallet that was previously erased using EraseWalletSeedsAndAccounts */
    static declare function ContinueWalletLinkedFromURI(linked_uri: string, password: string): boolean;
    /** Create the wallet - this should only be called after receiving a `notifyInit...` signal from InitUnityLib */
    static declare function InitWalletFromAndroidLegacyProtoWallet(wallet_file: string, old_password: string, new_password: string): boolean;
    /** Check if a file is a valid legacy proto wallet */
    static declare function isValidAndroidLegacyProtoWallet(wallet_file: string, old_password: string): LegacyWalletResult;
    /** Check link URI for validity */
    static declare function IsValidLinkURI(phrase: string): boolean;
    /** Replace the existing wallet accounts with a new one from a linked URI - only after first emptying the wallet. */
    static declare function ReplaceWalletLinkedFromURI(linked_uri: string, password: string): boolean;
    /**
     * Erase the seeds and accounts of a wallet leaving an empty wallet (with things like the address book intact)
     * After calling this it will be necessary to create a new linked account or recovery phrase account again.
     * NB! This will empty a wallet regardless of whether it has funds in it or not and makes no provisions to check for this - it is the callers responsibility to ensure that erasing the wallet is safe to do in this regard.
     */
    static declare function EraseWalletSeedsAndAccounts(): boolean;
    /**
     * Check recovery phrase for (syntactic) validity
     * Considered valid if the contained mnemonic is valid and the birth-number is either absent or passes Base-10 checksum
     */
    static declare function IsValidRecoveryPhrase(phrase: string): boolean;
    /** Generate a new recovery mnemonic */
    static declare function GenerateRecoveryMnemonic(): MnemonicRecord;
    static declare function GenerateGenesisKeys(): string;
    /** Compute recovery phrase with birth number */
    static declare function ComposeRecoveryPhrase(mnemonic: string, birthTime: number): MnemonicRecord;
    /** Stop the library */
    static declare function TerminateUnityLib();
    /** Generate a QR code for a string, QR code will be as close to width_hint as possible when applying simple scaling. */
    static declare function QRImageFromString(qr_string: string, width_hint: number): QrCodeRecord;
    /** Get a receive address for the active account */
    static declare function GetReceiveAddress(): string;
    /** Get the recovery phrase for the wallet */
    static declare function GetRecoveryPhrase(): MnemonicRecord;
    /** Check if the wallet is using a mnemonic seed ie. recovery phrase (else it is a linked wallet) */
    static declare function IsMnemonicWallet(): boolean;
    /** Check if the phrase mnemonic is a correct one for the wallet (phrase can be with or without birth time) */
    static declare function IsMnemonicCorrect(phrase: string): boolean;
    /**
     * Get the 'dictionary' of valid words that a recovery phrase can be composed of
     * NB! Not all combinations of these words are valid
     * Do not use this to generate/compose your own phrases - always use 'GenerateRecoveryMnemonic' for this
     * This function should only be used for input validation/auto-completion
     */
    static declare function GetMnemonicDictionary(): Array<string>;
    /** Unlock wallet; wallet will automatically relock after "timeout_in_seconds" */
    static declare function UnlockWallet(password: string, timeout_in_seconds: number): boolean;
    /** Forcefully lock wallet again */
    static declare function LockWallet(): boolean;
    static declare function GetWalletLockStatus(): WalletLockStatus;
    /** Change the wallet password */
    static declare function ChangePassword(oldPassword: string, newPassword: string): boolean;
    /** Rescan blockchain for wallet transactions */
    static declare function DoRescan();
    /** Check if text/address is something we are capable of sending money too */
    static declare function IsValidRecipient(request: UriRecord): UriRecipient;
    /** Check if text/address is a native (to our blockchain) address */
    static declare function IsValidNativeAddress(address: string): boolean;
    /** Check if text/address is a valid bitcoin address */
    static declare function IsValidBitcoinAddress(address: string): boolean;
    /** Compute the fee required to send amount to given recipient */
    static declare function feeForRecipient(request: UriRecipient): number;
    /** Attempt to pay a recipient, will throw on failure with description */
    static declare function performPaymentToRecipient(request: UriRecipient, substract_fee: boolean): PaymentResultStatus;
    /**
     * Get the wallet transaction for the hash
     * Will throw if not found
     */
    static declare function getTransaction(txHash: string): TransactionRecord;
    /** resubmit a transaction to the network, returns the raw hex of the transaction as a string or empty on fail */
    static declare function resendTransaction(txHash: string): string;
    /** Get list of all address book entries */
    static declare function getAddressBookRecords(): Array<AddressRecord>;
    /** Add a record to the address book */
    static declare function addAddressBookRecord(address: AddressRecord);
    /** Delete a record from the address book */
    static declare function deleteAddressBookRecord(address: AddressRecord);
    /** Interim persist and prune of state. Use at key moments like app backgrounding. */
    static declare function PersistAndPruneForSPV();
    /**
     * Reset progress notification. In cases where there has been no progress for a long time, but the process
     * is still running the progress can be reset and will represent work to be done from this reset onwards.
     * For example when the process is in the background on iOS for a long long time (but has not been terminated
     * by the OS) this might make more sense then to continue the progress from where it was a day or more ago.
     */
    static declare function ResetUnifiedProgress();
    /** Get info of last blocks (at most 32) in SPV chain */
    static declare function getLastSPVBlockInfos(): Array<BlockInfoRecord>;
    static declare function getUnifiedProgress(): number;
    static declare function getMonitoringStats(): MonitorRecord;
    static declare function RegisterMonitorListener(listener: NJSMonitorListener);
    static declare function UnregisterMonitorListener(listener: NJSMonitorListener);
    static declare function getClientInfo(): Map<string, string>;
    /**
     * Get list of wallet mutations
     *NB! This is SPV specific, non SPV wallets should use account specific getMutationHistory on an accounts controller instead
     */
    static declare function getMutationHistory(): Array<MutationRecord>;
    /**
     * Get list of all transactions wallet has been involved in
     *NB! This is SPV specific, non SPV wallets should use account specific getTransactionHistory on an accounts controller instead
     */
    static declare function getTransactionHistory(): Array<TransactionRecord>;
    /**
     * Check if the wallet has any transactions that are still pending confirmation, to be used to determine if e.g. it is safe to perform a link or whether we should wait.
     *NB! This is SPV specific, non SPV wallets should use HaveUnconfirmedFunds on wallet controller instead
     */
    static declare function HaveUnconfirmedFunds(): boolean;
    /**
     * Check current wallet balance (including unconfirmed funds)
     *NB! This is SPV specific, non SPV wallets should use GetBalance on wallet controller instead
     */
    static declare function GetBalance(): number;
}
/**
 * Controller to perform functions at a wallet level (e.g. get balance of the entire wallet)
 * For per account functionality see accounts_controller
 */
declare class NJSIWalletController
{
    /** Set listener to be notified of wallet events */
    static declare function setListener(networklistener: NJSIWalletListener);
    /** Check if the wallet has any transactions that are still pending confirmation, to be used to determine if e.g. it is safe to perform a link or whether we should wait. */
    static declare function HaveUnconfirmedFunds(): boolean;
    /** Check current wallet balance, as a single simple number that includes confirmed/unconfirmed/immature funds */
    static declare function GetBalanceSimple(): number;
    /** Check current wallet balance */
    static declare function GetBalance(): BalanceRecord;
    /** Abandon a transaction */
    static declare function AbandonTransaction(txHash: string): boolean;
    /** Get a unique UUID that identifies this wallet */
    static declare function GetUUID(): string;
}
/** Interface to receive wallet level events */
declare class NJSIWalletListener
{
    /** Notification of change in overall wallet balance */
    declare function notifyBalanceChange(new_balance: BalanceRecord);
    /**
     * Notification of new mutations.
     * If self_committed it is due to a call to performPaymentToRecipient, else it is because of a transaction
     * reached us in another way. In general this will be because we received funds from someone, hower there are
     * also cases where funds is send from our wallet while !self_committed (for example by a linked desktop wallet
     * or another wallet instance using the same keys as ours).
     *
     * Note that no notifyNewMutation events will fire until after 'notifySyncDone'
     * Therefore it is necessary to first fetch the full mutation history before starting to listen for this event.
     */
    declare function notifyNewMutation(mutation: MutationRecord, self_committed: boolean);
    /**
     * Notification that an existing transaction/mutation  has updated
     *
     * Note that no notifyUpdatedTransaction events will fire until after 'notifySyncDone'
     * Therefore it is necessary to first fetch the full mutation history before starting to listen for this event.
     */
    declare function notifyUpdatedTransaction(transaction: TransactionRecord);
    /** Wallet unlocked */
    declare function notifyWalletUnlocked();
    /** Wallet locked */
    declare function notifyWalletLocked();
    /** Core wants the wallet to unlock; UI should respond to this by calling 'UnlockWallet' */
    declare function notifyCoreWantsUnlock(reason: string);
    /** Core wants display info to the user, type can be one of "MSG_ERROR", "MSG_WARNING", "MSG_INFORMATION"; caption is the suggested caption and message the suggested message to display */
    declare function notifyCoreInfo(type: string, caption: string, message: string);
}
/** Monitoring events */
declare class NJSMonitorListener
{
    declare function onPartialChain(height: number, probable_height: number, offset: number);
    declare function onPruned(height: number);
    declare function onProcessedSPVBlocks(height: number);
}
/** Interface to receive events from the core */
declare class NJSILibraryListener
{
    /**
     * Fraction of work done since session start or last progress reset [0..1]
     * Unified progress combines connection state, header and block sync
     */
    declare function notifyUnifiedProgress(progress: number);
    /** Called once when 'notifyUnifiedProgress' reaches '1' for first time after session start */
    declare function notifySyncDone();
    declare function notifyBalanceChange(new_balance: BalanceRecord);
    /**
     * Notification of new mutations
     * If self_committed it is due to a call to performPaymentToRecipient, else it is because of a transaction
     * reached us in another way. In general this will be because we received funds from someone, hower there are
     * also cases where funds is send from our wallet while !self_committed (for example by a linked desktop wallet
     * or another wallet instance using the same keys as ours).
     *
     * Note that no notifyNewMutation events will fire until after 'notifySyncDone'
     * Therefore it is necessary to first fetch the full mutation history before starting to listen for this event.
     */
    declare function notifyNewMutation(mutation: MutationRecord, self_committed: boolean);
    /**
     * Notification that an existing transaction/mutation  has updated
     *
     * Note that no notifyUpdatedTransaction events will fire until after 'notifySyncDone'
     * Therefore it is necessary to first fetch the full mutation history before starting to listen for this event.
     */
    declare function notifyUpdatedTransaction(transaction: TransactionRecord);
    declare function notifyInitWithExistingWallet();
    declare function notifyInitWithoutExistingWallet();
    declare function notifyShutdown();
    declare function notifyCoreReady();
    declare function logPrint(str: string);
}
/** C++ interface to execute RPC commands */
declare class NJSIRpcController
{
    static declare function execute(rpcCommandLine: string, resultListener: NJSIRpcListener);
    static declare function getAutocompleteList(): Array<string>;
}
/**
 * Interface to handle result of RPC commands
 * Calls either onSuccess or onError depending on whether command suceedes or fails
 */
declare class NJSIRpcListener
{
    /**
     * Returns a filtered version of the command with sensitive information like passwords removed
     * Any kind of 'command history' functionality should store this filtered command and not the original command
     */
    declare function onFilteredCommand(filteredCommand: string);
    /**
     * Returns the result and a filtered version of the command with sensitive information like passwords removed
     * Any kind of 'command history' functionality should store this filtered command and not the original command
     */
    declare function onSuccess(filteredCommand: string, result: string);
    /**
     * Returns an error message which might be a plain string or JSON depending on the type of error
     * Also returns a filtered version of the command with sensitive information like passwords removed
     * Any kind of 'command history' functionality should store this filtered command and not the original command
     */
    declare function onError(filteredCommand: string, errorMessage: string);
}
/** C++ interface to control networking related aspects of the software */
declare class NJSIP2pNetworkController
{
    /** Register listener to be notified of networking events */
    static declare function setListener(networklistener: NJSIP2pNetworkListener);
    /** Turn p2p networking off */
    static declare function disableNetwork();
    /** Turn p2p networking on */
    static declare function enableNetwork();
    /** Get connected peer info */
    static declare function getPeerInfo(): Array<PeerRecord>;
    /** Get all banned peers */
    static declare function listBannedPeers(): Array<BannedPeerRecord>;
    static declare function banPeer(address: string, banTimeInSeconds: number): boolean;
    /** Unban a single peer */
    static declare function unbanPeer(address: string): boolean;
    /** Disconnect a specific peer */
    static declare function disconnectPeer(nodeid: number): boolean;
    /** Clear all banned peers */
    static declare function ClearBanned(): boolean;
}
/** Interface to receive updates about network status */
declare class NJSIP2pNetworkListener
{
    /** Notify that p2p networking has been enabled */
    declare function onNetworkEnabled();
    /** Notify that p2p networking has been disabled */
    declare function onNetworkDisabled();
    /** Notify that number of peers has changed */
    declare function onConnectionCountChanged(numConnections: number);
    /** Notify that amount of data sent/received has changed */
    declare function onBytesChanged(totalRecv: number, totalSent: number);
}
/** C++ interface to control accounts */
declare class NJSIAccountsController
{
    /** Register listener to be notified of account related events */
    static declare function setListener(accountslistener: NJSIAccountsListener);
    /** List all currently visible accounts in the wallet */
    static declare function listAccounts(): Array<AccountRecord>;
    /** Set the currently active account */
    static declare function setActiveAccount(accountUUID: string): boolean;
    /** Get the currently active account */
    static declare function getActiveAccount(): string;
    /** Create an account, possible types are (HD/Mobile/Witness/Mining/Legacy). Returns the UUID of the new account */
    static declare function createAccount(accountName: string, accountType: string): string;
    /** Check name of account */
    static declare function getAccountName(accountUUID: string): string;
    /** Rename an account */
    static declare function renameAccount(accountUUID: string, newAccountName: string): boolean;
    /** Delete an account, account remains available in background but is hidden from user */
    static declare function deleteAccount(accountUUID: string): boolean;
    /**
     * Purge an account, account is permenently removed from wallet (but may still reappear in some instances if it is an HD account and user recovers from phrase in future)
     * If it is a Legacy or imported witness key or similar account then it will be gone forever
     * Generally prefer 'deleteAccount' and use this with caution
     */
    static declare function purgeAccount(accountUUID: string): boolean;
    /** Get a URI that will enable 'linking' of this account in another wallet (for e.g. mobile wallet linking) for an account. Empty on failiure.  */
    static declare function getAccountLinkURI(accountUUID: string): string;
    /** Get a URI that will enable creation of a "witness only" account in another wallet that can witness on behalf of this account */
    static declare function getWitnessKeyURI(accountUUID: string): string;
    /**
     * Create a new "witness-only" account from a previously exported URI
     * Returns UUID on success, empty string on failiure
     */
    static declare function createAccountFromWitnessKeyURI(witnessKeyURI: string, newAccountName: string): string;
    /** Get a receive address for account */
    static declare function getReceiveAddress(accountUUID: string): string;
    /** Get list of all transactions account has been involved in */
    static declare function getTransactionHistory(accountUUID: string): Array<TransactionRecord>;
    /** Get list of mutations for account */
    static declare function getMutationHistory(accountUUID: string): Array<MutationRecord>;
    /** Check balance for active account */
    static declare function getActiveAccountBalance(): BalanceRecord;
    /** Check balance for account */
    static declare function getAccountBalance(accountUUID: string): BalanceRecord;
    /** Check balance for all accounts, returns a map of account_uuid->balance_record */
    static declare function getAllAccountBalances(): Map<string, BalanceRecord>;
    /**Register with wallet that this account has been "linked" with an external service (e.g. to host holding key) */
    static declare function addAccountLink(accountUUID: string, serviceName: string, data: string): boolean;
    /**Register with wallet to remove an existing link */
    static declare function removeAccountLink(accountUUID: string, serviceName: string): boolean;
    /**List all active account links that we have previously registered */
    static declare function listAccountLinks(accountUUID: string): Array<AccountLinkRecord>;
}
/** Interface to receive updates about accounts */
declare class NJSIAccountsListener
{
    /** Notify that the active account has changed */
    declare function onActiveAccountChanged(accountUUID: string);
    /** Notify that the active account name has changed */
    declare function onActiveAccountNameChanged(newAccountName: string);
    /** Notify that an account name has changed */
    declare function onAccountNameChanged(accountUUID: string, newAccountName: string);
    /** Notify that a new account has been added */
    declare function onAccountAdded(accountUUID: string, accountName: string);
    /** Notify that an account has been deleted */
    declare function onAccountDeleted(accountUUID: string);
    /** Notify that an account has been modified */
    declare function onAccountModified(accountUUID: string, accountData: AccountRecord);
}
/** C++ interface to control witness accounts */
declare class NJSIWitnessController
{
    /** Get information on min/max witness periods, weights etc. */
    static declare function getNetworkLimits(): Map<string, string>;
    /** Get an estimate of weights/parts that a witness account will be funded with */
    static declare function getEstimatedWeight(amount_to_lock: number, lock_period_in_blocks: number): WitnessEstimateInfoRecord;
    /** Fund a witness account */
    static declare function fundWitnessAccount(funding_account_UUID: string, witness_account_UUID: string, funding_amount: number, requestedLockPeriodInBlocks: number): WitnessFundingResultRecord;
    /** Renew a witness account */
    static declare function renewWitnessAccount(funding_account_UUID: string, witness_account_UUID: string): WitnessFundingResultRecord;
    /** Get information on account weight and other witness statistics for account */
    static declare function getAccountWitnessStatistics(witnessAccountUUID: string): WitnessAccountStatisticsRecord;
    /** Turn compounding on/off */
    static declare function setAccountCompounding(witnessAccountUUID: string, percent_to_compount: number);
    /** Check state of compounding; returns a percentage between 1 and 100, or 0 if not compounding */
    static declare function isAccountCompounding(witnessAccountUUID: string): number;
    /** Get the witness address of the account */
    static declare function getWitnessAddress(witnessAccountUUID: string): string;
}
/** C++ interface to control generation of blocks (proof of work) */
declare class NJSIGenerationController
{
    /** Register listener to be notified of generation related events */
    static declare function setListener(generationListener: NJSIGenerationListener);
    /**
     * Activate block generation (proof of work)
     * Number of threads should not exceed physical threads, memory limit is a string specifier in the form of #B/#K/#M/#G (e.g. 102400B, 10240K, 1024M, 1G)
     */
    static declare function startGeneration(numThreads: number, numArenaThreads: number, memoryLimit: string): boolean;
    /** Stop any active block generation (proof of work) */
    static declare function stopGeneration(): boolean;
    /**
     * Get the address of the account that is used for generation by default. Empty on failiure
     * Note that this isn't necessarily the actual generation address as there might be an override
     * See: getGenerationOverrideAddress
     */
    static declare function getGenerationAddress(): string;
    /**
     * Get the 'override' address for generation, if one has been set
     * The override address, when present it used for all block generation in place of the default account address
     */
    static declare function getGenerationOverrideAddress(): string;
    /** Set an override address to use for block generation in place of the default */
    static declare function setGenerationOverrideAddress(overrideAddress: string): boolean;
    static declare function getAvailableCores(): number;
    static declare function getMinimumMemory(): number;
    static declare function getMaximumMemory(): number;
}
/** Interface to receive updates about block generation */
declare class NJSIGenerationListener
{
    /** Signal that block generation has started */
    declare function onGenerationStarted();
    /** Signal that block generation has stopped */
    declare function onGenerationStopped();
    /** Periodically signal latest block generation statistics */
    declare function onStatsUpdated(hashesPerSecond: number, hashesPerSecondUnit: string, rollingHashesPerSecond: number, rollingHashesPerSecondUnit: string, bestHashesPerSecond: number, bestHashesPerSecondUnit: string, arenaSetupTime: number);
}
