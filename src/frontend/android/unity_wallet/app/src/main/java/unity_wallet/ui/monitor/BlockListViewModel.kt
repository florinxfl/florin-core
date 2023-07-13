// Copyright (c) 2018-2022 The Centure developers
// Authored by: Willem de Jonge (willem@isnapp.nl)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

package unity_wallet.ui.monitor

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import unity_wallet.jniunifiedbackend.BlockInfoRecord

class BlockListViewModel : ViewModel() {
    private lateinit var blocks: MutableLiveData<List<BlockInfoRecord>>

    fun getBlocks(): LiveData<List<BlockInfoRecord>> {
        if (!::blocks.isInitialized) {
            blocks = MutableLiveData()
        }
        return blocks
    }

    fun setBlocks(blocks_: List<BlockInfoRecord>) {
        if (!::blocks.isInitialized) {
            blocks = MutableLiveData()
        }
        blocks.value = blocks_
    }
}
