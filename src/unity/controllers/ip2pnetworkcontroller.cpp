// Copyright (c) 2020-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

//Workaround braindamaged 'hack' in libtool.m4 that defines DLL_EXPORT when building a dll via libtool (this in turn imports unwanted symbols from e.g. pthread that breaks static pthread linkage)
#ifdef DLL_EXPORT
#undef DLL_EXPORT
#endif

#include "net.h"
#include "net_processing.h"
#include "validation/validation.h"
#include "ui_interface.h"


// Unity specific includes
#include "../unity_impl.h"
#include "i_p2p_network_controller.hpp"
#include "i_p2p_network_listener.hpp"
#include "peer_record.hpp"
#include "banned_peer_record.hpp"

std::shared_ptr<IP2pNetworkListener> networkListener;
boost::signals2::connection enabledConn;
boost::signals2::connection disabledConn;

void IP2pNetworkController::setListener(const std::shared_ptr<IP2pNetworkListener>& networkListener_)
{
    networkListener = networkListener_;
    if (networkListener)
    {
        enabledConn = uiInterface.NotifyNetworkActiveChanged.connect([](bool networkActive)
        {
            if (networkActive)
            {
                networkListener->onNetworkEnabled();
            }
            else
            {
                networkListener->onNetworkDisabled();
            }
        });
        disabledConn = uiInterface.NotifyNumConnectionsChanged.connect([](int newNumConnections)
        {
            networkListener->onConnectionCountChanged(newNumConnections);
        });
    }
    else
    {
        enabledConn.disconnect();
        disabledConn.disconnect();
    }
    
    std::thread([=]
    {
        while(networkListener && g_connman)
        {
            networkListener->onBytesChanged(g_connman->GetTotalBytesRecv(), g_connman->GetTotalBytesSent());
            MilliSleep(30000);
        }
    }).detach();
}

void IP2pNetworkController::disableNetwork()
{
    if (g_connman)
    {
        g_connman->SetNetworkActive(false);
    }
}

void IP2pNetworkController::enableNetwork()
{
    if (g_connman)
    {
        g_connman->SetNetworkActive(true);
    }
}

std::vector<PeerRecord> IP2pNetworkController::getPeerInfo()
{
    std::vector<PeerRecord> ret;

    if (g_connman) {
        std::vector<CNodeStats> vstats;
        g_connman->GetNodeStats(vstats);
        for (CNodeStats& nstat: vstats)
        {
            int64_t nSyncedHeight = 0;
            int64_t nCommonHeight = 0;
            int64_t nMisbehavior = 0;
            CNodeStateStats stateStats;
            TRY_LOCK(cs_main, lockMain);
            if (lockMain && GetNodeStateStats(nstat.nodeid, stateStats))
            {
                nSyncedHeight = stateStats.nSyncHeight;
                nCommonHeight = stateStats.nCommonHeight;
                nMisbehavior = stateStats.nMisbehavior;
            }

            PeerRecord rec((int64_t)nstat.nodeid,
                           nstat.addr.ToString(),
                           nstat.addr.HostnameLookup(),
                           nstat.addrLocal,
                           nstat.addrBind.IsValid()?nstat.addrBind.ToString():"",
                           (int64_t)nstat.nStartingHeight,
                           (int64_t)nSyncedHeight,
                           (int64_t)nCommonHeight,
                           (int64_t)nstat.nTimeConnected,
                           (int64_t)nstat.nTimeOffset,
                           (int64_t)(nstat.dPingTime * 1000),
                           (int64_t)nstat.nLastSend,
                           (int64_t)nstat.nLastRecv,
                           (int64_t)nstat.nSendBytes,
                           (int64_t)nstat.nRecvBytes,
                           nstat.cleanSubVer,
                           (int64_t)nstat.nVersion,
                           (int64_t)nstat.nServices,
                           nstat.fInbound,
                           nstat.fWhitelisted,
                           nstat.fAddnode,
                           nstat.fRelayTxes,
                           (int64_t)nMisbehavior
                           );
            ret.emplace_back(rec);
        }
    }

    return ret;
}

std::vector<BannedPeerRecord> IP2pNetworkController::listBannedPeers()
{
    std::vector<BannedPeerRecord> ret;
    
    if (g_connman)
    {   
        banmap_t banMap;
        g_connman->GetBanned(banMap);
        for (const auto& [subNet, banEntry] : banMap)
        {
            BannedPeerRecord rec(subNet.ToString(), banEntry.nBanUntil, banEntry.nCreateTime, banEntry.banReasonToString());
            ret.push_back(rec);
        }
    }
    return ret;
}

bool IP2pNetworkController::banPeer(const std::string& address, int64_t banTimeInSeconds)
{
    if (g_connman)
    {
        std::string host;
        int port;
        SplitHostPort(address, port, host);
        CNetAddr netAddr;
        if (!LookupHost(host.c_str(), netAddr, false))
            return false;
        
        g_connman->Ban(netAddr, BanReasonManuallyAdded, banTimeInSeconds, false);
        return true;
    }
    return false;
}

bool IP2pNetworkController::unbanPeer(const std::string& address)
{
    if (g_connman)
    {
        CNetAddr netAddr;
        CSubNet subnet;
        if (LookupHost(address.c_str(), netAddr, false))
        {
            g_connman->Unban(netAddr);
            return true;
        }
        else if(LookupSubNet(address.c_str(), subnet))
        {
            g_connman->Unban(subnet);
            return true;
        }
    }
    return false;
}

bool IP2pNetworkController::disconnectPeer(int64_t nodeID)
{
    if (g_connman)
    {        
        g_connman->DisconnectNode(nodeID);
        return true;
    }
    return false;
}

bool IP2pNetworkController::ClearBanned()
{
    if (g_connman)
    {
        g_connman->ClearBanned();
        return true;
    }
    return false;
}
