// Copyright (c) 2019-2022 The Centure developers
// Authored by: Willem de Jonge (willem@isnapp.nl), Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

package unity_wallet.ui.widgets

import android.content.Context
import android.util.AttributeSet
import android.widget.ViewSwitcher
import unity_wallet.jniunifiedbackend.ILibraryController
import unity_wallet.Authentication
import unity_wallet.R
import kotlinx.android.synthetic.main.pref_view_recovery.view.*

class AuthenticatedRecoveryView(context: Context?, attrs: AttributeSet?) : ViewSwitcher(context, attrs) {

    override fun onFinishInflate() {
        super.onFinishInflate()
        val lockedView = getChildAt(0)
        lockedView.setOnClickListener {
            Authentication.instance.authenticate(context!!, null, context?.getString(R.string.show_recovery_msg)) {
                displayedChild = 1
                //TODO: Reintroduce showing birth time here if/when we decide we want it in future
                recoveryPhrase.text = ILibraryController.GetRecoveryPhrase().phrase
            }
        }
        recoveryPhrase.setOnClickListener {
            displayedChild = 0
        }
    }
}
