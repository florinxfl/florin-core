<template>
  <div class="account-header">
    <div v-if="!editMode" class="flex-row">
      <div v-if="isSingleAccount" class="flex-row flex-1">
        <div class="logo" />
        <div class="balance-row flex-1">
          <span>{{ balanceForDisplay }}</span>
          <span>{{ totalBalanceFiat }}</span>
        </div>
      </div>
      <div v-else class="left-colum" @click="editName">
        <account-tooltip type="Account" :account="account">
          <div class="flex-row flex-1">
            <div class="accountname ellipsis">{{ name }}</div>
            <fa-icon class="pen" :icon="['fal', 'fa-pen']" />
          </div>

          <div class="balance-row">
            <span>{{ balanceForDisplay }}</span>
            <span>{{ totalBalanceFiat }}</span>
          </div>
        </account-tooltip>
      </div>
      <div v-if="showBuySellButtons">
        <button outlined class="small" @click="buyCoins" :disabled="buyDisabled">{{ $t("buttons.buy") }}</button>
        <button outlined class="small" @click="sellCoins" :disabled="sellDisabled">{{ $t("buttons.sell") }}</button>
      </div>
      <div v-if="isSingleAccount" class="flex-row icon-buttons">
        <div class="icon-button">
          <fa-icon :icon="['fal', 'cog']" @click="showSettings" />
        </div>
        <div class="icon-button">
          <fa-icon :icon="['fal', lockIcon]" @click="changeLockSettings" />
        </div>
      </div>
    </div>
    <input v-else ref="accountNameInput" type="text" v-model="newAccountName" @keydown="onKeydown" @blur="cancelEdit" />
    <div v-if="!isSpending">
      <div v-if="!isLinkedToHoldin">
        <button outlined class="small" @click="linkToHoldin('add')" :disabled="sellDisabled">
          {{ $t("holding_account.add_to_holdin") }}
        </button>
      </div>
      <div v-else>
        <button outlined class="small" @click="linkToHoldin('remove')" :disabled="sellDisabled">
          {{ $t("holding_account.remove_from_holdin") }}
        </button>
      </div>
    </div>
  </div>
</template>

<script>
import { mapState } from "vuex";
import { AccountsController, BackendUtilities, LibraryController } from "../unity/Controllers";
import { formatMoneyForDisplay } from "../util.js";
import AccountTooltip from "./AccountTooltip.vue";
import EventBus from "../EventBus";
import axios from "axios";
import WalletPasswordDialog from "../components/WalletPasswordDialog";

export default {
  components: { AccountTooltip },
  name: "AccountHeader",
  data() {
    return {
      editMode: false,
      newAccountName: null,
      buyDisabled: false,
      sellDisabled: false,
      requestLinkToHoldin: false,
      isLinkedToHoldin: false,
      witnessKey: ""
    };
  },
  props: {
    account: {
      type: Object,
      default: () => {}
    },
    isSingleAccount: {
      type: Boolean,
      default: false
    }
  },
  computed: {
    ...mapState("app", ["rate"]),
    ...mapState("wallet", ["walletPassword", "unlocked"]),
    name() {
      return this.account ? this.account.label : null;
    },
    totalBalanceFiat() {
      if (!this.rate) return "";
      return `€ ${formatMoneyForDisplay(this.account.balance * this.rate, true)}`;
    },
    balanceForDisplay() {
      if (!this.account || this.account.balance === undefined) return "";
      return formatMoneyForDisplay(this.account.balance);
    },
    showBuySellButtons() {
      return !this.account || (this.account.type === "Desktop" && !this.editMode);
    },
    lockIcon() {
      return this.unlocked ? "unlock" : "lock";
    }
  },
  watch: {
    name: {
      immediate: true,
      handler() {
        this.editMode = false;
      }
    },
    walletPassword: {
      immediate: true,
      handler() {
        if (this.walletPassword && this.requestLinkToHoldin) {
          // Check if add or remove.
          if (this.isLinkedToHoldin) {
            this.holdinAPI("remove");
          } else {
            this.holdinAPI("add");
          }
        }
      }
    },
    account: {
      immediate: true,
      handler() {
        if (!this.isSpending) {
          this.checkForHoldinLink();
        }
      }
    }
  },
  mounted() {
    if (this.walletPassword && !this.isSpending) {
      this.getWitnessKey();
    }
    if (!this.isSpending) {
      this.checkForHoldinLink();
    }
  },
  methods: {
    getWitnessKey() {
      this.witnessKey = AccountsController.GetWitnessKeyURI(this.account.UUID);
    },
    checkForHoldinLink() {
      AccountsController.ListAccountLinksAsync(this.account.UUID).then(result => {
        const index = result.indexOf("holdin");

        this.isLinkedToHoldin = index > -1;
      });
    },
    editName() {
      this.newAccountName = this.name;
      this.editMode = true;
      this.$nextTick(() => {
        this.$refs["accountNameInput"].focus();
      });
    },
    onKeydown(e) {
      switch (e.keyCode) {
        case 13:
          this.changeAccountName();
          break;
        case 27:
          this.editMode = false;
          break;
      }
    },
    changeAccountName() {
      if (this.newAccountName !== this.account.label) {
        AccountsController.RenameAccount(this.account.UUID, this.newAccountName);
      }
      this.editMode = false;
    },
    cancelEdit() {
      this.editMode = false;
    },
    async sellCoins() {
      try {
        this.sellDisabled = true;
        const url = await BackendUtilities.GetSellSessionUrl();
        window.open(url, "sell-coins");
      } finally {
        this.sellDisabled = false;
      }
    },
    async buyCoins() {
      try {
        this.buyDisabled = true;
        const url = await BackendUtilities.GetBuySessionUrl();
        window.open(url, "buy-coins");
      } finally {
        this.buyDisabled = false;
      }
    },
    showSettings() {
      if (this.$route.path === "/settings/") return;
      this.$router.push({ name: "settings" });
    },
    changeLockSettings() {
      EventBus.$emit(this.unlocked ? "lock-wallet" : "unlock-wallet");
    },
    linkToHoldin(action) {
      this.requestLinkToHoldin = true;

      if (!this.walletPassword) {
        EventBus.$emit("show-dialog", {
          title: this.$t("password_dialog.unlock_wallet"),
          component: WalletPasswordDialog,
          showButtons: false
        });
        // Check the listener above for when this fires the Holdin API function.
      } else {
        LibraryController.UnlockWallet(this.walletPassword, 120);
        this.holdinAPI(action);
      }
    },
    async holdinAPI(action) {
      this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", true);
      AccountsController.GetWitnessKeyURIAsync(this.account.UUID).then(async key => {
        let result = null;
        if (action === "add") {
          let infoResult = await BackendUtilities.holdinAPIActions(key, "getinfo");

          if (infoResult.data.available === 1 && infoResult.data.active === "1") {
            // Account was linked elsewhere. Note that on Florin
            this.addAccountLink(this.account.UUID);
          } else if (infoResult.data.available === 1 && infoResult.data.active === "0") {
            // Account was linked and then removed. Reactivate.
            result = await BackendUtilities.holdinAPIActions(key, "activate");
            if (result.status_code === 200) {
              this.addAccountLink(this.account.UUID);
            } else {
              alert(`Holdin: ${result.status_message}`);
            }
          } else if (infoResult.data.available === 0 && infoResult.data.active === "0") {
            // Add account for the first time.
            result = await BackendUtilities.holdinAPIActions(key, "add");
            if (result.status_code === 200) {
              this.addAccountLink(this.account.UUID);
            } else {
              this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", false);
              alert(`Holdin: ${result.status_message}`);
            }
          } else {
            this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", false);
            alert("Holdin: API Error");
          }
        } else {
          result = await BackendUtilities.holdinAPIActions(key, "remove");

          if (result.status_code === 200) {
            AccountsController.RemoveAccountLinkAsync(this.account.UUID, "holdin")
              .then(() => {
                this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", false);
                this.isLinkedToHoldin = false;
                this.requestLinkToHoldin = false;
              })
              .catch(err => {
                this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", false);
                alert(err.message);
              });
          } else {
            alert(`Holdin: ${result.status_message}`);
          }
        }
      });
    },
    addAccountLink(accountUID) {
      AccountsController.AddAccountLinkAsync(accountUID, "holdin")
        .then(() => {
          this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", false);
          this.requestLinkToHoldin = false;
          this.isLinkedToHoldin = true;
        })
        .catch(err => {
          alert(err.message);
        });
    }
  }
};
</script>

<style lang="less" scoped>
.account-header {
  width: 100%;
  height: var(--header-height);
  line-height: var(--header-height);

  & > div {
    align-items: center;
    justify-content: center;
    height: var(--header-height);
  }
}

.left-colum {
  display: flex;
  flex-direction: column;
  flex: 1;
  white-space: nowrap;
  overflow: hidden;
  height: 40px;
  cursor: pointer;
  position: relative;
}

.balance-row {
  line-height: 20px;
  font-size: 0.9em;

  & :first-child {
    margin-right: 10px;
  }
}

button.small {
  height: 20px !important;
  line-height: 20px !important;
  font-size: 10px !important;
  padding: 0 10px !important;
  margin-left: 5px;
}

.accountname {
  flex: 1;
  font-size: 1em;
  font-weight: 500;
  line-height: 20px;
  margin-right: 30px;
}

.pen {
  display: none;
  position: absolute;
  right: 5px;
  line-height: 20px;
}

.left-colum:hover .pen {
  display: block;
}

.logo {
  width: 22px;
  min-width: 22px;
  height: 22px;
  min-height: 22px;
  background: url("../img/logo-black.svg"), linear-gradient(transparent, transparent);
  background-size: cover;
  margin-right: 10px;
}

.icon-buttons {
  margin-left: 10px;
}

.icon-button {
  display: inline-block;
  padding: 0 13px;
  line-height: 40px;
  height: 40px;
  font-weight: 500;
  font-size: 1em;
  color: var(--primary-color);
  text-align: center;
  cursor: pointer;

  &:hover {
    background-color: #f5f5f5;
  }
}
</style>
