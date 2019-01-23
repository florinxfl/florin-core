// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#import "DBBalanceRecord.h"
#import "DBTransactionRecord.h"
#import <Foundation/Foundation.h>


/** This interface will be implemented in Java and ObjC and can be called from C++. */
@protocol DBGuldenUnifiedFrontend

/**
 * Fraction of work done since session start or last progress reset [0..1]
 * Unified progress combines connection state, header and block sync
 */
- (void)notifyUnifiedProgress:(float)progress;

- (BOOL)notifyBalanceChange:(nonnull DBBalanceRecord *)newBalance;

- (BOOL)notifyNewTransaction:(nonnull DBTransactionRecord *)newTransaction;

- (BOOL)notifyUpdatedTransaction:(nonnull DBTransactionRecord *)transaction;

- (void)notifyInitWithExistingWallet;

- (void)notifyInitWithoutExistingWallet;

- (BOOL)notifyShutdown;

- (BOOL)notifyCoreReady;

- (void)logPrint:(nonnull NSString *)str;

@end
