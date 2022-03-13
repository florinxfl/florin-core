<template>
  <div class="send-view flex-col">
    <portal v-if="UIConfig.showSidebar" to="sidebar-right-title">
      {{ $t("buttons.send") }}
    </portal>
    <div class="main">
      <div class="flex flex-row">
        <input
          v-model="amount"
          ref="amount"
          type="number"
          step="0.00000001"
          :placeholder="computedAmountPlaceholder"
          min="0"
          :readonly="computedAmountReadonly"
        />
        <div v-if="maxAmount > 0" class="max" @click="setUseMax">
          <span>MAX</span>
        </div>
      </div>
      <content-wrapper>
        <p>
          {{ this.useMax ? $t("send_coins.fee_will_be_subtracted") : "&nbsp;" }}
        </p>
      </content-wrapper>
      <input v-model="address" type="text" :placeholder="$t('send_coins.enter_coins_address')" :class="addressClass" @keydown="isAddressInvalid = false" />
      <input v-model="label" type="text" :placeholder="$t('send_coins.enter_label')" />
      <input
        v-model="password"
        type="password"
        v-show="walletPassword === null"
        :placeholder="$t('common.enter_your_password')"
        :class="passwordClass"
        @keydown="onPasswordKeydown"
      />
    </div>
    <div class="flex-row">
      <button @click="clearInput" outlined :disabled="disableClearButton">
        {{ $t("buttons.clear") }}
      </button>
      <button class="stretch" @click="showConfirmation" :disabled="disableSendButton">
        {{ $t("buttons.send") }}
      </button>
      <button @click="sellCoins" :disabled="sellDisabled">
        {{ $t("buttons.sell_coins") }}
      </button>
    </div>
  </div>
</template>

<script>
import { mapState, mapGetters } from "vuex";
import { displayToMonetary, formatMoneyForDisplay } from "../../../util.js";
import { LibraryController } from "@/unity/Controllers";
import ConfirmTransactionDialog from "./ConfirmTransactionDialog";
import EventBus from "@/EventBus";
import { BackendUtilities } from "@/unity/Controllers";
import UIConfig from "../../../../ui-config.json";

export default {
  name: "Send",
  data() {
    return {
      amount: null,
      maxAmount: null,
      address: null,
      label: null,
      password: null,
      isAddressInvalid: false,
      isPasswordInvalid: false,
      sellDisabled: false,
      useMax: false,
      UIConfig: UIConfig
    };
  },
  computed: {
    ...mapState("wallet", ["walletPassword"]),
    ...mapState("app", ["decimals"]),
    ...mapGetters("wallet", ["account"]),
    computedAmountReadonly() {
      return this.maxAmount === 0 || this.useMax;
    },
    computedPassword() {
      return this.walletPassword ? this.walletPassword : this.password || "";
    },
    computedAmountPlaceholder() {
      return `0.${"0".repeat(this.decimals || 2)}`;
    },
    computedMaxForDisplay() {
      return formatMoneyForDisplay(this.maxAmount);
    },
    addressClass() {
      return this.isAddressInvalid ? "error" : "";
    },
    passwordClass() {
      return this.isPasswordInvalid ? "error" : "";
    },
    hasErrors() {
      return this.isAddressInvalid || this.isPasswordInvalid;
    },
    disableClearButton() {
      if (this.amount !== null && !isNaN(parseFloat(this.amount))) return false;
      if (this.address !== null && this.address.length > 0) return false;
      if (this.password !== null && this.password.length > 0) return false;
      return true;
    },
    disableSendButton() {
      if (isNaN(parseFloat(this.amount))) return true;
      if (this.address === null || this.address.trim().length === 0) return true;
      if (this.computedPassword.trim().length === 0) return true;
      return false;
    }
  },
  mounted() {
    this.$refs.amount.focus();
    EventBus.$on("transaction-succeeded", this.onTransactionSucceeded);
  },
  beforeDestroy() {
    EventBus.$off("transaction-succeeded", this.onTransactionSucceeded);
  },
  watch: {
    account: {
      immediate: true,
      handler() {
        this.maxAmount = this.account.spendable;
      }
    },
    maxAmount() {
      if (this.useMax) {
        this.amount = this.computedMaxForDisplay;
      }
    },
    amount() {
      if (displayToMonetary(this.amount) >= this.maxAmount) {
        this.useMax = true;
      } else {
        this.useMax = false;
      }
    },
    useMax() {
      this.amount = this.useMax ? this.computedMaxForDisplay : null;
    }
  },
  methods: {
    async sellCoins() {
      try {
        this.sellDisabled = true;
        let url = await BackendUtilities.GetSellSessionUrl();
        if (!url) {
          url = "https://florin.org/sell";
        }
        window.open(url, "sell-florin");
      } finally {
        this.sellDisabled = false;
      }
    },
    onPasswordKeydown() {
      this.isPasswordInvalid = false;
    },
    clearInput() {
      this.amount = null;
      //this.useMax = false;
      this.address = null;
      this.password = null;
      this.label = null;
      this.$refs.amount.focus();
    },
    showConfirmation() {
      // amount is always less then or equal to the floored spendable amount
      // if useMax is checked, use the maxAmount and subtract the fee from the amount
      let amount = this.useMax ? this.maxAmount : displayToMonetary(this.amount);

      // validate address
      this.isAddressInvalid = !LibraryController.IsValidNativeAddress(this.address);

      // validate password (confirmation dialog unlocks/locks when user confirms so don't leave it unlocked here)
      this.isPasswordInvalid = !this.validatePassword(this.computedPassword);

      if (this.hasErrors) return;

      EventBus.$emit("show-dialog", {
        title: this.$t("send_coins.confirm_transaction"),
        component: ConfirmTransactionDialog,
        componentProps: {
          amount: amount,
          address: this.address,
          password: this.password,
          subtractFee: this.useMax
        },
        showButtons: false
      });
    },
    onTransactionSucceeded() {
      this.$router.push({ name: "transactions" });
    },
    validatePassword(password) {
      // validation can only be done by unlocking the wallet, but make sure to lock the wallet afterwards
      const isValid = LibraryController.UnlockWallet(password);
      LibraryController.LockWallet();
      return isValid;
    },
    setUseMax() {
      this.useMax = true;
    }
  }
};
</script>

<style lang="less" scoped>
.send-view {
  height: 100%;
  flex: 1;

  .main {
    flex: 1;
  }
}

button {
  width: 150px;
}

.stretch {
  margin: 0 30px;
  flex: 1;
}

.max {
  line-height: 40px;
  height: 40px;
  font-weight: 600;
  font-size: 0.75em;
  background-color: var(--primary-color);
  color: #f5f5f5;
  padding: 0 20px;
  cursor: pointer;
}
</style>
