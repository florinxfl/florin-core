// Copyright (c) 2018-2022 The Centure developers
// Authored by: Willem de Jonge (willem@isnapp.nl)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

package unity_wallet.ui.monitor

import android.annotation.SuppressLint
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import unity_wallet.jniunifiedbackend.PeerRecord
import unity_wallet.R
import kotlinx.android.synthetic.main.peer_list_row.view.*



class PeerListAdapter : ListAdapter<PeerRecord, PeerListAdapter.ItemViewHolder>(DiffCallback())  {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ItemViewHolder {
        return ItemViewHolder(
                LayoutInflater.from(parent.context)
                        .inflate(R.layout.peer_list_row, parent, false)
        )
    }

    override fun onBindViewHolder(holder: ItemViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    class ItemViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        fun bind(item: PeerRecord) = with(itemView) {

            itemView.peer_list_row_ip.text = if (item.hostname.isEmpty()) item.ip else item.hostname
            itemView.peer_list_row_height.text = if (item.syncedHeight > 0) item.syncedHeight.toString() + " blocks" else if (item.startHeight > 0) item.startHeight.toString() + " blocks" else null
            itemView.peer_list_row_user_agent.text = item.userAgent
            itemView.peer_list_row_protocol.text = item.protocol.toString()
            @SuppressLint("SetTextI18n")
            itemView.peer_list_row_ping.text = item.latency.toString()+"ms"
        }
    }

    override fun getItemId(position: Int): Long
    {
        val product = getItem(position)
        return product.id
    }
}

class DiffCallback : DiffUtil.ItemCallback<PeerRecord>() {
    override fun areItemsTheSame(oldItem: PeerRecord, newItem: PeerRecord): Boolean {
        return oldItem.ip == newItem.ip
    }

    override fun areContentsTheSame(oldItem: PeerRecord, newItem: PeerRecord): Boolean {
        return oldItem == newItem
    }
}
