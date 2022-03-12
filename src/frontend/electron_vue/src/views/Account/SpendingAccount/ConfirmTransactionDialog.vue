<template>
  <div class="confirm-transaction-dialog">
    <div class="tx-amount">{{ computedAmount }}</div>
    <div class="tx-fee">{{ computedFee }}</div>
    <div class="tx-to">
      <fa-icon :icon="['far', 'long-arrow-down']" />
    </div>
    <div class="tx-address">{{ address }}</div>
    <div class="agree-fee" v-if="amountWithFeeExceedsBalance">
      <div class="warning">The amount you want to send exceeds your balance when the transaction fee is included.</div>
      <input type="checkbox" id="agree" v-model="agreeSubtractFee" />
      <label for="agree">I agree to subtract the transaction fee from the amount.</label>
    </div>
    <button @click="confirm" :disabled="computedButtonDisabled" class="button">
      {{ $t("buttons.confirm") }}
    </button>
  </div>
</template>

<script>
import EventBus from "@/EventBus";
import { formatMoneyForDisplay } from "../../../util.js";
import { LibraryController, AccountsController } from "@/unity/Controllers";

export default {
  name: "ConfirmTransactionDialog",
  data() {
    return {
      fee: 0,
      amountWithFeeExceedsBalance: false,
      agreeSubtractFee: false
    };
  },
  props: {
    amount: null,
    address: null,
    password: null,
    subtractFee: null
  },
  mounted() {
    this.fee = LibraryController.FeeForRecipient(this.computedRequest);

    // if fee needs to be added it's possible the total amount exceeds the amount which is available for spending.
    if (!this.subtractFee) {
      let accountBalance = AccountsController.GetActiveAccountBalance();
      this.amountWithFeeExceedsBalance = accountBalance.availableExcludingLocked < this.amount + this.fee;
    }
  },
  computed: {
    computedRequest() {
      return {
        valid: true,
        address: this.address,
        label: "",
        desc: "",
        amount: this.amount
      };
    },
    computedAmount() {
      return `${formatMoneyForDisplay(this.amount)} ${this.$t("common.ticker_symbol")}`;
    },
    computedFee() {
      return `${formatMoneyForDisplay(this.fee, false, 8)} ${this.$t("common.ticker_symbol")} FEE`;
    },
    computedButtonDisabled() {
      if (!this.amountWithFeeExceedsBalance) return false;
      return !this.agreeSubtractFee;
    },
    computedSubtractFee() {
      return this.subtractFee || this.agreeSubtractFee;
    }
  },
  methods: {
    confirm() {
      // at this point the password should already be validated
      LibraryController.UnlockWallet(this.password);

      // try to make the payment
      let result = LibraryController.PerformPaymentToRecipient(this.computedRequest, this.computedSubtractFee);

      if (result !== 0) {
        // payment failed, log an error. have to make this more robust
        console.error(result);
      }

      // lock the wallet again
      LibraryController.LockWallet();

      EventBus.$emit("transaction-succeeded");
      EventBus.$emit("close-dialog");
    }
  }
};
</script>

<style lang="less" scoped>
.confirm-transaction-dialog {
  text-align: center;

  & > h4 {
    margin: 20px 0 0 0;
  }
}
.tx-amount {
  font-size: 1.6em;
  font-weight: 600;
  margin: 0 0 10px 0;
}
.tx-fee {
  font-size: 0.9em;
}
.tx-to {
  margin: 20px 0 10px 0;
  font-size: 1.6em;
}
.tx-address {
  padding: 10px 0 40px 0;
  font-weight: 500;
}
.agree-fee {
  margin-bottom: 40px;

  & > .warning {
    color: var(--error-color);
    margin-bottom: 10px;
  }

  & > input {
    margin-right: 5px;
  }
}
.button {
  width: 100%;
}
</style>
