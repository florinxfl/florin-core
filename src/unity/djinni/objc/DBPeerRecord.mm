// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

#import "DBPeerRecord.h"


@implementation DBPeerRecord

- (nonnull instancetype)initWithIp:(nonnull NSString *)ip
                          hostname:(nonnull NSString *)hostname
                            height:(int32_t)height
                           latency:(int32_t)latency
                         userAgent:(nonnull NSString *)userAgent
                          protocol:(int64_t)protocol
{
    if (self = [super init]) {
        _ip = [ip copy];
        _hostname = [hostname copy];
        _height = height;
        _latency = latency;
        _userAgent = [userAgent copy];
        _protocol = protocol;
    }
    return self;
}

+ (nonnull instancetype)peerRecordWithIp:(nonnull NSString *)ip
                                hostname:(nonnull NSString *)hostname
                                  height:(int32_t)height
                                 latency:(int32_t)latency
                               userAgent:(nonnull NSString *)userAgent
                                protocol:(int64_t)protocol
{
    return [[self alloc] initWithIp:ip
                           hostname:hostname
                             height:height
                            latency:latency
                          userAgent:userAgent
                           protocol:protocol];
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<%@ %p ip:%@ hostname:%@ height:%@ latency:%@ userAgent:%@ protocol:%@>", self.class, (void *)self, self.ip, self.hostname, @(self.height), @(self.latency), self.userAgent, @(self.protocol)];
}

@end
