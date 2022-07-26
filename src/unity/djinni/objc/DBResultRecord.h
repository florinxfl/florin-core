// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#import <Foundation/Foundation.h>

/**
 * Copyright (c) 2018-2020 The Gulden developers
 * Authored by: Malcolm MacLeod (mmacleod@gmx.com)
 * Distributed under the GULDEN software license, see the accompanying
 * file COPYING
 */
@interface DBResultRecord : NSObject
- (nonnull instancetype)initWithResult:(BOOL)result
                                  info:(nonnull NSString *)info;
+ (nonnull instancetype)resultRecordWithResult:(BOOL)result
                                          info:(nonnull NSString *)info;

@property (nonatomic, readonly) BOOL result;

@property (nonatomic, readonly, nonnull) NSString * info;

@end
