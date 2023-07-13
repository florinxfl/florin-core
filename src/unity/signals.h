// Copyright (c) 2018-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

#ifndef UNITY_SIGNAL_H
#define UNITY_SIGNAL_H

// Return true if all slots return true (or no slots), return false otherwise.
struct BooleanAndAllReturnValues
{
    typedef bool result_type;

    template<typename I>
    bool operator()(I first, I last) const
    {
        while (first != last) {
            if (!(*first)) return false;
            ++first;
        }
        return true;
    }
};

// Helper macros to connect a boost signal to connect in a thread safe way a Qt UI signal. Signal is queued and executed in the UI event loop.
#define THREADSAFE_CONNECT_UI_SIGNAL_TO_CORE_SIGNAL0(CORE_SIGNAL, UI_OBJ, UI_SIGNAL) CORE_SIGNAL.connect( [&]() { QMetaObject::invokeMethod(UI_OBJ, UI_SIGNAL, Qt::QueuedConnection); });
#define THREADSAFE_CONNECT_UI_SIGNAL_TO_CORE_SIGNAL1(CORE_SIGNAL, ARG1_TYPE, UI_OBJ, UI_SIGNAL) CORE_SIGNAL.connect( [&](ARG1_TYPE arg1) { QMetaObject::invokeMethod(UI_OBJ, UI_SIGNAL, Qt::QueuedConnection, Q_ARG(ARG1_TYPE, arg1)); });
#define THREADSAFE_CONNECT_UI_SIGNAL_TO_CORE_SIGNAL2(CORE_SIGNAL, ARG1_TYPE, ARG2_TYPE, UI_OBJ, UI_SIGNAL) CORE_SIGNAL.connect( [&](ARG1_TYPE arg1, ARG2_TYPE arg2) { QMetaObject::invokeMethod(UI_OBJ, UI_SIGNAL, Qt::QueuedConnection, Q_ARG(ARG1_TYPE, arg1), Q_ARG(ARG2_TYPE, arg2)); });

#endif
