<template>
  <div class="account-header">
    <div class="flex-row" v-if="!editMode">
      <div class="left-colum" @click="editName">
        <div class="flex-row flex-1">
          <div class="accountname ellipsis">{{ name }}</div>
          <fa-icon class="pen" :icon="['fal', 'fa-pen']" />
        </div>
        <div class="balance-row">
          <span>{{ balanceForDisplay }}</span>
          <span>{{ totalBalanceFiat }}</span>
        </div>
      </div>
      <div v-if="isSpending">
        <button outlined class="small" @click="buyCoins" :disabled="buyDisabled">buy</button>
        <button outlined class="small" @click="sellCoins" :disabled="sellDisabled">sell</button>
      </div>
    </div>
    <input v-else ref="accountNameInput" type="text" v-model="newAccountName" @keydown="onKeydown" @blur="cancelEdit" />
  </div>
</template>

<script>
import { mapState } from "vuex";
import { formatMoneyForDisplay } from "../util.js";
import { AccountsController, BackendUtilities } from "@/unity/Controllers";

export default {
  name: "AccountHeader",
  data() {
    return {
      editMode: false,
      newAccountName: null,
      buyDisabled: false,
      sellDisabled: false
    };
  },
  props: {
    account: {
      type: Object,
      default: () => {}
    }
  },
  computed: {
    ...mapState("app", ["rate"]),
    name() {
      return this.account.label;
    },
    balance() {
      return `${this.balanceForDisplay} ${this.totalBalanceFiat}`;
    },
    totalBalanceFiat() {
      if (!this.rate) return "";
      return `€ ${formatMoneyForDisplay(this.account.balance * this.rate, true)}`;
    },
    balanceForDisplay() {
      if (this.account.balance == null) return "";
      return formatMoneyForDisplay(this.account.balance);
    },
    isSpending() {
      return this.account.type === "Desktop";
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
        let url = await BackendUtilities.GetSellSessionUrl();
        if (!url) {
          url = "https://florin.org/sell";
        }
        window.open(url, "sell-florin");
      } finally {
        this.sellDisabled = false;
      }
    },
    async buyCoins() {
      try {
        this.buyDisabled = true;
        let url = await BackendUtilities.GetBuySessionUrl();
        if (!url) {
          url = "https://florin.org/buy";
        }
        window.open(url, "buy-florin");
      } finally {
        this.buyDisabled = false;
      }
    }
  }
};
</script>

<style lang="less" scoped>
.account-header {
  width: 100%;
  height: var(--header-height);
  line-height: 40px;
  padding: calc((var(--header-height) - 40px) / 2) 0;
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
  height: 20px;
  line-height: 20px;
  font-size: 10px;
  padding: 0 10px;
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
</style>
