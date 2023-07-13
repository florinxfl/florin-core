// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// File contains modifications by: The Centure developers
// All modifications:
// Copyright (c) 2016-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING
#include "noui.h"

#include "ui_interface.h"
#include "util.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <cstdio>
#include <stdint.h>
#include <string>

#include <boost/bind/bind.hpp> 
using namespace boost::placeholders;

static bool noui_ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style)
{
    bool fSecure = style & CClientUIInterface::SECURE;
    style &= ~CClientUIInterface::SECURE;

    std::string strCaption;
    // Check for usage of predefined caption
    switch (style) {
    case CClientUIInterface::MSG_ERROR:
        strCaption += _("Error");
        break;
    case CClientUIInterface::MSG_WARNING:
        strCaption += _("Warning");
        break;
    case CClientUIInterface::MSG_INFORMATION:
        strCaption += _("Information");
        break;
    default:
        strCaption += caption; // Use supplied caption (can be empty)
    }

    if (!fSecure)
        LogPrintf("%s: %s\n", strCaption, message);
    fprintf(stderr, "%s: %s\n", strCaption.c_str(), message.c_str());
    return false;
}

static bool noui_ThreadSafeQuestion(const std::string& /* ignored interactive message */, const std::string& message, const std::string& caption, unsigned int style)
{
    return noui_ThreadSafeMessageBox(message, caption, style);
}

static void noui_InitMessage(const std::string& message)
{
    LogPrintf("init message: %s\n", message);
}

#ifdef ENABLE_WALLET
static void NotifyRequestUnlockS(CWallet* wallet, std::string reason)
{
    SecureString passwd = GetArg("-unlockpasswd", "").c_str();
    if (!passwd.empty())
    {
        if (!wallet->Unlock(passwd))
        {
            fprintf(stderr, "Wallet requested unlock but -unlockpasswd was invalid - please unlock via RPC or in the case of an upgrade temporarily set -unlockpasswd in " GLOBAL_APPNAME ".conf: reason [%s]\n", reason.c_str());
            return;
        }
    }
    fprintf(stderr, "Wallet requested unlock but could not unlock - please unlock via RPC or in the case of an upgrade temporarily set -unlockpasswd in " GLOBAL_APPNAME ".conf: reason [%s]\n", reason.c_str());
}

static void NotifyRequestUnlockWithCallbackS(CWallet* wallet, std::string reason, std::function<void (void)> successCallback)
{
    SecureString passwd = GetArg("-unlockpasswd", "").c_str();
    if (!passwd.empty())
    {
        if (!wallet->Unlock(passwd))
        {
            fprintf(stderr, "Wallet requested unlock but -unlockpasswd was invalid - please unlock via RPC or in the case of an upgrade temporarily set -unlockpasswd in " GLOBAL_APPNAME ".conf: reason for request [%s]\n", reason.c_str());
            return;
        }
    }
    else
    {
        fprintf(stderr, "Wallet requested unlock but could not unlock - please unlock via RPC or in the case of an upgrade temporarily set -unlockpasswd in " GLOBAL_APPNAME ".conf: reason for request [%s]\n", reason.c_str());
        return;
    }
    successCallback();
}
#endif

void noui_connect()
{
    // Connect daemon signal handlers
    uiInterface.ThreadSafeMessageBox.connect(noui_ThreadSafeMessageBox);
    uiInterface.ThreadSafeQuestion.connect(noui_ThreadSafeQuestion);
    uiInterface.InitMessage.connect(noui_InitMessage);

    #ifdef ENABLE_WALLET
    uiInterface.RequestUnlock.connect(boost::bind(NotifyRequestUnlockS, _1, _2));
    uiInterface.RequestUnlockWithCallback.connect(boost::bind(NotifyRequestUnlockWithCallbackS, _1, _2, _3));
    #endif
}
