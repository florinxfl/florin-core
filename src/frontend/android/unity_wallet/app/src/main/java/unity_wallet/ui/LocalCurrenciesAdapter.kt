// Copyright (c) 2018-2022 The Centure developers
// Authored by: Malcolm MacLeod (mmacleod@gmx.com)
// Distributed under the GNU Lesser General Public License v3, see the accompanying
// file COPYING

package unity_wallet.ui

import android.content.Context
import android.preference.PreferenceManager
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import unity_wallet.AppContext
import unity_wallet.Currency
import unity_wallet.R
import unity_wallet.localCurrency
import kotlinx.android.synthetic.main.local_currency_list_item.view.*
import java.util.*

class LocalCurrenciesAdapter(context: Context, private val dataSource: TreeMap<String, Currency>) : BaseAdapter() {
    private val inflater: LayoutInflater = context.getSystemService(Context.LAYOUT_INFLATER_SERVICE) as LayoutInflater
    private var allRates = mapOf<String, Double>()

    override fun getCount(): Int {
        return dataSource.size
    }

    override fun getItem(position: Int) : Currency
    {
        return dataSource.values.toTypedArray()[position]
    }

    override fun getItemId(position: Int): Long {
        return position.toLong()
    }

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {

        val currencyRecord = getItem(position)
        val rowView = inflater.inflate(R.layout.local_currency_list_item, parent, false)
        rowView.textViewCurrencyName.text = currencyRecord.name
        rowView.textViewCurrencyCode.text = currencyRecord.code
        rowView.imageViewCurrencySelected.visibility = if (currencyRecord.code == localCurrency.code)
        {
            View.VISIBLE
        }
        else
        {
            View.GONE
        }
        if (currencyRecord.code == localCurrency.code && allRates.containsKey(currencyRecord.code)) {
            val rate = allRates.getValue(currencyRecord.code)
            rowView.exchangeRateView.text = currencyRecord.formatRate(rate)
            rowView.exchangeRateView.visibility = View.VISIBLE
        }
        else {
            rowView.exchangeRateView.visibility = View.GONE
        }
        return rowView
    }

    fun setSelectedPosition(position: Int) = PreferenceManager.getDefaultSharedPreferences(AppContext.instance).edit().putString("preference_local_currency",
        getItem(position).code
    ).apply()

    fun updateAllRates(rates: Map<String, Double>) {
        allRates = rates
        notifyDataSetChanged()
    }
}

