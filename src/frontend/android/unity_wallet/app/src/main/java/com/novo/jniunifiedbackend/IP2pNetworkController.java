// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from libunity.djinni

package com.novo.jniunifiedbackend;

import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;

/** C++ interface to control networking related aspects of the software */
public abstract class IP2pNetworkController {
    /** Register listener to be notified of networking events */
    public static void setListener(IP2pNetworkListener networklistener)
    {
        CppProxy.setListener(networklistener);
    }

    /** Turn p2p networking off */
    public static void disableNetwork()
    {
        CppProxy.disableNetwork();
    }

    /** Turn p2p networking on */
    public static void enableNetwork()
    {
        CppProxy.enableNetwork();
    }

    /** Get connected peer info */
    public static ArrayList<PeerRecord> getPeerInfo()
    {
        return CppProxy.getPeerInfo();
    }

    private static final class CppProxy extends IP2pNetworkController
    {
        private final long nativeRef;
        private final AtomicBoolean destroyed = new AtomicBoolean(false);

        private CppProxy(long nativeRef)
        {
            if (nativeRef == 0) throw new RuntimeException("nativeRef is zero");
            this.nativeRef = nativeRef;
        }

        private native void nativeDestroy(long nativeRef);
        public void _djinni_private_destroy()
        {
            boolean destroyed = this.destroyed.getAndSet(true);
            if (!destroyed) nativeDestroy(this.nativeRef);
        }
        protected void finalize() throws java.lang.Throwable
        {
            _djinni_private_destroy();
            super.finalize();
        }

        public static native void setListener(IP2pNetworkListener networklistener);

        public static native void disableNetwork();

        public static native void enableNetwork();

        public static native ArrayList<PeerRecord> getPeerInfo();
    }
}
