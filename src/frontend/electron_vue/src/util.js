import store from "./store";

export function formatMoneyForDisplay(monetaryAmount, isFiat = false, minimumAmountOfDecimals = null) {
  //fixme: This should truncate not round
  let decimalPlaces = 2;

  if (isFiat) {
    decimalPlaces = 2;
  } else {
    decimalPlaces = store.state.app.decimals;
  }

  if (decimalPlaces < minimumAmountOfDecimals) {
    decimalPlaces = minimumAmountOfDecimals;
  }

  return (monetaryAmount / 100000000).toFixed(decimalPlaces);
}

export function displayToMonetary(displayAmount) {
  return displayAmount * 100000000;
}
