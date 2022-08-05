// Copyright (c) 2019 The Florin developers
// Authored by: Willem de Jonge (willem@isnapp.nl)
// Distributed under the Florin software license, see the accompanying
// file COPYING

package unity_wallet.ui.widgets

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.TextView
import unity_wallet.BuildConfig
import unity_wallet.UnityCore

class UnityBuildInfoView(context: Context?, attrs: AttributeSet?) : TextView(context, attrs) {
    init {
        @Suppress("ConstantConditionIf")
        if (BuildConfig.TESTNET) {
            text = "Unity core ${UnityCore.instance.buildInfo}"
        }
        else {
            visibility = View.GONE
        }
    }
}
