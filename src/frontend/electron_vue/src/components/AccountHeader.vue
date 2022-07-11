<template>
  <div>
    <confirm-dialog v-model="modal" />
    <div class="account-header">
      <div v-if="!editMode" class="flex-row">
        <div v-if="isSingleAccount" class="flex-row flex-1">
          <div class="logo" />
          <div class="balance-row flex-1">
            <span>{{ balanceForDisplay }}</span>
            <span>{{ totalBalanceFiat }}</span>
          </div>
        </div>
        <div v-else class="left-colum">
          <div>
            <account-tooltip type="Account" :account="account">
              <div style="display: flex; flex-direction: row">
                <div style="width: calc(100% - 45px)" @click="editName" class="flex-row flex-1">
                  <div class="accountname ellipsis">{{ name }}</div>
                  <fa-icon class="pen" :icon="['fal', 'fa-pen']" />
                </div>
                <div style=" width: 40px; text-align: center" @click="deleteAccount" class="trash flex-row ">
                  <fa-icon :icon="['fal', 'fa-trash']" />
                </div>
              </div>
              <div class="balance-row">
                <span>{{ balanceForDisplay }}</span>
                <span>{{ totalBalanceFiat }}</span>
              </div>
            </account-tooltip>
          </div>
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
    </div>
  </div>
</template>

<script>
import { mapState } from "vuex";
import { AccountsController, BackendUtilities } from "../unity/Controllers";
import { formatMoneyForDisplay } from "../util.js";
import AccountTooltip from "./AccountTooltip.vue";
import EventBus from "../EventBus";
import ConfirmDialog from "./ConfirmDialog.vue";

export default {
  components: { AccountTooltip, ConfirmDialog },
  name: "AccountHeader",
  data() {
    return {
      editMode: false,
      newAccountName: null,
      buyDisabled: false,
      sellDisabled: false,
      modal: null
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
    ...mapState("wallet", ["unlocked"]),
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
    }
  },
  methods: {
    editName() {
      this.newAccountName = this.name;
      this.editMode = true;
      this.$nextTick(() => {
        this.$refs["accountNameInput"].focus();
      });
    },
    deleteAccount() {
      this.showConfirmModal();
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
    showConfirmModal() {
      if (
        this.account.allBalances.availableIncludingLocked === 0 &&
        this.account.allBalances.unconfirmedIncludingLocked === 0 &&
        this.account.allBalances.immatureIncludingLocked === 0
      ) {
        this.modal = { title: "Confirm Delete", message: "Are you sure you want to delete the account?", closeModal: this.closeModal, confirm: this.confirm };
      } else {
        this.modal = { title: "Error", message: "Your account needs to be empty before you can delete it", showButtons: false, closeModal: this.closeModal };
      }
    },
    closeModal() {
      this.modal = null;
    },
    confirm() {
      this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", true);
      this.modal = null;
      setTimeout(() => {
        AccountsController.DeleteAccountAsync(this.account.UUID)
          .then(() => {
            this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", false);
          })
          .catch(err => {
            this.$store.dispatch("app/SET_ACTIVITY_INDICATOR", false);
            alert(err.message);
          });
      }, 1000);
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
  right: 45px;
  line-height: 20px;
}

.trash {
  display: none;
  position: absolute;
  right: 5px;
  line-height: 18px;
}

.left-colum:hover .trash {
  display: block;
  color: #ff0000;
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
