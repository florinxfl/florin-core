import store from "./store";
import { Parser } from "@json2csv/plainjs";

export function formatMoneyForDisplay(monetaryAmount, isFiat = false, minimumAmountOfDecimals = 0) {
  // default use 2 decimals;
  let decimals = 2;

  // for fiat we always use 2 decimals, but if it's not for fiat...
  if (!isFiat) {
    // get number of decimals decimals from state
    decimals = store.state.app.decimals;
    // use minimumAmountOfDecimals if it's more than current decimals
    if (minimumAmountOfDecimals > decimals) {
      decimals = minimumAmountOfDecimals;
    }
  }

  // trunctate the amount to specified decimal places
  return (Math.floor(monetaryAmount / Math.pow(10, 8 - decimals)) / Math.pow(10, decimals)).toFixed(decimals);
}

export function displayToMonetary(displayAmount) {
  // note: do not divide by 100000000 because it will sometimes give rounding issues

  // convert the amount to string
  let str = displayAmount.toString();
  // find the index decimal separator
  let idx = str.indexOf(".");

  // if the decimal separator doesn't exist...
  if (idx === -1) {
    // add the decimal separator at the end
    str += ".";
    // and update the index
    idx = str.length;
  }
  // add 8 zeroes at the end, maybe there are too many now
  str += "0".repeat(8);
  // but here they are removed from the tail
  str = str.substring(0, idx + 9);
  // now remove the . and then parse the string to integer
  return parseInt(str.replace(".", ""));
}

function formatTime(timestamp) {
  let date = new Date(timestamp * 1000);
  return `${date.getDate()}/${date.getMonth() + 1}/${date.getFullYear()}, ${("0" + date.getHours()).slice(-2)}:${("0" + date.getMinutes()).slice(-2)}`;
}

export function downloadTransactionList(transactions, fileName) {
  var transformedTransactionArray = [];
  transactions.forEach(async (tx, index) => {
    transformedTransactionArray.push({
      ...tx,
      timestamp: formatTime(tx.timestamp),
      change: formatMoneyForDisplay(tx.change)
    });

    if (index + 1 === transactions.length) {
      try {
        const parser = new Parser();
        const csv = parser.parse(transformedTransactionArray);
        var blob = new Blob([csv], { type: "text/csv" });
        var url = URL.createObjectURL(blob);

        var downloadLink = document.createElement("a");
        downloadLink.href = url;
        downloadLink.setAttribute("download", `${fileName}.csv`);
        downloadLink.click();
      } catch (err) {
        alert(err.message);
      }
    }
  });
}
