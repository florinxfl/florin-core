// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#import <Foundation/Foundation.h>

@interface DBPeerRecord : NSObject
- (nonnull instancetype)initWithIp:(nonnull NSString *)ip
                          hostname:(nonnull NSString *)hostname
                            height:(int32_t)height
                           latency:(int32_t)latency
                         userAgent:(nonnull NSString *)userAgent
                          protocol:(int64_t)protocol;
+ (nonnull instancetype)peerRecordWithIp:(nonnull NSString *)ip
                                hostname:(nonnull NSString *)hostname
                                  height:(int32_t)height
                                 latency:(int32_t)latency
                               userAgent:(nonnull NSString *)userAgent
                                protocol:(int64_t)protocol;

@property (nonatomic, readonly, nonnull) NSString * ip;

@property (nonatomic, readonly, nonnull) NSString * hostname;

@property (nonatomic, readonly) int32_t height;

@property (nonatomic, readonly) int32_t latency;

@property (nonatomic, readonly, nonnull) NSString * userAgent;

@property (nonatomic, readonly) int64_t protocol;

@end
